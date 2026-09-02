# Explores the exact 2.9.7 client's embedded browser through its own remote
# debugging endpoint, if it accepts one.
#
# Why this runs before any injection code: every remaining gate on the
# business-layer route -- are the frontend anchors reachable at runtime, what
# shape does the player-URL response have, and how long may a native call take
# before the client's retry path fires -- is answerable from a live V8 context.
# A debugging endpoint answers them without a struct layout, an extension, or a
# single line of injected native code. If the client refuses the switch, that is
# a clean negative and the work falls back to native injection.
#
# Safety boundary: only an isolated copy of the installation is started, the
# switch is process-local, nothing is written into the installed tree, and the
# report carries fixed classifications and framework shapes only -- never page
# content, request bodies, URLs, titles, cookies, or credentials.

[CmdletBinding()]
param(
    [string]$NcmPath = 'D:\Program Files (x86)\Netease\CloudMusic\cloudmusic.exe',
    [string]$OutputDirectory = (Join-Path $env:TEMP 'ncm-devtools'),
    [int]$EndpointTimeoutSeconds = 60,
    [int]$FrontendTimeoutSeconds = 180,
    [int]$ObservationSeconds = 900,
    [int]$StopTimeoutSeconds = 30
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

. (Join-Path $PSScriptRoot 'lib\isolated-client.ps1')

function Get-FreeLoopbackPort {
    $listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
    $listener.Start()
    try { return [int]$listener.LocalEndpoint.Port } finally { $listener.Stop() }
}

function Invoke-DevToolsHttp {
    param([Parameter(Mandatory)][string]$Uri)

    try {
        return Invoke-RestMethod -Uri $Uri -TimeoutSec 5 -UseBasicParsing
    } catch {
        return $null
    }
}

# Classifies a target without recording where the user is. Frontend paths are
# build artifacts; query strings and fragments can carry state, so they are
# dropped.
function Get-TargetLabel {
    param([Parameter(Mandatory)][psobject]$Target)

    $url = if ($Target.PSObject.Properties.Name -contains 'url') { [string]$Target.url } else { '' }
    if ([string]::IsNullOrWhiteSpace($url)) { return 'about:blank' }
    try {
        $parsed = [Uri]$url
        return ('{0}://{1}{2}' -f $parsed.Scheme, $parsed.Host, $parsed.AbsolutePath)
    } catch {
        return 'unparsable'
    }
}

class CdpSession : System.IDisposable {
    [System.Net.WebSockets.ClientWebSocket]$Socket
    [int]$NextId = 1

    CdpSession([string]$endpoint) {
        $this.Socket = [System.Net.WebSockets.ClientWebSocket]::new()
        $this.Socket.ConnectAsync([Uri]$endpoint, [Threading.CancellationToken]::None).GetAwaiter().GetResult()
    }

    [psobject] Send([string]$method, [hashtable]$parameters) {
        $id = $this.NextId++
        $payload = @{ id = $id; method = $method; params = $parameters } | ConvertTo-Json -Depth 10 -Compress
        $bytes = [Text.Encoding]::UTF8.GetBytes($payload)
        $this.Socket.SendAsync(
            [ArraySegment[byte]]::new($bytes),
            [System.Net.WebSockets.WebSocketMessageType]::Text,
            $true, [Threading.CancellationToken]::None).GetAwaiter().GetResult()

        # Responses interleave with protocol events, so read until this id comes
        # back rather than assuming the next frame is the answer.
        $deadline = [DateTime]::UtcNow.AddSeconds(20)
        while ([DateTime]::UtcNow -lt $deadline) {
            $message = $this.Receive()
            if ($null -eq $message) { continue }
            if (($message.PSObject.Properties.Name -contains 'id') -and ([int]$message.id -eq $id)) {
                return $message
            }
        }
        throw "no response to $method within the read budget."
    }

    [psobject] Receive() {
        $buffer = [byte[]]::new(65536)
        $builder = [Text.StringBuilder]::new()
        do {
            $segment = [ArraySegment[byte]]::new($buffer)
            $result = $this.Socket.ReceiveAsync($segment, [Threading.CancellationToken]::None).GetAwaiter().GetResult()
            if ($result.MessageType -eq [System.Net.WebSockets.WebSocketMessageType]::Close) { return $null }
            [void]$builder.Append([Text.Encoding]::UTF8.GetString($buffer, 0, $result.Count))
        } while (-not $result.EndOfMessage)

        try { return $builder.ToString() | ConvertFrom-Json } catch { return $null }
    }

    # Evaluates an expression and returns its value, or an error marker. The
    # expressions this probe sends return booleans, counts, and framework
    # identifier names only.
    [string] Evaluate([string]$expression) {
        try {
            $response = $this.Send('Runtime.evaluate', @{
                expression    = $expression
                returnByValue = $true
                timeout       = 5000
            })
        } catch {
            return "send-failed: $($_.Exception.Message)"
        }
        if ($response.PSObject.Properties.Name -contains 'error') {
            return "protocol-error: $($response.error.message)"
        }
        $inner = $response.result
        if ($inner.PSObject.Properties.Name -contains 'exceptionDetails') {
            return 'threw'
        }
        if ($inner.result.PSObject.Properties.Name -notcontains 'value') {
            return "type:$($inner.result.type)"
        }
        return [string]$inner.result.value
    }

    [void] Dispose() {
        if ($null -ne $this.Socket) {
            try {
                $this.Socket.CloseAsync(
                    [System.Net.WebSockets.WebSocketCloseStatus]::NormalClosure, '',
                    [Threading.CancellationToken]::None).GetAwaiter().GetResult()
            } catch { }
            $this.Socket.Dispose()
        }
    }
}

# Fixed battery. Every expression returns a short classification, never page
# content. `ownKeys` results are minified framework identifiers from a shipped
# build, not user data.
$AnchorBattery = [ordered]@{
    'location-path'        = 'location.protocol + location.pathname'
    'frame-count'          = 'String(window.frames.length)'
    'has-NEJ'              = 'typeof window.NEJ'
    'has-NEJ-P'            = 'typeof (window.NEJ && window.NEJ.P)'
    'has-nej-lower'        = 'typeof window.nej'
    'has-nm'               = 'typeof window.nm'
    'nej-ut-j-type'        = 'typeof (window.NEJ && window.NEJ.P && window.NEJ.P("nej.ut.j"))'
    'nej-ut-j-keys'        = '(function(){try{var m=window.NEJ.P("nej.ut.j");return m?Object.keys(m).slice(0,40).join(","):"null"}catch(e){return "threw"}})()'
    'nej-h-keys'           = '(function(){try{var m=window.NEJ.P("nej.h");return m?Object.keys(m).slice(0,40).join(","):"null"}catch(e){return "threw"}})()'
    'has-ctl'              = 'typeof window.ctl'
    'ctl-keys'             = '(function(){try{return window.ctl?Object.keys(window.ctl).slice(0,60).join(","):"null"}catch(e){return "threw"}})()'
    'has-dc'               = 'typeof window.dc'
    'xhr-is-native'        = 'String(/\[native code\]/.test(String(window.XMLHttpRequest)))'
    'xhr-open-is-native'   = 'String(/\[native code\]/.test(String(window.XMLHttpRequest.prototype.open)))'
    'global-fn-count'      = '(function(){var n=0;for(var k in window){try{if(typeof window[k]==="function")n++}catch(e){}}return String(n)})()'
}

# Installs a passive recorder over XHR. It counts and classifies; it never
# stores a URL, a request body, or a response body. The player-URL response is
# summarised into the four fields the design needs and nothing else.
$RecorderSource = @'
(function(){
  if (window.__ncmProbeInstalled) { return "already"; }
  var open = XMLHttpRequest.prototype.open;
  var send = XMLHttpRequest.prototype.send;
  if (!open || !send) { return "no-xhr"; }
  var state = { total: 0, playerUrl: 0, decoded: 0, opaque: 0, samples: [] };
  window.__ncmProbe = state;
  XMLHttpRequest.prototype.open = function(method, url) {
    try { this.__ncmIsPlayerUrl = String(url).indexOf("player/url") >= 0; } catch (e) {}
    return open.apply(this, arguments);
  };
  XMLHttpRequest.prototype.send = function() {
    var self = this;
    try {
      state.total++;
      if (self.__ncmIsPlayerUrl) {
        state.playerUrl++;
        self.addEventListener("load", function(){
          try {
            var body = JSON.parse(self.responseText);
            state.decoded++;
            var first = body && body.data && body.data[0];
            if (state.samples.length < 24) {
              state.samples.push([
                body ? body.code : -1,
                first ? first.code : -1,
                first ? (first.url ? 1 : 0) : -1,
                first ? (first.br || 0) : -1
              ].join(":"));
            }
          } catch (e) { state.opaque++; }
        });
      }
    } catch (e) {}
    return send.apply(self, arguments);
  };
  window.__ncmProbeInstalled = true;
  return "installed";
})()
'@

$RecorderReport = '(function(){var s=window.__ncmProbe;if(!s)return "absent";return ["total="+s.total,"playerUrl="+s.playerUrl,"decoded="+s.decoded,"opaque="+s.opaque,"samples="+s.samples.join("|")].join(" ")})()'

$target = Assert-ExactClientTarget -NcmPath $NcmPath
Assert-NoClientRunning
[void](New-Item -ItemType Directory -Path $OutputDirectory -Force)

$statePath = Get-ClientLocalStatePath
$stateBefore = Get-ClientStateFingerprint -LiteralPath $statePath
$experimentRoot = New-ExperimentRoot -Prefix 'ncm-unblock-297-devtools-'
$backupPath = Join-Path $experimentRoot.FullName 'localdata.backup'
if ($stateBefore.Exists) { Copy-Item -LiteralPath $statePath -Destination $backupPath -Force }

$reportPath = Join-Path $OutputDirectory ('devtools-{0:yyyyMMdd-HHmmss}.txt' -f (Get-Date))
$report = [Collections.Generic.List[string]]::new()
function Add-Line { param([string]$Text) $report.Add($Text); Write-Output $Text }

Write-Output "private-recovery-directory: $($experimentRoot.FullName)"
Write-Output "report: $reportPath"

$port = Get-FreeLoopbackPort
$session = $null
try {
    $tree = New-IsolatedClientTree -ExperimentRoot $experimentRoot -Target $target
    Add-Line "target-version: $($target.VersionInfo.FileVersion)"
    Add-Line "remote-debugging-port: $port"

    $arguments = @("--remote-debugging-port=$port")
    $process = Start-Process -FilePath $tree.Target -ArgumentList $arguments `
        -WorkingDirectory $tree.App -PassThru
    Add-Line "client-started: pid=$($process.Id)"

    # Phase A: does the client honour the switch at all? `/json` is the oldest
    # spelling and the one a 2014 Chromium is most likely to serve.
    $listUri = "http://127.0.0.1:$port/json"
    $versionUri = "http://127.0.0.1:$port/json/version"
    $deadline = [DateTime]::UtcNow.AddSeconds($EndpointTimeoutSeconds)
    $targets = $null
    while ([DateTime]::UtcNow -lt $deadline) {
        $targets = Invoke-DevToolsHttp -Uri $listUri
        if ($null -eq $targets) { $targets = Invoke-DevToolsHttp -Uri "http://127.0.0.1:$port/json/list" }
        if ($null -ne $targets) { break }
        Start-Sleep -Milliseconds 500
    }

    if ($null -eq $targets) {
        Add-Line 'devtools-endpoint: absent'
        Add-Line 'verdict: the client did not open a remote debugging endpoint; fall back to native injection.'
    } else {
        Add-Line 'devtools-endpoint: present'
        $browserVersion = Invoke-DevToolsHttp -Uri $versionUri
        if ($null -ne $browserVersion -and $browserVersion.PSObject.Properties.Name -contains 'Browser') {
            Add-Line "devtools-browser: $($browserVersion.Browser)"
        }
        if ($null -ne $browserVersion -and $browserVersion.PSObject.Properties.Name -contains 'Protocol-Version') {
            Add-Line "devtools-protocol: $($browserVersion.'Protocol-Version')"
        }

        $pages = @($targets | Where-Object { $_.type -eq 'page' })
        Add-Line "devtools-targets: total=$(@($targets).Count) pages=$($pages.Count)"
        foreach ($item in $targets) {
            Add-Line "  target type=$($item.type) label=$(Get-TargetLabel -Target $item)"
        }

        # Phase B: what is actually reachable in each page context?
        #
        # The endpoint answers well before the frontend has run, so the battery
        # waits for a page that has the framework registry. Without this a slow
        # start would be recorded as "anchors absent", which is the same output
        # a genuine failure produces.
        $readyDeadline = [DateTime]::UtcNow.AddSeconds($FrontendTimeoutSeconds)
        $frontendReady = $false
        while ([DateTime]::UtcNow -lt $readyDeadline -and -not $frontendReady) {
            foreach ($candidate in @(Invoke-DevToolsHttp -Uri $listUri | Where-Object { $_.type -eq 'page' })) {
                if ($candidate.PSObject.Properties.Name -notcontains 'webSocketDebuggerUrl') { continue }
                try {
                    $session = [CdpSession]::new($candidate.webSocketDebuggerUrl)
                    if ($session.Evaluate('typeof window.NEJ') -eq 'object') { $frontendReady = $true }
                } catch {
                } finally {
                    if ($null -ne $session) { $session.Dispose(); $session = $null }
                }
                if ($frontendReady) { break }
            }
            if (-not $frontendReady) { Start-Sleep -Seconds 2 }
        }
        Add-Line "frontend-ready: $frontendReady"

        $pages = @(Invoke-DevToolsHttp -Uri $listUri | Where-Object { $_.type -eq 'page' })
        Add-Line "pages-at-battery: $($pages.Count)"

        $recorded = 0
        foreach ($page in $pages) {
            if ($page.PSObject.Properties.Name -notcontains 'webSocketDebuggerUrl') { continue }
            $label = Get-TargetLabel -Target $page
            Add-Line "page: $label"
            try {
                $session = [CdpSession]::new($page.webSocketDebuggerUrl)
                foreach ($name in $AnchorBattery.Keys) {
                    Add-Line "  $name = $($session.Evaluate($AnchorBattery[$name]))"
                }
                Add-Line "  recorder = $($session.Evaluate($RecorderSource))"
                $recorded++
            } catch {
                Add-Line "  session-failed: $($_.Exception.Message)"
            } finally {
                if ($null -ne $session) { $session.Dispose(); $session = $null }
            }
        }
        Add-Line "recorders-installed: $recorded"

        # Phase C: hold while the operator plays. The run ends when the client
        # exits, when a stop file appears, or at the ceiling.
        Add-Line ''
        Add-Line 'Play one normal track and one greyed-out track, then close the client normally.'
        $stopFile = Join-Path $OutputDirectory 'stop'
        $ceiling = [DateTime]::UtcNow.AddSeconds($ObservationSeconds)
        while ([DateTime]::UtcNow -lt $ceiling) {
            Start-Sleep -Seconds 3
            if (Test-Path -LiteralPath $stopFile) { Add-Line 'stopped: stop-file'; break }
            if (@(Get-IsolatedProcesses -Root $experimentRoot.FullName).Count -eq 0) {
                Add-Line 'stopped: client-exited'
                break
            }
        }

        # Read the recorders back while the contexts still exist. A client that
        # has already exited leaves whatever it recorded unreadable, which is
        # itself reported rather than hidden. The battery is repeated here
        # because frames created after the first pass carry the player logic in
        # a context the first pass never saw.
        $live = @(Invoke-DevToolsHttp -Uri $listUri | Where-Object { $_.type -eq 'page' })
        Add-Line "readback-pages: $($live.Count)"
        foreach ($page in $live) {
            if ($page.PSObject.Properties.Name -notcontains 'webSocketDebuggerUrl') { continue }
            $label = Get-TargetLabel -Target $page
            try {
                $session = [CdpSession]::new($page.webSocketDebuggerUrl)
                Add-Line "readback page: $label"
                Add-Line "  recorder = $($session.Evaluate($RecorderReport))"
                foreach ($name in $AnchorBattery.Keys) {
                    Add-Line "  $name = $($session.Evaluate($AnchorBattery[$name]))"
                }
            } catch {
                Add-Line "readback-failed: $($_.Exception.Message)"
            } finally {
                if ($null -ne $session) { $session.Dispose(); $session = $null }
            }
        }
    }
} finally {
    if ($null -ne $session) { $session.Dispose() }
    $stopped = Stop-IsolatedTree -Root $experimentRoot.FullName -TimeoutSeconds $StopTimeoutSeconds
    Add-Line "isolated-tree-stopped: $stopped"
    # These emit their verdicts to the caller, so they are captured rather than
    # left out of the artifact of record: whether the real client state came
    # back unchanged is exactly the line a later reader needs.
    foreach ($line in @(Restore-ClientState -StatePath $statePath -Before $stateBefore -BackupPath $backupPath)) {
        Add-Line ([string]$line)
    }
    foreach ($line in @(Remove-ExperimentRoot -ExperimentRoot $experimentRoot)) {
        Add-Line ([string]$line)
    }
    Set-Content -LiteralPath $reportPath -Value $report -Encoding UTF8
    Write-Output "report-written: $reportPath"
}
