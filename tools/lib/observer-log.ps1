# Tailing helpers for the loopback observer's stdout.
#
# The observer flushes one line per event into a file it still holds open, so
# reads share the writer's handle and stop at the last newline. The remainder
# after that newline is either empty or a partially written record, and
# consuming it would retire the slot the next line is appended into.
#
# Dot-source this file; it defines functions and starts nothing.

function Read-SharedText {
    param([Parameter(Mandatory)][string]$LiteralPath)

    if (-not (Test-Path -LiteralPath $LiteralPath -PathType Leaf)) {
        return ''
    }
    $stream = [System.IO.File]::Open($LiteralPath, 'Open', 'Read', 'ReadWrite')
    try {
        return ([System.IO.StreamReader]::new($stream)).ReadToEnd()
    } finally {
        $stream.Dispose()
    }
}

function Get-ObserverPort {
    param([Parameter(Mandatory)][string]$LiteralPath)

    if ((Read-SharedText -LiteralPath $LiteralPath) -match
        'observer-listening address=127\.0\.0\.1 port=(\d+)') {
        return [int]$Matches[1]
    }
    return 0
}

function Read-ObserverLines {
    param(
        [Parameter(Mandatory)][string]$LiteralPath,
        [int]$Consumed = 0
    )

    $lines = @((Read-SharedText -LiteralPath $LiteralPath) -split "`r?`n")
    $complete = [Math]::Max(0, $lines.Count - 1)
    $fresh = @()
    for ($index = $Consumed; $index -lt $complete; ++$index) {
        $fresh += $lines[$index]
    }
    return [pscustomobject]@{ Lines = $fresh; Consumed = $complete }
}

function Get-ObserverRecord {
    param([Parameter(Mandatory)][AllowEmptyString()][string]$Line)

    if ($Line -match ('^proxy-event index=\d+ method=(\S+) target-form=(\S+) scheme=(\S+)' +
                      ' destination=(\S+) port=(\S+) headers-complete=(\S+)$')) {
        return [pscustomobject]@{
            Kind        = 'event'
            Method      = $Matches[1]
            Form        = $Matches[2]
            Scheme      = $Matches[3]
            Destination = $Matches[4]
            Port        = $Matches[5]
        }
    }
    if ($Line -match '^observer-stopped reason=(\S+)') {
        return [pscustomobject]@{ Kind = 'stopped'; Reason = $Matches[1] }
    }
    return $null
}
