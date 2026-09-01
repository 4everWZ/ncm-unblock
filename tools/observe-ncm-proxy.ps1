[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$NcmPath,

    [string]$ObserverPath = (Join-Path $PSScriptRoot '..\build\win32-release\src\proxy_observer\ncm_proxy_observer.exe'),

    [ValidateRange(1, 300)]
    [int]$ObservationSeconds = 15,

    [ValidateRange(1, 1000)]
    [int]$MaxEvents = 50,

    [switch]$ForceCloseAfterTimeout,

    [ValidateSet('CommandLine', 'ClientUi')]
    [string]$RouteMode = 'CommandLine'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Test-ByteArrayEqual {
    param([byte[]]$Left, [byte[]]$Right)

    if ($Left.Length -ne $Right.Length) {
        return $false
    }
    for ($index = 0; $index -lt $Left.Length; ++$index) {
        if ($Left[$index] -ne $Right[$index]) {
            return $false
        }
    }
    return $true
}

function Read-FileExclusively {
    param([string]$Path)

    $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::None)
    try {
        if ($stream.Length -gt [int]::MaxValue) {
            throw 'Recovery source is unexpectedly large.'
        }
        $bytes = [byte[]]::new([int]$stream.Length)
        $offset = 0
        while ($offset -lt $bytes.Length) {
            $read = $stream.Read($bytes, $offset, $bytes.Length - $offset)
            if ($read -eq 0) {
                throw 'Recovery source ended before the snapshot was complete.'
            }
            $offset += $read
        }
        return ,$bytes
    } finally {
        $stream.Dispose()
    }
}

function Get-PrivateProxyState {
    $path = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Internet Settings'
    $item = Get-ItemProperty -LiteralPath $path
    $state = [ordered]@{}
    foreach ($name in @('ProxyEnable', 'ProxyServer', 'ProxyOverride', 'AutoConfigURL', 'AutoDetect')) {
        $property = $item.PSObject.Properties[$name]
        $state[$name] = if ($null -eq $property) {
            [ordered]@{ Present = $false; Value = $null }
        } else {
            [ordered]@{ Present = $true; Value = $property.Value }
        }
    }
    return ($state | ConvertTo-Json -Compress -Depth 3)
}

function Get-TargetNcmProcesses {
    $result = @()
    foreach ($process in @(Get-Process -Name cloudmusic, cloudmusic_reporter -ErrorAction SilentlyContinue)) {
        try {
            $candidatePath = [IO.Path]::GetFullPath($process.Path)
            if ($candidatePath -ieq $script:ResolvedNcmPath -or
                $candidatePath -ieq $script:TargetReporterPath) {
                $result += $process
            }
        } catch {
            # A process can exit while its path is being read; it is absent from the next snapshot.
        }
    }
    return $result
}

function Get-AllNcmProcesses {
    return @(Get-Process -Name cloudmusic, cloudmusic_reporter -ErrorAction SilentlyContinue)
}

function Wait-AllNcmExit {
    param([int]$TimeoutSeconds)

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $emptySnapshots = 0
    do {
        if (@(Get-AllNcmProcesses).Count -eq 0) {
            ++$emptySnapshots
            if ($emptySnapshots -ge 3) {
                return $true
            }
        } else {
            $emptySnapshots = 0
        }
        Start-Sleep -Milliseconds 250
    } while ([DateTime]::UtcNow -lt $deadline)
    return $false
}

function Stop-OwnedObserver {
    param([Diagnostics.Process]$Process)

    if ($null -eq $Process) {
        return $true
    }
    try {
        if ($Process.HasExited) {
            return $true
        }
        $Process.Kill()
        return $Process.WaitForExit(5000)
    } catch {
        return $false
    }
}

