[CmdletBinding()]
param(
    [string]$NcmPath = 'D:\Program Files (x86)\Netease\CloudMusic\cloudmusic.exe',

    [string]$ProxyPath,

    [ValidateRange(5, 900)]
    [int]$ObservationSeconds = 30,

    # Playback-attribution experiment. When set, the staged proxy is expected to
    # be the census build: every isolated process writes a module-load timeline
    # into this directory, and the run holds for the whole observation window so
    # an operator can sign in and play a track while the timeline records.
    # Reports are written outside the private run directory so they survive its
    # cleanup, and they carry only allowlisted module names.
    [string]$CensusOutputDirectory,

    # Investigation-only output from the production bootstrap. Each process
    # writes only its pid plus fixed hook/registration classifications; no
    # client content, URL, request, or user state is recorded.
    [string]$InjectionOutputDirectory,

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

. (Join-Path $PSScriptRoot 'lib\isolated-client.ps1')

if ([string]::IsNullOrWhiteSpace($ProxyPath)) {
    $ProxyPath = Join-Path $PSScriptRoot '..\build\win32-release\src\winmm_proxy\winmm.dll'
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

$target = Assert-ExactClientTarget -NcmPath $NcmPath
$proxy = Get-OrdinaryFile -LiteralPath $ProxyPath -Description 'winmm proxy'
Assert-NoClientRunning

$realState = Get-ClientLocalStatePath
$before = Get-ClientStateFingerprint -LiteralPath $realState

$root = New-ExperimentRoot -Prefix 'ncm-unblock-297-winmm-proxy-'
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
    $tree = New-IsolatedClientTree -ExperimentRoot $root -Target $target
    $app = $tree.App
    $isolatedTarget = $tree.Target
    $isolatedProxy = Add-IsolatedWinmmProxy -App $app -Proxy $proxy
    Write-Output "isolated-target: $isolatedTarget"

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $isolatedTarget
    $startInfo.WorkingDirectory = $app
    $startInfo.UseShellExecute = $false
    $startInfo.EnvironmentVariables['LOCALAPPDATA'] = $tree.Profile
    if (-not [string]::IsNullOrWhiteSpace($FixtureBackend)) {
        $startInfo.EnvironmentVariables['NCM_WINMM_FIXTURE_BACKEND'] = $FixtureBackend
        Write-Output "fixture-backend: $FixtureBackend"
    }
    $censusEnabled = -not [string]::IsNullOrWhiteSpace($CensusOutputDirectory)
    if ($censusEnabled) {
        $censusRoot = (New-Item -ItemType Directory -Force -Path $CensusOutputDirectory).FullName
        $startInfo.EnvironmentVariables['NCM_CENSUS_REPORT_DIR'] = $censusRoot
        $startInfo.EnvironmentVariables['NCM_CENSUS_WINDOW_MS'] = [string]($ObservationSeconds * 1000)
        Write-Output "census-output: $censusRoot"
        Write-Output "census-window-seconds: $ObservationSeconds"
        Write-Output 'census-action: sign in and play one normal track, then one greyed-out track.'
    }
    $injectionEnabled = -not [string]::IsNullOrWhiteSpace($InjectionOutputDirectory)
    if ($injectionEnabled) {
        $injectionRoot = (New-Item -ItemType Directory -Force -Path $InjectionOutputDirectory).FullName
        $startInfo.EnvironmentVariables['NCM_INJECTION_REPORT_DIR'] = $injectionRoot
        Write-Output "injection-output: $injectionRoot"
    }
    $launched = [System.Diagnostics.Process]::Start($startInfo)

    $deadline = [DateTime]::UtcNow.AddSeconds($ObservationSeconds)
    $observedProxy = $false
    $observedBackend = $false
    $observedWindow = $false
    $observedRegistration = $false
    $observedMarker = $false
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
        if ($injectionEnabled) {
            $reports = @(Get-ChildItem -LiteralPath $injectionRoot `
                    -Filter 'injection-*.txt' -File -ErrorAction SilentlyContinue)
            $observedRegistration = @($reports |
                Select-String -SimpleMatch 'registration=succeeded').Count -ne 0
            $observedMarker = @($reports | ForEach-Object {
                    if ((Get-Content -LiteralPath $_.FullName -Raw) -match 'marker=(\d+)' -and
                        [int]$Matches[1] -gt 0) {
                        $_
                    }
                }).Count -ne 0
        }
    } while ([DateTime]::UtcNow -lt $deadline -and
             ($censusEnabled -or
              ($injectionEnabled -and (-not $observedRegistration -or -not $observedMarker)) -or
              -not ($observedProxy -and $observedBackend -and $observedWindow)))

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

    if ($censusEnabled) {
        # The census threads flush at their own deadline, which is the same as
        # this loop's, so give them a bounded moment to write the final report.
        Start-Sleep -Seconds 3
        $reports = @(Get-ChildItem -LiteralPath $censusRoot -Filter 'census-*.txt' -File -ErrorAction SilentlyContinue)
        Write-Output "census-reports: $($reports.Count)"
        foreach ($report in $reports) {
            $lines = @(Get-Content -LiteralPath $report.FullName)
            $totals = @($lines | Where-Object { $_ -like 'totals *' })
            $stacks = @($lines |
                Where-Object { $_ -like 'event * load *' } |
                ForEach-Object { ($_ -split ' ')[3] } |
                Sort-Object -Unique)
            Write-Output "  $($report.Name): $($totals -join '') stacks=[$($stacks -join ',')]"
        }
        if ($reports.Count -eq 0) {
            Write-Output 'census-warning: no report was written, so the census proxy did not run.'
        }
    }

    if ($injectionEnabled) {
        $injectionReports = @(Get-ChildItem -LiteralPath $injectionRoot `
            -Filter 'injection-*.txt' -File -ErrorAction SilentlyContinue)
        Write-Output "injection-reports: $($injectionReports.Count)"
        foreach ($report in $injectionReports) {
            $classification = (Get-Content -LiteralPath $report.FullName -Raw).Trim()
            Write-Output "  $($report.Name): $classification"
        }
        Write-Output "extension-registration-observed: $observedRegistration"
        Write-Output "native-marker-observed: $observedMarker"
    }

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

    Restore-ClientState -StatePath $realState -Before $before -BackupPath $backup

    $redirected = Join-Path $root.FullName 'profile\Netease\CloudMusic'
    Write-Output "redirected-state-created: $(Test-Path -LiteralPath $redirected)"
    Write-Output 'not-isolated: HKCU registry writes under Software\Netease are outside this experiment boundary'
    Remove-ExperimentRoot -ExperimentRoot $root
}
