[CmdletBinding()]
param(
    [string]$NcmPath = 'D:\Program Files (x86)\Netease\CloudMusic\cloudmusic.exe',

    [string]$ProxyPath,

    [ValidateRange(5, 300)]
    [int]$ObservationSeconds = 30,

    # Positive control. When set, the staged proxy is expected to be the test
    # fixture, which resolves its backend from this path. Pointing it at a file
    # that does not exist turns "did NCM actually call the proxy?" into an
    # observable exit code, with no module enumeration involved.
    [string]$FixtureBackend,

    # The client minimizes to the tray instead of exiting, so the isolated tree
    # normally needs a bounded forced close. Only processes whose image lives
    # under this run's private directory are ever terminated.
    [bool]$ForceCloseAfterTimeout = $true
)

# Starts an isolated copy of the exact NCM root with the full-parity WinMM proxy
# beside it, and observes whether the client reaches its UI while the proxy
# forwards to the system module.
#
# The installed client is never modified: the tree is copied to a private
# directory, `%LOCALAPPDATA%` is redirected into that directory, and the real
# client-local state is verified unchanged before cleanup.

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$expectedVersion = '2.9.7.199711'

if ([string]::IsNullOrWhiteSpace($ProxyPath)) {
    $ProxyPath = Join-Path $PSScriptRoot '..\build\win32-release\src\winmm_proxy\winmm.dll'
}

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

function Get-WinmmModules {
    param([Parameter(Mandatory)][Diagnostics.Process]$Process)

    try {
        $Process.Refresh()
        return [pscustomobject]@{
            Ok      = $true
            Error   = ''
            Modules = @($Process.Modules |
                Where-Object { $_.ModuleName -like 'winmm*' } |
                ForEach-Object { $_.FileName })
            Total   = @($Process.Modules).Count
        }
    } catch {
        return [pscustomobject]@{ Ok = $false; Error = $_.Exception.Message; Modules = @(); Total = 0 }
    }
}

$target = Get-OrdinaryFile -LiteralPath $NcmPath -Description 'target executable'
if ($target.VersionInfo.FileVersion -ne $expectedVersion) {
    throw "target executable is not $expectedVersion but $($target.VersionInfo.FileVersion)."
}
$signature = Get-AuthenticodeSignature -LiteralPath $target.FullName
if ($signature.Status -ne [System.Management.Automation.SignatureStatus]::Valid) {
    throw "target executable does not have a valid Authenticode signature: $($signature.Status)"
}
$proxy = Get-OrdinaryFile -LiteralPath $ProxyPath -Description 'winmm proxy'

$running = @(Get-Process -Name cloudmusic, cloudmusic_reporter -ErrorAction SilentlyContinue)
if ($running.Count -ne 0) {
    throw "Close NetEase Cloud Music first: $($running.Count) client process(es) are running, so this run could not own its tree unambiguously."
}

$installRoot = $target.Directory
$realState = Join-Path $env:LOCALAPPDATA 'Netease\CloudMusic\localdata'
$before = Get-ClientStateFingerprint -LiteralPath $realState

$experiment = Join-Path ([System.IO.Path]::GetTempPath()) ("ncm-unblock-297-winmm-proxy-" + [System.Guid]::NewGuid().ToString('N'))
$root = (New-Item -ItemType Directory -Path $experiment)
Set-PrivateDirectoryAcl -Directory $root
Write-Output "private-experiment-directory: $($root.FullName)"

# Recovery for the case where the redirected profile does not contain the
# client. No NCM process runs before or after this experiment, so the isolated
# copy is the only possible writer.
$backup = Join-Path $root.FullName 'localdata.backup'
if ($before.Exists) {
    Copy-Item -LiteralPath $realState -Destination $backup
}

