[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$NcmPath,

    [Parameter(Mandatory)]
    [string]$UnmPath,

    [ValidateRange(1, 65535)]
    [int]$HttpPort = 3412,

    [ValidateRange(1, 65535)]
    [int]$HttpsPort = 3413,

    [ValidateRange(1, 60)]
    [int]$SessionMinutes = 20,

    [string[]]$Sources = @()
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

function Get-SystemProxyState {
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

function Get-NcmProcesses {
    return @(Get-Process -Name cloudmusic, cloudmusic_reporter -ErrorAction SilentlyContinue)
}

function Wait-NcmExit {
    param([int]$TimeoutSeconds)

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $emptySnapshots = 0
    do {
        if (@(Get-NcmProcesses).Count -eq 0) {
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

function Stop-ExactProcess {
    param(
        [Diagnostics.Process]$Process,
        [string]$ExpectedPath,
        [DateTime]$ExpectedStartTime
    )

    if ($null -eq $Process) {
        return
    }
    try {
        $Process.Refresh()
        if ($Process.HasExited) {
            return
        }
        if ([IO.Path]::GetFullPath($Process.Path) -ine $ExpectedPath -or
            $Process.StartTime.ToUniversalTime() -ne $ExpectedStartTime) {
            throw "Process identity changed for PID $($Process.Id); refusing termination."
        }
        $Process.Kill()
        if (-not $Process.WaitForExit(5000)) {
            throw "PID $($Process.Id) did not exit within five seconds."
        }
    } catch [ArgumentException] {
        # The exact process exited between identity checks.
    }
}

function Stop-LaunchedNcmTree {
    param(
        [uint32]$RootId,
        [DateTime]$RootCreationDate
    )

    $instances = @(Get-CimInstance Win32_Process)
    $depthById = @{ [uint32]$RootId = 0 }
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

    $allowedPaths = @($script:ResolvedNcmPath, $script:ReporterPath)
    $tree = @($instances | Where-Object { $depthById.ContainsKey([uint32]$_.ProcessId) })
    $root = @($tree | Where-Object {
            [uint32]$_.ProcessId -eq $RootId -and $_.CreationDate -eq $RootCreationDate
        })
    if ($root.Count -ne 1) {
        throw 'The launched NCM root identity could not be re-established.'
    }
    foreach ($instance in $tree) {
        if ([string]::IsNullOrWhiteSpace($instance.ExecutablePath)) {
            throw 'A launched NCM descendant has no inspectable path.'
        }
        $candidatePath = [IO.Path]::GetFullPath($instance.ExecutablePath)
        if (@($allowedPaths | Where-Object { $_ -ieq $candidatePath }).Count -ne 1) {
            throw "Refusing to terminate an unexpected descendant: $candidatePath"
        }
    }

    foreach ($instance in @($tree | Sort-Object { -$depthById[[uint32]$_.ProcessId] })) {
        $current = Get-CimInstance Win32_Process -Filter "ProcessId = $($instance.ProcessId)" -ErrorAction SilentlyContinue
        if ($null -eq $current) {
            continue
        }
        if ($current.CreationDate -ne $instance.CreationDate -or
            [IO.Path]::GetFullPath($current.ExecutablePath) -ine [IO.Path]::GetFullPath($instance.ExecutablePath)) {
            throw "Process identity changed for PID $($instance.ProcessId)."
        }
        [Diagnostics.Process]::GetProcessById([int]$instance.ProcessId).Kill()
    }
}

$script:ResolvedNcmPath = (Resolve-Path -LiteralPath $NcmPath).Path
$resolvedUnmPath = (Resolve-Path -LiteralPath $UnmPath).Path
$script:ReporterPath = Join-Path ([IO.Path]::GetDirectoryName($script:ResolvedNcmPath)) 'cloudmusic_reporter.exe'
$localDataPath = Join-Path $env:LOCALAPPDATA 'Netease\CloudMusic\localdata'

if ([Diagnostics.FileVersionInfo]::GetVersionInfo($script:ResolvedNcmPath).FileVersion -ne '2.9.7.199711') {
    throw 'The target must be NCM 2.9.7.199711.'
}
if (-not [Environment]::Is64BitOperatingSystem) {
    throw 'The selected UNM artifact requires 64-bit Windows.'
}
if ($HttpPort -eq $HttpsPort) {
    throw 'HTTP and HTTPS ports must be distinct.'
}
if (@($Sources | Where-Object { $_ -notmatch '^[a-z0-9_-]+$' }).Count -ne 0) {
    throw 'Source names must contain only lowercase letters, digits, underscores, or hyphens.'
}
if (@(Get-NcmProcesses).Count -ne 0) {
    throw 'Every NCM instance must be closed before the experiment starts.'
}
if (-not (Test-Path -LiteralPath $localDataPath -PathType Leaf)) {
    throw 'NCM localdata was not found; refusing to run without a recovery source.'
}
$principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if ($principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run this user-scoped experiment from a non-elevated PowerShell session.'
}
$occupied = @(Get-NetTCPConnection -State Listen -ErrorAction SilentlyContinue |
        Where-Object { $_.LocalPort -in @($HttpPort, $HttpsPort) })
if ($occupied.Count -ne 0) {
    throw "Port $HttpPort or $HttpsPort is already occupied."
}

$tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd([IO.Path]::DirectorySeparatorChar)
$experimentId = [Guid]::NewGuid().ToString('N')
$experimentPath = [IO.Path]::GetFullPath((Join-Path $tempRoot ('ncm-proxy-experiment-' + $experimentId)))
$backupPath = Join-Path $experimentPath 'localdata.backup'
$manifestPath = Join-Path $experimentPath 'recovery.json'
$unmOutputPath = Join-Path $experimentPath 'unm.stdout'
$unmErrorPath = Join-Path $experimentPath 'unm.stderr'
$restorePath = Join-Path ([IO.Path]::GetDirectoryName($localDataPath)) ('localdata.codex-restore-' + $experimentId)
$displacedPath = Join-Path ([IO.Path]::GetDirectoryName($localDataPath)) ('localdata.codex-displaced-' + $experimentId)

$unm = $null
$ncm = $null
$ncmRootCreationDate = $null
$snapshotCreated = $false
$restored = $false
$systemProxyUnchanged = $false
$primaryError = $null
$recoveryError = $null

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

    if (-not (Wait-NcmExit -TimeoutSeconds 1)) {
        throw 'An NCM process appeared while the recovery snapshot was prepared.'
    }
    $originalBytes = [IO.File]::ReadAllBytes($localDataPath)
    $originalItem = Get-Item -LiteralPath $localDataPath
    $originalAcl = Get-Acl -LiteralPath $localDataPath
    $originalCreationTimeUtc = $originalItem.CreationTimeUtc
    $originalLastWriteTimeUtc = $originalItem.LastWriteTimeUtc
    $originalAttributes = $originalItem.Attributes
    $systemProxyBefore = Get-SystemProxyState
    [IO.File]::WriteAllBytes($backupPath, $originalBytes)
    if (-not (Test-ByteArrayEqual -Left $originalBytes -Right ([IO.File]::ReadAllBytes($backupPath)))) {
        throw 'The private recovery copy does not match localdata.'
    }
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
        RestoreSiblingPath = $restorePath
        DisplacedPath = $displacedPath
    } | ConvertTo-Json | Set-Content -LiteralPath $manifestPath -Encoding utf8
    $snapshotCreated = $true
    Write-Output "private-recovery-path=$experimentPath"

    $arguments = @('-a', '127.0.0.1', '-p', "${HttpPort}:${HttpsPort}", '-s')
    if ($Sources.Count -ne 0) {
        $arguments += @('-o') + $Sources
    }
    $unm = Start-Process -FilePath $resolvedUnmPath -ArgumentList $arguments `
        -WorkingDirectory ([IO.Path]::GetDirectoryName($resolvedUnmPath)) `
        -RedirectStandardOutput $unmOutputPath -RedirectStandardError $unmErrorPath `
        -WindowStyle Hidden -PassThru
    $unmStartTimeUtc = $unm.StartTime.ToUniversalTime()

    $ready = $false
    $readinessDeadline = [DateTime]::UtcNow.AddSeconds(15)
    do {
        Start-Sleep -Milliseconds 250
        $unm.Refresh()
        if ($unm.HasExited) {
            break
        }
        $listeners = @(Get-NetTCPConnection -State Listen -ErrorAction SilentlyContinue |
                Where-Object {
                    $_.LocalAddress -eq '127.0.0.1' -and
                    $_.LocalPort -in @($HttpPort, $HttpsPort) -and
                    $_.OwningProcess -eq $unm.Id
                })
        $listenerPorts = @($listeners | Select-Object -ExpandProperty LocalPort | Sort-Object -Unique)
        if ($listenerPorts.Count -eq 2) {
            try {
                $pac = Invoke-WebRequest -Uri "http://127.0.0.1:$HttpPort/proxy.pac" `
                    -TimeoutSec 2 -UseBasicParsing
                $ready = $pac.StatusCode -eq 200 -and $pac.Content.Length -gt 0
            } catch {
                $ready = $false
            }
        }
    } while (-not $ready -and [DateTime]::UtcNow -lt $readinessDeadline)
    if (-not $ready) {
        throw 'UNM did not satisfy listener and PAC readiness.'
    }
    $sourceDescription = if ($Sources.Count -eq 0) { 'upstream-default' } else { $Sources -join ',' }
    Write-Output "unm-ready=true pid=$($unm.Id) http=$HttpPort https=$HttpsPort sources=$sourceDescription"

    $ncm = Start-Process -FilePath $script:ResolvedNcmPath `
        -WorkingDirectory ([IO.Path]::GetDirectoryName($script:ResolvedNcmPath)) -PassThru
    $ncmInstance = Get-CimInstance Win32_Process -Filter "ProcessId = $($ncm.Id)"
    $ncmRootCreationDate = $ncmInstance.CreationDate
    Write-Output "ncm-started=true pid=$($ncm.Id)"
    Write-Output "user-action=set NCM custom HTTP proxy to 127.0.0.1:$HttpPort, test playback, then use the tray menu to exit NCM"

    $deadline = [DateTime]::UtcNow.AddMinutes($SessionMinutes)
    do {
        Start-Sleep -Seconds 1
        $unm.Refresh()
        if ($unm.HasExited) {
            throw "UNM exited unexpectedly with code $($unm.ExitCode)."
        }
        if (@(Get-NcmProcesses).Count -eq 0 -and (Wait-NcmExit -TimeoutSeconds 1)) {
            Write-Output 'ncm-session-ended=true'
            break
        }
    } while ([DateTime]::UtcNow -lt $deadline)

    if (@(Get-NcmProcesses).Count -ne 0) {
        Write-Output 'session-timeout=true; requesting normal NCM close'
        try {
            $ncm.Refresh()
            if (-not $ncm.HasExited -and $ncm.MainWindowHandle -ne 0) {
                [void]$ncm.CloseMainWindow()
            }
        } catch {}
        if (-not (Wait-NcmExit -TimeoutSeconds 20)) {
            Stop-LaunchedNcmTree -RootId ([uint32]$ncm.Id) -RootCreationDate $ncmRootCreationDate
        }
        if (-not (Wait-NcmExit -TimeoutSeconds 10)) {
            throw 'The exact launched NCM tree could not be stopped after timeout.'
        }
    }
} catch {
    $primaryError = $_.Exception
} finally {
    if ($null -ne $ncm -and @(Get-NcmProcesses).Count -ne 0) {
        try {
            $ncm.Refresh()
            if (-not $ncm.HasExited -and $ncm.MainWindowHandle -ne 0) {
                [void]$ncm.CloseMainWindow()
            }
            if (-not (Wait-NcmExit -TimeoutSeconds 10) -and $null -ne $ncmRootCreationDate) {
                Stop-LaunchedNcmTree -RootId ([uint32]$ncm.Id) -RootCreationDate $ncmRootCreationDate
                [void](Wait-NcmExit -TimeoutSeconds 10)
            }
        } catch {
            if ($null -eq $primaryError) {
                $primaryError = $_.Exception
            }
        }
    }

    if ($null -ne $unm) {
        try {
            Stop-ExactProcess -Process $unm -ExpectedPath $resolvedUnmPath -ExpectedStartTime $unmStartTimeUtc
        } catch {
            if ($null -eq $primaryError) {
                $primaryError = $_.Exception
            }
        }
    }

    if ($snapshotCreated) {
        try {
            if (-not (Wait-NcmExit -TimeoutSeconds 2)) {
                throw 'NCM is still active; localdata recovery was not attempted.'
            }
            $currentBytes = if (Test-Path -LiteralPath $localDataPath -PathType Leaf) {
                [IO.File]::ReadAllBytes($localDataPath)
            } else {
                $null
            }
            $needsRestore = $null -eq $currentBytes -or
                -not (Test-ByteArrayEqual -Left $originalBytes -Right $currentBytes)
            if ($needsRestore) {
                if ((Test-Path -LiteralPath $restorePath) -or (Test-Path -LiteralPath $displacedPath)) {
                    throw 'A recovery sibling already exists; refusing overwrite.'
                }
                [IO.File]::WriteAllBytes($restorePath, $originalBytes)
                $flush = [IO.File]::Open($restorePath, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
                try {
                    $flush.Flush($true)
                } finally {
                    $flush.Dispose()
                }
                if (Test-Path -LiteralPath $localDataPath -PathType Leaf) {
                    [IO.File]::Replace($restorePath, $localDataPath, $displacedPath, $true)
                } else {
                    [IO.File]::Move($restorePath, $localDataPath)
                }
            }
            Set-Acl -LiteralPath $localDataPath -AclObject $originalAcl
            [IO.File]::SetCreationTimeUtc($localDataPath, $originalCreationTimeUtc)
            [IO.File]::SetLastWriteTimeUtc($localDataPath, $originalLastWriteTimeUtc)
            [IO.File]::SetAttributes($localDataPath, $originalAttributes)
            $restoredItem = Get-Item -LiteralPath $localDataPath
            $restored = (Test-ByteArrayEqual -Left $originalBytes -Right ([IO.File]::ReadAllBytes($localDataPath))) -and
                $restoredItem.CreationTimeUtc -eq $originalCreationTimeUtc -and
                $restoredItem.LastWriteTimeUtc -eq $originalLastWriteTimeUtc -and
                $restoredItem.Attributes -eq $originalAttributes -and
                (Get-Acl -LiteralPath $localDataPath).Sddl -ceq $originalAcl.Sddl
            if (-not $restored) {
                throw 'localdata recovery verification failed.'
            }
            $systemProxyUnchanged = (Get-SystemProxyState) -ceq $systemProxyBefore
            if (-not $systemProxyUnchanged) {
                throw 'System proxy state changed during the experiment.'
            }
        } catch {
            $recoveryError = $_.Exception
        }
    }

    Write-Output "localdata-restored=$($restored.ToString().ToLowerInvariant())"
    Write-Output "system-proxy-unchanged=$($systemProxyUnchanged.ToString().ToLowerInvariant())"
    if ($null -ne $primaryError -or $null -ne $recoveryError) {
        Write-Warning "Private recovery material retained at: $experimentPath"
    } else {
        $resolvedExperimentPath = [IO.Path]::GetFullPath($experimentPath)
        if ([IO.Path]::GetDirectoryName($resolvedExperimentPath) -ine $tempRoot -or
            [IO.Path]::GetFileName($resolvedExperimentPath) -notmatch '^ncm-proxy-experiment-[0-9a-f]{32}$') {
            throw 'Experiment cleanup path failed validation.'
        }
        foreach ($path in @($backupPath, $manifestPath, $unmOutputPath, $unmErrorPath, $restorePath, $displacedPath)) {
            Remove-Item -LiteralPath $path -ErrorAction SilentlyContinue
        }
        Remove-Item -LiteralPath $resolvedExperimentPath -ErrorAction SilentlyContinue
    }
}

$failures = @()
if ($null -ne $primaryError) {
    $failures += "experiment: $($primaryError.Message)"
}
if ($null -ne $recoveryError) {
    $failures += "recovery: $($recoveryError.Message)"
}
if (-not $restored) {
    $failures += 'localdata restoration was not verified'
}
if (-not $systemProxyUnchanged) {
    $failures += 'system proxy equality was not verified'
}
if ($failures.Count -ne 0) {
    throw ($failures -join '; ')
}