function Stop-LaunchedNcmTree {
    param([Diagnostics.Process]$Process)

    if ($null -eq $Process) {
        return $false
    }
    try {
        if ($Process.HasExited) {
            return (Wait-AllNcmExit -TimeoutSeconds 2)
        }

        $instances = @(Get-CimInstance Win32_Process)
        $depthById = @{}
        $depthById[[uint32]$Process.Id] = 0
        $changed = $true
        while ($changed) {
            $changed = $false
            foreach ($instance in $instances) {
                $processId = [uint32]$instance.ProcessId
                $parentId = [uint32]$instance.ParentProcessId
                if (-not $depthById.ContainsKey($processId) -and $depthById.ContainsKey($parentId)) {
                    $depthById[$processId] = $depthById[$parentId] + 1
                    $changed = $true
                }
            }
        }

        $tree = @($instances | Where-Object { $depthById.ContainsKey([uint32]$_.ProcessId) })
        $root = @($tree | Where-Object { [uint32]$_.ProcessId -eq [uint32]$Process.Id })
        if ($root.Count -ne 1) {
            return $false
        }

        $allowedPaths = @($script:ResolvedNcmPath, $script:TargetReporterPath)
        foreach ($instance in $tree) {
            if ([string]::IsNullOrWhiteSpace($instance.ExecutablePath)) {
                return $false
            }
            $candidatePath = [IO.Path]::GetFullPath($instance.ExecutablePath)
            if (@($allowedPaths | Where-Object { $_ -ieq $candidatePath }).Count -ne 1) {
                return $false
            }
        }

        $orderedTree = @($tree | Sort-Object `
                @{ Expression = { if ([uint32]$_.ProcessId -eq [uint32]$Process.Id) { 0 } else { 1 } } }, `
                @{ Expression = { -$depthById[[uint32]$_.ProcessId] } })
        foreach ($instance in $orderedTree) {
            $current = Get-CimInstance Win32_Process -Filter "ProcessId = $($instance.ProcessId)" -ErrorAction SilentlyContinue
            if ($null -eq $current) {
                continue
            }
            if ($current.CreationDate -ne $instance.CreationDate -or
                [string]::IsNullOrWhiteSpace($current.ExecutablePath) -or
                [IO.Path]::GetFullPath($current.ExecutablePath) -ine [IO.Path]::GetFullPath($instance.ExecutablePath)) {
                return $false
            }
            [Diagnostics.Process]::GetProcessById([int]$instance.ProcessId).Kill()
        }
        return (Wait-AllNcmExit -TimeoutSeconds 10)
    } catch {
        return $false
    }
}

function Request-NormalNcmExit {
    param(
        [Diagnostics.Process]$Process,
        [int]$TimeoutSeconds
    )

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        if (@(Get-AllNcmProcesses).Count -eq 0) {
            return (Wait-AllNcmExit -TimeoutSeconds 1)
        }
        try {
            if (-not $Process.HasExited) {
                $Process.Refresh()
                if ($Process.MainWindowHandle -ne 0) {
                    [void]$Process.CloseMainWindow()
                }
            }
        } catch {
            # The owned root can exit while a normal close is being retried.
        }
        Start-Sleep -Milliseconds 250
    } while ([DateTime]::UtcNow -lt $deadline)
    return $false
}

function Test-TargetCommandLineArgument {
    param([string]$ExpectedArgument)

    foreach ($process in @(Get-TargetNcmProcesses | Where-Object { $_.ProcessName -ieq 'cloudmusic' })) {
        try {
            $instance = Get-CimInstance Win32_Process -Filter "ProcessId = $($process.Id)"
            if ($null -ne $instance.CommandLine -and
                $instance.CommandLine.IndexOf($ExpectedArgument, [StringComparison]::Ordinal) -ge 0) {
                return $true
            }
        } catch {
            # Retry through the caller's bounded startup loop.
        }
    }
    return $false
}

$script:ResolvedNcmPath = (Resolve-Path -LiteralPath $NcmPath).Path
$resolvedObserverPath = (Resolve-Path -LiteralPath $ObserverPath).Path
$script:TargetReporterPath = Join-Path ([IO.Path]::GetDirectoryName($script:ResolvedNcmPath)) 'cloudmusic_reporter.exe'

