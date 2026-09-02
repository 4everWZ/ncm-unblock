# Shared harness for experiments that start the exact NCM client in isolation.
#
# Every such experiment needs the same safety properties: an exact-version
# signed target, no pre-existing client process, a private copy of the
# installation, a recorded fingerprint of the real client-local state, reclaim
# limited to processes whose image lives under the private root, and verified
# restoration in the same run. They live here so the repository has one copy of
# that path instead of one per experiment.
#
# Dot-source this file; it defines functions and starts nothing.

$NcmExpectedVersion = '2.9.7.199711'

function Get-OrdinaryFile {
    param([Parameter(Mandatory)][string]$LiteralPath, [Parameter(Mandatory)][string]$Description)

    $resolved = [System.IO.Path]::GetFullPath($LiteralPath)
    $item = Get-Item -LiteralPath $resolved -Force
    if ($item.PSIsContainer) {
        throw "$Description is not a file: $resolved"
    }
    if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "$Description is a reparse point: $resolved"
    }
    return $item
}

function Set-PrivateDirectoryAcl {
    param([Parameter(Mandatory)][System.IO.DirectoryInfo]$Directory)

    $currentSid = [System.Security.Principal.WindowsIdentity]::GetCurrent().User
    $systemSid = [System.Security.Principal.SecurityIdentifier]::new(
        [System.Security.Principal.WellKnownSidType]::LocalSystemSid, $null)
    $inheritance = [System.Security.AccessControl.InheritanceFlags]'ContainerInherit, ObjectInherit'
    $propagation = [System.Security.AccessControl.PropagationFlags]::None
    $allow = [System.Security.AccessControl.AccessControlType]::Allow
    $acl = [System.Security.AccessControl.DirectorySecurity]::new()
    $acl.SetOwner($currentSid)
    $acl.SetAccessRuleProtection($true, $false)
    $acl.AddAccessRule([System.Security.AccessControl.FileSystemAccessRule]::new(
        $currentSid, [System.Security.AccessControl.FileSystemRights]::FullControl,
        $inheritance, $propagation, $allow))
    $acl.AddAccessRule([System.Security.AccessControl.FileSystemAccessRule]::new(
        $systemSid, [System.Security.AccessControl.FileSystemRights]::FullControl,
        $inheritance, $propagation, $allow))
    Set-Acl -LiteralPath $Directory.FullName -AclObject $acl
}

function Get-ClientLocalStatePath {
    return (Join-Path $env:LOCALAPPDATA 'Netease\CloudMusic\localdata')
}

function Get-ClientStateFingerprint {
    param([Parameter(Mandatory)][string]$LiteralPath)

    if (-not (Test-Path -LiteralPath $LiteralPath -PathType Leaf)) {
        return [pscustomobject]@{ Exists = $false; Length = 0; Hash = ''; Written = [DateTime]::MinValue }
    }
    $item = Get-Item -LiteralPath $LiteralPath -Force
    return [pscustomobject]@{
        Exists  = $true
        Length  = $item.Length
        Hash    = (Get-FileHash -LiteralPath $LiteralPath -Algorithm SHA256).Hash
        Written = $item.LastWriteTimeUtc
    }
}

function Get-IsolatedProcesses {
    param([Parameter(Mandatory)][string]$Root)

    $result = @()
    foreach ($instance in @(Get-CimInstance Win32_Process -ErrorAction SilentlyContinue)) {
        if ([string]::IsNullOrWhiteSpace($instance.ExecutablePath)) {
            continue
        }
        $path = try { [System.IO.Path]::GetFullPath($instance.ExecutablePath) } catch { continue }
        if ($path.StartsWith($Root, [StringComparison]::OrdinalIgnoreCase)) {
            $result += $instance
        }
    }
    return $result
}

function Stop-IsolatedTree {
    param([Parameter(Mandatory)][string]$Root, [Parameter(Mandatory)][int]$TimeoutSeconds)

    foreach ($instance in @(Get-IsolatedProcesses -Root $Root)) {
        try {
            [Diagnostics.Process]::GetProcessById([int]$instance.ProcessId).Kill()
        } catch {
            # The process can exit on its own between the snapshot and the stop.
        }
    }
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        if (@(Get-IsolatedProcesses -Root $Root).Count -eq 0) {
            return $true
        }
        Start-Sleep -Milliseconds 250
    } while ([DateTime]::UtcNow -lt $deadline)
    return (@(Get-IsolatedProcesses -Root $Root).Count -eq 0)
}

