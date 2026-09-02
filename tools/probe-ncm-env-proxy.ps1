[CmdletBinding()]
param(
    [string]$NcmPath = 'D:\Program Files (x86)\Netease\CloudMusic\cloudmusic.exe',

    [string]$ObserverPath,

    # Durable artifacts are written here, outside the private run directory, so
    # they survive its cleanup.
    [string]$OutputDirectory,

    # Ceiling, not a fixed duration. The run ends as soon as the evidence is
    # settled or the operator quits the client.
    [ValidateRange(30, 3600)][int]$ObservationSeconds = 900,

    # Startup and sign-in traffic is not playback traffic. Events inside this
    # opening window are recorded and reported separately, and never satisfy the
    # exit rule; the operator's window opens after it.
    [ValidateRange(0, 600)][int]$StartupSeconds = 60,

    # How many events after an operator track change make the attribution
    # decisive, and how long the observer must then stay quiet before the burst
    # is treated as complete.
    [ValidateRange(1, 1000)][int]$EvidenceEvents = 1,
    [ValidateRange(5, 300)][int]$SettleSeconds = 20,

    # A client that retries rejected requests may never go quiet for the settle
    # window. This many post-startup events is already past doubt, so the run
    # stops on it.
    [ValidateRange(1, 10000)][int]$EvidenceCap = 40,

    # Optional. Stages a WinMM proxy beside the isolated executable. The
    # differential does not need one; this exists to re-run it in the
    # configuration the product will ship.
    [string]$ProxyPath = '',

    [bool]$ForceCloseAfterTimeout = $true
)

# Attributes NCM playback traffic with a process-local environment-proxy
# differential.
#
# `http_proxy`/`https_proxy` are honored by libcurl and ignored by WinHTTP,
# WinINet, and Schannel, and the CEF `--proxy-server` path is already falsified
# for this traffic, so a request arriving at the loopback observer isolates a
# libcurl-class stack and observer silence excludes it. The census that this
# replaces could not discriminate: every candidate stack is mapped before the
# first track plays.
#
# The variables are set on the isolated process only. No system, per-user, or
# client-persisted proxy setting is touched, and the observer forwards nothing.
#
# The operator plays the tracks. This run holds only until its evidence is
# settled, then reclaims the tree by itself.

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

. (Join-Path $PSScriptRoot 'lib\isolated-client.ps1')
. (Join-Path $PSScriptRoot 'lib\observer-log.ps1')

if ([string]::IsNullOrWhiteSpace($ObserverPath)) {
    $ObserverPath = Join-Path $PSScriptRoot '..\build\win32-release\src\proxy_observer\ncm_proxy_observer.exe'
}
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path ([System.IO.Path]::GetTempPath()) 'ncm-env-proxy'
}

$target = Assert-ExactClientTarget -NcmPath $NcmPath
$observer = Get-OrdinaryFile -LiteralPath $ObserverPath -Description 'loopback observer'
$proxy = $null
if (-not [string]::IsNullOrWhiteSpace($ProxyPath)) {
    $proxy = Get-OrdinaryFile -LiteralPath $ProxyPath -Description 'winmm proxy'
}
Assert-NoClientRunning

$realState = Get-ClientLocalStatePath
$before = Get-ClientStateFingerprint -LiteralPath $realState

$outputRoot = (New-Item -ItemType Directory -Force -Path $OutputDirectory).FullName
$stamp = [DateTime]::Now.ToString('yyyyMMdd-HHmmss')
$observerLog = Join-Path $outputRoot "observer-$stamp.log"
$observerError = Join-Path $outputRoot "observer-$stamp.err"
$summaryPath = Join-Path $outputRoot "env-proxy-$stamp.txt"
$stopFile = Join-Path $outputRoot 'stop'
Remove-Item -LiteralPath $stopFile -ErrorAction SilentlyContinue

$root = New-ExperimentRoot -Prefix 'ncm-unblock-297-env-proxy-'
Write-Output "private-experiment-directory: $($root.FullName)"

# Recovery for the case where the redirected profile does not contain the
# client. No NCM process runs before or after this experiment, so the isolated
# copy is the only possible writer.
$backup = Join-Path $root.FullName 'localdata.backup'
if ($before.Exists) {
    Copy-Item -LiteralPath $realState -Destination $backup
}