$version = [Diagnostics.FileVersionInfo]::GetVersionInfo($script:ResolvedNcmPath).FileVersion
if ($version -ne '2.9.7.199711') {
    throw "Expected NCM 2.9.7.199711, found '$version'."
}
if (@(Get-AllNcmProcesses).Count -ne 0) {
    throw 'Every NCM instance must be closed normally before the shared private-state experiment starts.'
}
$principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if ($principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run this user-scoped experiment from a non-elevated PowerShell session.'
}

$localDataPath = Join-Path $env:LOCALAPPDATA 'Netease\CloudMusic\localdata'
if (-not (Test-Path -LiteralPath $localDataPath -PathType Leaf)) {
    throw 'NCM localdata was not found; refusing an experiment without a recovery source.'
}

$tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$experimentId = [Guid]::NewGuid().ToString('N')
$experimentPath = [IO.Path]::GetFullPath((Join-Path $tempRoot ('ncm-proxy-experiment-' + $experimentId)))
if (-not $experimentPath.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Temporary recovery path resolved outside the system temporary directory.'
}

$backupPath = Join-Path $experimentPath 'localdata.backup'
$manifestPath = Join-Path $experimentPath 'recovery.json'
$observerOutputPath = Join-Path $experimentPath 'observer.stdout'
$observerErrorPath = Join-Path $experimentPath 'observer.stderr'
$restoreSiblingPath = Join-Path ([IO.Path]::GetDirectoryName($localDataPath)) ('localdata.codex-restore-' + $experimentId)
$displacedPath = Join-Path ([IO.Path]::GetDirectoryName($localDataPath)) ('localdata.codex-displaced-' + $experimentId)

$observer = $null
$launchedNcm = $null
$backupCreated = $false
$routingObserved = $false
$observerEventCount = 0
$stateRestored = $false
$systemProxyUnchanged = $null
$primaryError = $null
$recoveryError = $null
$cleanupError = $null
$proxyStateBefore = $null
$originalBytes = $null
$originalAcl = $null
$originalCreationTimeUtc = $null
$originalLastWriteTimeUtc = $null
$originalAttributes = $null