function Assert-ExactClientTarget {
    param(
        [Parameter(Mandatory)][string]$NcmPath,
        [string]$ExpectedVersion = $NcmExpectedVersion
    )

    $target = Get-OrdinaryFile -LiteralPath $NcmPath -Description 'target executable'
    if ($target.VersionInfo.FileVersion -ne $ExpectedVersion) {
        throw "target executable is not $ExpectedVersion but $($target.VersionInfo.FileVersion)."
    }
    $signature = Get-AuthenticodeSignature -LiteralPath $target.FullName
    if ($signature.Status -ne [System.Management.Automation.SignatureStatus]::Valid) {
        throw "target executable does not have a valid Authenticode signature: $($signature.Status)"
    }
    return $target
}

function Assert-NoClientRunning {
    $running = @(Get-Process -Name cloudmusic, cloudmusic_reporter -ErrorAction SilentlyContinue)
    if ($running.Count -ne 0) {
        throw "Close NetEase Cloud Music first: $($running.Count) client process(es) are running, so this run could not own its tree unambiguously."
    }
}

function New-ExperimentRoot {
    param([Parameter(Mandatory)][string]$Prefix)

    $path = Join-Path ([System.IO.Path]::GetTempPath()) ($Prefix + [System.Guid]::NewGuid().ToString('N'))
    $root = New-Item -ItemType Directory -Path $path
    Set-PrivateDirectoryAcl -Directory $root
    return $root
}

function New-IsolatedClientTree {
    param(
        [Parameter(Mandatory)][System.IO.DirectoryInfo]$ExperimentRoot,
        [Parameter(Mandatory)][System.IO.FileInfo]$Target
    )

    $app = Join-Path $ExperimentRoot.FullName 'app'
    $profileRoot = Join-Path $ExperimentRoot.FullName 'profile'
    Copy-Item -LiteralPath $Target.Directory.FullName -Destination $app -Recurse -Force
    [void](New-Item -ItemType Directory -Path $profileRoot)

    $isolatedTarget = Join-Path $app $Target.Name
    if (-not (Test-Path -LiteralPath $isolatedTarget -PathType Leaf)) {
        throw 'the isolated copy does not contain the target executable.'
    }
    return [pscustomobject]@{
        App     = $app
        Profile = $profileRoot
        Target  = $isolatedTarget
    }
}

function Add-IsolatedWinmmProxy {
    param(
        [Parameter(Mandatory)][string]$App,
        [Parameter(Mandatory)][System.IO.FileInfo]$Proxy
    )

    $isolatedProxy = Join-Path $App 'winmm.dll'
    if (Test-Path -LiteralPath $isolatedProxy) {
        throw 'the installation already contains a winmm.dll; this experiment does not replace one.'
    }
    Copy-Item -LiteralPath $Proxy.FullName -Destination $isolatedProxy
    return $isolatedProxy
}

function Restore-ClientState {
    param(
        [Parameter(Mandatory)][string]$StatePath,
        [Parameter(Mandatory)][psobject]$Before,
        [Parameter(Mandatory)][string]$BackupPath
    )

    $after = Get-ClientStateFingerprint -LiteralPath $StatePath
    $unchanged = $Before.Exists -eq $after.Exists -and $Before.Length -eq $after.Length -and
        $Before.Hash -eq $after.Hash -and $Before.Written -eq $after.Written
    Write-Output "installed-client-state-unchanged: $unchanged"
    if ($unchanged) {
        return
    }

    Write-Warning 'The real localdata changed, so %LOCALAPPDATA% redirection did not contain this client.'
    if ($Before.Exists) {
        Copy-Item -LiteralPath $BackupPath -Destination $StatePath -Force
        (Get-Item -LiteralPath $StatePath -Force).LastWriteTimeUtc = $Before.Written
        $restored = Get-ClientStateFingerprint -LiteralPath $StatePath
        if ($restored.Hash -ne $Before.Hash) {
            throw "unable to restore $StatePath; the private backup is at $BackupPath and the experiment directory was preserved."
        }
        Write-Output 'installed-client-state-restored: True'
    } elseif ($after.Exists) {
        Remove-Item -LiteralPath $StatePath -Force
        Write-Output 'installed-client-state-restored: True (removed a file this run created)'
    }
}

function Remove-ExperimentRoot {
    param([Parameter(Mandatory)][System.IO.DirectoryInfo]$ExperimentRoot)

    if (@(Get-IsolatedProcesses -Root $ExperimentRoot.FullName).Count -ne 0) {
        return
    }
    # Handles can linger briefly after the tree exits.
    for ($attempt = 0; $attempt -lt 5 -and (Test-Path -LiteralPath $ExperimentRoot.FullName); ++$attempt) {
        Remove-Item -LiteralPath $ExperimentRoot.FullName -Recurse -Force -ErrorAction SilentlyContinue
        if (Test-Path -LiteralPath $ExperimentRoot.FullName) { Start-Sleep -Milliseconds 500 }
    }
    Write-Output "experiment-directory-removed: $(-not (Test-Path -LiteralPath $ExperimentRoot.FullName))"
}