$launched = $null
$observerProcess = $null
$timeline = @()
$invariant = [System.Globalization.CultureInfo]::InvariantCulture
try {
    $tree = New-IsolatedClientTree -ExperimentRoot $root -Target $target
    if ($null -ne $proxy) {
        $staged = Add-IsolatedWinmmProxy -App $tree.App -Proxy $proxy
        Write-Output "staged-winmm-proxy: $staged"
    }
    Write-Output "isolated-target: $($tree.Target)"

    $idleTimeoutMs = [Math]::Min(3600000, ($ObservationSeconds + 60) * 1000)
    $observerProcess = Start-Process -FilePath $observer.FullName `
        -ArgumentList '--port', '0', '--max-events', '10000', '--idle-timeout-ms', "$idleTimeoutMs" `
        -RedirectStandardOutput $observerLog -RedirectStandardError $observerError `
        -PassThru -WindowStyle Hidden

    $port = 0
    for ($attempt = 0; $attempt -lt 100 -and $port -eq 0; ++$attempt) {
        Start-Sleep -Milliseconds 100
        $port = Get-ObserverPort -LiteralPath $observerLog
    }
    if ($port -eq 0) {
        throw "the loopback observer never reported a bound port; its stderr is at $observerError"
    }
    $endpoint = "http://127.0.0.1:$port"
    Write-Output "observer-endpoint: $endpoint"
    Write-Output "observer-log: $observerLog"

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $tree.Target
    $startInfo.WorkingDirectory = $tree.App
    $startInfo.UseShellExecute = $false
    $startInfo.EnvironmentVariables['LOCALAPPDATA'] = $tree.Profile
    # Windows environment variables are case-insensitive, so these three cover
    # the upper-case spellings as well. An ambient `no_proxy` would silently
    # exempt the hosts under test, so it is removed rather than inherited.
    $startInfo.EnvironmentVariables['http_proxy'] = $endpoint
    $startInfo.EnvironmentVariables['https_proxy'] = $endpoint
    $startInfo.EnvironmentVariables['all_proxy'] = $endpoint
    [void]$startInfo.EnvironmentVariables.Remove('no_proxy')
    Write-Output 'environment-proxy: http_proxy, https_proxy, all_proxy (process-local only)'
    Write-Output "stop-file: $stopFile"
    Write-Output ''
    Write-Output 'operator-action: wait for the startup window to close, then play one normal'
    Write-Output '  track and one greyed-out track. Startup and sign-in traffic is recorded'
    Write-Output "  separately and never ends the run; the first $StartupSeconds seconds are reserved for it."
    Write-Output '  The run ends by itself once traffic settles after a track change.'
    Write-Output '  The observer rejects every request, so a client that cannot load content is'
    Write-Output '  itself the result; quit the client from its tray icon to end the run early.'
    Write-Output ''

    $launched = [System.Diagnostics.Process]::Start($startInfo)
    $started = [DateTime]::UtcNow
    $deadline = $started.AddSeconds($ObservationSeconds)

    $consumedLines = 0
    $totalEvents = 0
    $startupEvents = 0
    $sinceMarkerEvents = 0
    $sinceMarkerNetease = 0
    $lastEvent = $null
    $observedWindow = $false
    $lastTitle = ''
    $markers = 0
    $markersAfterStartup = 0
    $promptShown = $false
    $observerStopped = ''
    $reason = 'deadline'

    while ($true) {
        Start-Sleep -Milliseconds 1000
        $now = [DateTime]::UtcNow
        $seconds = ($now - $started).TotalSeconds
        $offset = ([Math]::Round($seconds, 1)).ToString($invariant)
        $afterStartup = $seconds -ge $StartupSeconds

        if ($afterStartup -and -not $promptShown) {
            $promptShown = $true
            $timeline += "t=$offset operator-window-open"
            Write-Output ''
            Write-Output "operator-window-open (t=$offset): play one normal track, then one greyed-out track."
            Write-Output '  The run ends by itself once traffic settles after a track change.'
            Write-Output ''
        }

        # The window state is read before the log so that a track change and the
        # traffic it causes inside the same tick are attributed in that order.
        if (-not $launched.HasExited) {
            try {
                $launched.Refresh()
                if ($launched.MainWindowHandle -ne 0) {
                    if (-not $observedWindow) {
                        $observedWindow = $true
                        $timeline += "t=$offset main-window"
                        Write-Output "main-window: True (t=$offset)"
                    }
                    # The root window title is the track the client is playing,
                    # so a change is a playback signal. Only the transition is
                    # recorded; the title itself is the operator's listening
                    # history and is never written down.
                    $title = $launched.MainWindowTitle
                    if (-not [string]::IsNullOrWhiteSpace($title) -and $title -ne $lastTitle) {
                        $lastTitle = $title
                        ++$markers
                        $phase = if ($afterStartup) { 'playback' } else { 'startup' }
                        if ($afterStartup) {
                            ++$markersAfterStartup
                            $sinceMarkerEvents = 0
                            $sinceMarkerNetease = 0
                        }
                        $timeline += "t=$offset track-change index=$markers phase=$phase"
                        Write-Output "track-change: $markers ($phase, t=$offset)"
                    }
                }
            } catch {
                # The root can exit while its window state is being read.
            }
        }

        $tail = Read-ObserverLines -LiteralPath $observerLog -Consumed $consumedLines
        $consumedLines = $tail.Consumed
        foreach ($line in @($tail.Lines)) {
            $record = Get-ObserverRecord -Line $line
            if ($null -eq $record) {
                continue
            }
            if ($record.Kind -eq 'event') {
                ++$totalEvents
                $lastEvent = $now
                if ($afterStartup) {
                    if ($markersAfterStartup -ge 1) {
                        ++$sinceMarkerEvents
                        if ($record.Destination -eq 'netease') { ++$sinceMarkerNetease }
                    }
                } else {
                    ++$startupEvents
                }
                $phase = if ($afterStartup) { 'playback' } else { 'startup' }
                $entry = "t=$offset proxy-event phase=$phase method=$($record.Method) form=$($record.Form) scheme=$($record.Scheme) destination=$($record.Destination) port=$($record.Port)"
                $timeline += $entry
                Write-Output $entry
            } else {
                $observerStopped = $record.Reason
                $timeline += "t=$offset observer-stopped reason=$observerStopped"
                Write-Output "observer-stopped: $observerStopped"
            }
        }

        if ($launched.HasExited) { $reason = 'client-exited'; break }
        if (Test-Path -LiteralPath $stopFile) { $reason = 'stop-requested'; break }
        if ($sinceMarkerEvents -ge $EvidenceCap) { $reason = 'evidence-cap'; break }
        if ($markersAfterStartup -ge 1 -and $sinceMarkerEvents -ge $EvidenceEvents -and
            $null -ne $lastEvent -and ($now - $lastEvent).TotalSeconds -ge $SettleSeconds) {
            $reason = 'evidence-complete'
            break
        }
        if ($now -ge $deadline) { $reason = 'deadline'; break }
    }

    $elapsed = ([Math]::Round(([DateTime]::UtcNow - $started).TotalSeconds, 1)).ToString($invariant)
    $netease = @($timeline | Where-Object { $_ -like '*proxy-event*destination=netease*' }).Count

    if ($sinceMarkerNetease -gt 0) {
        $verdict = 'positive: traffic after an operator track change honors http_proxy/https_proxy and reaches a NetEase host'
    } elseif ($sinceMarkerEvents -gt 0) {
        $verdict = 'partial: traffic after a track change honored the environment proxy, but no NetEase destination'
    } elseif ($markersAfterStartup -ge 1 -and $totalEvents -gt 0) {
        $verdict = 'negative-for-playback: only pre-playback traffic honored the environment proxy'
    } elseif ($markersAfterStartup -ge 1) {
        $verdict = 'negative: playback was exercised and no request honored http_proxy/https_proxy'
    } elseif ($totalEvents -gt 0) {
        $verdict = 'startup-only: the environment proxy was honored before playback was demonstrated'
    } else {
        $verdict = 'inconclusive: no request arrived and playback was not demonstrated'
    }

    $summary = @(
        "run: $stamp"
        "target: $($target.FullName) $($target.VersionInfo.FileVersion)"
        "staged-winmm-proxy: $(if ($null -ne $proxy) { $proxy.FullName } else { 'none' })"
        "observer-endpoint: $endpoint"
        'environment: http_proxy, https_proxy, all_proxy set process-local; no_proxy removed'
        "startup-window-seconds: $StartupSeconds"
        "stop-reason: $reason"
        "elapsed-seconds: $elapsed"
        "main-window: $observedWindow"
        "track-changes: $markers after-startup=$markersAfterStartup"
        "proxy-events: $totalEvents startup=$startupEvents after-track-change=$sinceMarkerEvents netease-total=$netease netease-after-track-change=$sinceMarkerNetease"
        "observer-stopped: $(if ($observerStopped) { $observerStopped } else { 'no' })"
        "verdict: $verdict"
        '--- timeline ---'
    ) + $timeline
    Set-Content -LiteralPath $summaryPath -Value $summary -Encoding UTF8

    Write-Output ''
    Write-Output "stop-reason: $reason"
    Write-Output "elapsed-seconds: $elapsed"
    Write-Output "track-changes: $markers after-startup=$markersAfterStartup"
    Write-Output "proxy-events: $totalEvents startup=$startupEvents after-track-change=$sinceMarkerEvents netease-after-track-change=$sinceMarkerNetease"
    Write-Output "verdict: $verdict"
    Write-Output "summary: $summaryPath"

    if (-not $launched.HasExited) {
        [void]$launched.CloseMainWindow()
        [void]$launched.WaitForExit(5000)
    }
}
finally {
    if ($null -ne $observerProcess -and -not $observerProcess.HasExited) {
        $observerProcess.Kill()
        [void]$observerProcess.WaitForExit(5000)
    }

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