try {
    [void](New-Item -ItemType Directory -Path $experimentPath)
    $privateAcl = [Security.AccessControl.DirectorySecurity]::new()
    $privateAcl.SetAccessRuleProtection($true, $false)
    $currentSid = [Security.Principal.WindowsIdentity]::GetCurrent().User
    $systemSid = [Security.Principal.SecurityIdentifier]::new('S-1-5-18')
    $inheritance = [Security.AccessControl.InheritanceFlags]'ContainerInherit, ObjectInherit'
    $propagation = [Security.AccessControl.PropagationFlags]::None
    $allow = [Security.AccessControl.AccessControlType]::Allow
    $privateAcl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new(
            $currentSid, 'FullControl', $inheritance, $propagation, $allow))
    $privateAcl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new(
            $systemSid, 'FullControl', $inheritance, $propagation, $allow))
    Set-Acl -LiteralPath $experimentPath -AclObject $privateAcl

    if (-not (Wait-AllNcmExit -TimeoutSeconds 1)) {
        throw 'An NCM instance started while the private snapshot was being prepared.'
    }
    $localDataItem = Get-Item -LiteralPath $localDataPath
    $originalBytes = Read-FileExclusively -Path $localDataPath
    $originalAcl = Get-Acl -LiteralPath $localDataPath
    $originalCreationTimeUtc = $localDataItem.CreationTimeUtc
    $originalLastWriteTimeUtc = $localDataItem.LastWriteTimeUtc
    $originalAttributes = $localDataItem.Attributes
    $proxyStateBefore = Get-PrivateProxyState

    Copy-Item -LiteralPath $localDataPath -Destination $backupPath
    $backupBytes = [IO.File]::ReadAllBytes($backupPath)
    if (-not (Test-ByteArrayEqual -Left $originalBytes -Right $backupBytes)) {
        throw 'Private recovery copy does not match localdata.'
    }
    $backupCreated = $true
    $sha256 = [Security.Cryptography.SHA256]::Create()
    try {
        $backupSha256 = -join @($sha256.ComputeHash($originalBytes) | ForEach-Object { $_.ToString('x2') })
    } finally {
        $sha256.Dispose()
    }

    [ordered]@{
        SchemaVersion = 1
        BundleId = $experimentId
        TargetPath = $localDataPath
        BackupSha256 = $backupSha256
        Sddl = $originalAcl.Sddl
        CreationTimeUtc = $originalCreationTimeUtc.ToString('O')
        LastWriteTimeUtc = $originalLastWriteTimeUtc.ToString('O')
        Attributes = [int]$originalAttributes
        Length = $originalBytes.Length
        SecurityDescriptorScope = 'owner-group-dacl'
        RestoreSiblingPath = $restoreSiblingPath
        DisplacedPath = $displacedPath
    } | ConvertTo-Json | Set-Content -LiteralPath $manifestPath -Encoding utf8
    Write-Output "private-recovery-path=$experimentPath"
    Write-Output 'interrupt-policy=allow the first interrupt to finish recovery; do not interrupt recovery again'

    if (-not (Wait-AllNcmExit -TimeoutSeconds 1)) {
        throw 'An NCM instance started after the private snapshot was created.'
    }

    $observerIdleMilliseconds = (($ObservationSeconds + 30) * 1000).ToString()
    $observer = Start-Process -FilePath $resolvedObserverPath `
        -ArgumentList @('--port', '0', '--max-events', $MaxEvents.ToString(), '--idle-timeout-ms', $observerIdleMilliseconds) `
        -RedirectStandardOutput $observerOutputPath `
        -RedirectStandardError $observerErrorPath `
        -WindowStyle Hidden `
        -PassThru

    $observerPort = $null
    $listenDeadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        if ($observer.HasExited) {
            break
        }
        if (Test-Path -LiteralPath $observerOutputPath) {
            $output = Get-Content -LiteralPath $observerOutputPath -Raw
            if ($output -match 'observer-listening address=127\.0\.0\.1 port=(\d+)') {
                $observerPort = [int]$Matches[1]
                break
            }
        }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $listenDeadline)
    if ($null -eq $observerPort) {
        throw 'The loopback observer did not become ready.'
    }

    $launchParameters = @{
        FilePath = $script:ResolvedNcmPath
        WorkingDirectory = [IO.Path]::GetDirectoryName($script:ResolvedNcmPath)
        PassThru = $true
    }
    if ($RouteMode -eq 'CommandLine') {
        $proxyArgument = "--proxy-server=http://127.0.0.1:$observerPort"
        $launchParameters.ArgumentList = $proxyArgument
    }
    $launchedNcm = Start-Process @launchParameters

    if ($RouteMode -eq 'CommandLine') {
        $startDeadline = [DateTime]::UtcNow.AddSeconds(10)
        $argumentObserved = $false
        do {
            $argumentObserved = Test-TargetCommandLineArgument -ExpectedArgument $proxyArgument
            if ($argumentObserved) {
                break
            }
            Start-Sleep -Milliseconds 200
        } while ([DateTime]::UtcNow -lt $startDeadline)
        if (-not $argumentObserved) {
            throw 'The target NCM process did not expose the requested process-local proxy argument.'
        }
    }

    $observationDeadline = [DateTime]::UtcNow.AddSeconds($ObservationSeconds)
    while ([DateTime]::UtcNow -lt $observationDeadline -and -not $observer.HasExited) {
        Start-Sleep -Milliseconds 250
    }

    if (-not (Request-NormalNcmExit -Process $launchedNcm -TimeoutSeconds 20)) {
        if (-not $ForceCloseAfterTimeout -or -not (Stop-LaunchedNcmTree -Process $launchedNcm)) {
            throw 'The launched NCM tree remained active after the bounded normal-close period; no private-state overwrite was attempted.'
        }
    }

    if (-not (Stop-OwnedObserver -Process $observer)) {
        throw 'The owned observer did not stop within five seconds.'
    }
    $eventLines = @(if (Test-Path -LiteralPath $observerOutputPath) {
            Get-Content -LiteralPath $observerOutputPath | Where-Object { $_ -like 'proxy-event *' }
        })
    $observerEventCount = $eventLines.Count
    $routingObserved = $observerEventCount -gt 0
    if (-not $routingObserved) {
        $routeDescription = if ($RouteMode -eq 'CommandLine') {
            'the process-local proxy argument'
        } else {
            'the client UI experiment window'
        }
        throw "NCM started for $routeDescription, but the observer received no request."
    }
} catch {
    $primaryError = $_.Exception
} finally {
    if ($null -ne $launchedNcm -and @(Get-AllNcmProcesses).Count -ne 0) {
        try {
            $closed = Request-NormalNcmExit -Process $launchedNcm -TimeoutSeconds 5
            if (-not $closed -and $ForceCloseAfterTimeout) {
                [void](Stop-LaunchedNcmTree -Process $launchedNcm)
            }
        } catch {
            if ($null -eq $primaryError) {
                $primaryError = $_.Exception
            }
        }
    }

    if ($null -ne $observer) {
        try {
            if (-not (Stop-OwnedObserver -Process $observer)) {
                throw 'The owned observer did not stop within five seconds.'
            }
        } catch {
            if ($null -eq $primaryError) {
                $primaryError = $_.Exception
            }
        }
    }

    if ($null -ne $proxyStateBefore) {
        try {
            $systemProxyUnchanged = (Get-PrivateProxyState) -ceq $proxyStateBefore
        } catch {
            $systemProxyUnchanged = $null
            if ($null -eq $primaryError) {
                $primaryError = $_.Exception
            }
        }
    }

    if ($backupCreated) {
        try {
            if (-not (Wait-AllNcmExit -TimeoutSeconds 2)) {
                throw 'An NCM process is still active; shared private-state recovery was not attempted.'
            }

            $localDataExists = Test-Path -LiteralPath $localDataPath -PathType Leaf
            $needsRestore = -not $localDataExists
            if ($localDataExists) {
                $currentBytes = [IO.File]::ReadAllBytes($localDataPath)
                $currentItem = Get-Item -LiteralPath $localDataPath
                $currentAcl = Get-Acl -LiteralPath $localDataPath
                $needsRestore = -not (Test-ByteArrayEqual -Left $originalBytes -Right $currentBytes) -or
                    $currentItem.CreationTimeUtc -ne $originalCreationTimeUtc -or
                    $currentItem.LastWriteTimeUtc -ne $originalLastWriteTimeUtc -or
                    $currentItem.Attributes -ne $originalAttributes -or
                    $currentAcl.Sddl -cne $originalAcl.Sddl
            }

            if ($needsRestore) {
                [IO.File]::WriteAllBytes($restoreSiblingPath, $originalBytes)
                $flush = [IO.File]::Open($restoreSiblingPath, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
                try {
                    $flush.Flush($true)
                } finally {
                    $flush.Dispose()
                }

                if (-not (Wait-AllNcmExit -TimeoutSeconds 1)) {
                    throw 'An NCM process appeared before atomic recovery.'
                }

                if ($localDataExists) {
                    [IO.File]::Replace($restoreSiblingPath, $localDataPath, $displacedPath, $true)
                } else {
                    [IO.File]::Move($restoreSiblingPath, $localDataPath)
                }
                Set-Acl -LiteralPath $localDataPath -AclObject $originalAcl
                [IO.File]::SetCreationTimeUtc($localDataPath, $originalCreationTimeUtc)
                [IO.File]::SetLastWriteTimeUtc($localDataPath, $originalLastWriteTimeUtc)
                [IO.File]::SetAttributes($localDataPath, $originalAttributes)
            }

            if (-not (Wait-AllNcmExit -TimeoutSeconds 1)) {
                throw 'An NCM process appeared during recovery verification.'
            }
            $restoredBytes = [IO.File]::ReadAllBytes($localDataPath)
            $restoredItem = Get-Item -LiteralPath $localDataPath
            $restoredAcl = Get-Acl -LiteralPath $localDataPath
            $stateRestored = (Test-ByteArrayEqual -Left $originalBytes -Right $restoredBytes) -and
                $restoredItem.CreationTimeUtc -eq $originalCreationTimeUtc -and
                $restoredItem.LastWriteTimeUtc -eq $originalLastWriteTimeUtc -and
                $restoredItem.Attributes -eq $originalAttributes -and
                $restoredAcl.Sddl -ceq $originalAcl.Sddl
            if (-not $stateRestored) {
                throw 'localdata recovery verification failed.'
            }
        } catch {
            $recoveryError = $_.Exception
        }
    }

    try {
        if (Test-Path -LiteralPath $observerOutputPath) {
            Get-Content -LiteralPath $observerOutputPath
        }
        if (Test-Path -LiteralPath $observerErrorPath) {
            $observerError = Get-Content -LiteralPath $observerErrorPath -Raw
            if (-not [string]::IsNullOrWhiteSpace($observerError)) {
                Write-Warning "Observer error: $observerError"
            }
        }
    } catch {
        Write-Warning "Unable to read observer diagnostics: $($_.Exception.Message)"
    }

    $proxyResult = if ($null -eq $systemProxyUnchanged) { 'unknown' } else { $systemProxyUnchanged.ToString().ToLowerInvariant() }
    Write-Output "experiment-process-path=$($RouteMode.ToLowerInvariant())"
    Write-Output "observer-events=$observerEventCount"
    Write-Output "routing-observed=$($routingObserved.ToString().ToLowerInvariant())"
    Write-Output "localdata-restored=$($stateRestored.ToString().ToLowerInvariant())"
    Write-Output "system-proxy-unchanged=$proxyResult"

    $retainRecovery = $backupCreated -and -not $stateRestored
    if ($retainRecovery) {
        Write-Warning "Private recovery data retained at: $experimentPath"
    } else {
        $cleanupPaths = @(
            $backupPath, $manifestPath, $observerOutputPath, $observerErrorPath,
            $restoreSiblingPath, $displacedPath)
        foreach ($path in $cleanupPaths) {
            Remove-Item -LiteralPath $path -ErrorAction SilentlyContinue
        }
        Remove-Item -LiteralPath $experimentPath -ErrorAction SilentlyContinue
        $remainingPaths = @($cleanupPaths | Where-Object { Test-Path -LiteralPath $_ })
        if (Test-Path -LiteralPath $experimentPath) {
            $remainingPaths += $experimentPath
        }
        if ($remainingPaths.Count -ne 0) {
            $cleanupError = 'Private experiment cleanup left: ' + ($remainingPaths -join ', ')
            Write-Warning $cleanupError
        }
    }
}

$failures = @()
if ($null -ne $primaryError) {
    $failures += "experiment: $($primaryError.Message)"
}
if ($null -ne $recoveryError) {
    $failures += "recovery: $($recoveryError.Message)"
}
if ($null -ne $cleanupError) {
    $failures += "cleanup: $cleanupError"
}
if ($systemProxyUnchanged -ne $true) {
    $failures += 'system proxy equality was not verified'
}
if (-not $routingObserved) {
    $failures += 'no NCM proxy request was observed'
}
if (-not $stateRestored) {
    $failures += 'localdata restoration was not verified'
}
if ($failures.Count -ne 0) {
    throw ($failures -join '; ')
}