$launched = $null
try {
    $app = Join-Path $root.FullName 'app'
    $profileRoot = Join-Path $root.FullName 'profile'
    Copy-Item -LiteralPath $installRoot.FullName -Destination $app -Recurse -Force
    [void](New-Item -ItemType Directory -Path $profileRoot)

    $isolatedTarget = Join-Path $app $target.Name
    if (-not (Test-Path -LiteralPath $isolatedTarget -PathType Leaf)) {
        throw 'the isolated copy does not contain the target executable.'
    }
    $isolatedProxy = Join-Path $app 'winmm.dll'
    if (Test-Path -LiteralPath $isolatedProxy) {
        throw 'the installation already contains a winmm.dll; this experiment does not replace one.'
    }
    Copy-Item -LiteralPath $proxy.FullName -Destination $isolatedProxy
    Write-Output "isolated-target: $isolatedTarget"

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $isolatedTarget
    $startInfo.WorkingDirectory = $app
    $startInfo.UseShellExecute = $false
    $startInfo.EnvironmentVariables['LOCALAPPDATA'] = $profileRoot
    if (-not [string]::IsNullOrWhiteSpace($FixtureBackend)) {
        $startInfo.EnvironmentVariables['NCM_WINMM_FIXTURE_BACKEND'] = $FixtureBackend
        Write-Output "fixture-backend: $FixtureBackend"
    }
    $launched = [System.Diagnostics.Process]::Start($startInfo)

    $deadline = [DateTime]::UtcNow.AddSeconds($ObservationSeconds)
    $observedProxy = $false
    $observedBackend = $false
    $observedWindow = $false
    $snapshot = $null
    do {
        Start-Sleep -Milliseconds 500
        if ($launched.HasExited) {
            break
        }
        $snapshot = Get-WinmmModules -Process $launched
        $observedProxy = @($snapshot.Modules | Where-Object { $_ -ieq $isolatedProxy }).Count -eq 1
        $observedBackend = @($snapshot.Modules | Where-Object { $_ -inotlike "$app*" }).Count -ge 1
        try {
            $launched.Refresh()
            if ($launched.MainWindowHandle -ne 0) { $observedWindow = $true }
        } catch {
            # The root can exit while its window state is being read.
        }
    } while (-not ($observedProxy -and $observedBackend -and $observedWindow) -and
             [DateTime]::UtcNow -lt $deadline)

    $exited = $launched.HasExited
    Write-Output "root-exited-early: $exited"
    if ($exited) {
        $code = $launched.ExitCode
        Write-Output ("root-exit-code: {0} (0x{1:X8})" -f $code, $code)
        if ($code -eq 0xE0C40001) {
            Write-Output 'proxy-fail-closed: the proxy was loaded, called, and stopped the client'
        }
    }
    Write-Output "main-window: $observedWindow"
    if ($null -eq $snapshot) {
        Write-Output 'module-enumeration: not attempted'
    } elseif (-not $snapshot.Ok) {
        Write-Output "module-enumeration: failed ($($snapshot.Error))"
    } else {
        Write-Output "module-enumeration: ok ($($snapshot.Total) modules)"
        Write-Output "proxy-loaded: $observedProxy"
        Write-Output "system-backend-loaded: $observedBackend"
        foreach ($module in $snapshot.Modules) {
            Write-Output "  winmm-module: $module"
        }
    }
    Write-Output "isolated-processes: $(@(Get-IsolatedProcesses -Root $root.FullName).Count)"

    if (-not $launched.HasExited) {
        [void]$launched.CloseMainWindow()
        [void]$launched.WaitForExit(5000)
    }
}
finally {
    $remaining = @(Get-IsolatedProcesses -Root $root.FullName).Count
    if ($remaining -ne 0) {
        if (-not $ForceCloseAfterTimeout) {
            throw "isolated processes are still running under $($root.FullName); rerun with -ForceCloseAfterTimeout:`$true or stop them before cleanup."
        }
        Write-Output "forced-close: $remaining isolated process(es) did not exit normally"
        if (-not (Stop-IsolatedTree -Root $root.FullName -TimeoutSeconds 15)) {
            throw "unable to reclaim the isolated tree under $($root.FullName); it was preserved."
        }
    }
    if ($null -ne $launched) { $launched.Dispose() }

    $after = Get-ClientStateFingerprint -LiteralPath $realState
    $unchanged = $before.Exists -eq $after.Exists -and $before.Length -eq $after.Length -and
        $before.Hash -eq $after.Hash -and $before.Written -eq $after.Written
    Write-Output "installed-client-state-unchanged: $unchanged"
    if (-not $unchanged) {
        Write-Warning 'The real localdata changed, so %LOCALAPPDATA% redirection did not contain this client.'
        if ($before.Exists) {
            Copy-Item -LiteralPath $backup -Destination $realState -Force
            (Get-Item -LiteralPath $realState -Force).LastWriteTimeUtc = $before.Written
            $restored = Get-ClientStateFingerprint -LiteralPath $realState
            if ($restored.Hash -ne $before.Hash) {
                throw "unable to restore $realState; the private backup is at $backup and the experiment directory was preserved."
            }
            Write-Output 'installed-client-state-restored: True'
        } elseif ($after.Exists) {
            Remove-Item -LiteralPath $realState -Force
            Write-Output 'installed-client-state-restored: True (removed a file this run created)'
        }
    }

    $redirected = Join-Path $root.FullName 'profile\Netease\CloudMusic'
    Write-Output "redirected-state-created: $(Test-Path -LiteralPath $redirected)"
    Write-Output 'not-isolated: HKCU registry writes under Software\Netease are outside this experiment boundary'

    if (@(Get-IsolatedProcesses -Root $root.FullName).Count -eq 0) {
        # Handles can linger briefly after the tree exits.
        for ($attempt = 0; $attempt -lt 5 -and (Test-Path -LiteralPath $root.FullName); ++$attempt) {
            Remove-Item -LiteralPath $root.FullName -Recurse -Force -ErrorAction SilentlyContinue
            if (Test-Path -LiteralPath $root.FullName) { Start-Sleep -Milliseconds 500 }
        }
        Write-Output "experiment-directory-removed: $(-not (Test-Path -LiteralPath $root.FullName))"
    }
}
