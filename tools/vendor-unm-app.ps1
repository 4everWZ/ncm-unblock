[CmdletBinding()]
param(
    [string]$Tag = 'v0.28.0'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$unmDir = Join-Path $repository 'third_party\unm'
$upstream = Join-Path $unmDir 'app.js.upstream'
$patched = Join-Path $unmDir 'app.js'
$coreJs = Join-Path $repository 'core\unm-app.js'
$url = "https://raw.githubusercontent.com/UnblockNeteaseMusic/server/$Tag/precompiled/app.js"

[void](New-Item -ItemType Directory -Path $unmDir -Force)
[void](New-Item -ItemType Directory -Path (Join-Path $repository 'core') -Force)

Write-Output "download $url"
Invoke-WebRequest -Uri $url -OutFile $upstream -UseBasicParsing

$oldBr = @(
    '"downloadMaxbr"in t&&0===t.downloadMaxbr&&(t.downloadMaxbr=32e4),',
    '"dl"in t&&"downloadMaxbr"in t&&t.dl<t.downloadMaxbr&&(t.dl=t.downloadMaxbr),',
    '"playMaxbr"in t&&0===t.playMaxbr&&(t.playMaxbr=32e4),',
    '"pl"in t&&"playMaxbr"in t&&t.pl<t.playMaxbr&&(t.pl=t.playMaxbr),'
) -join ''

$newBr = @(
    '"downloadMaxbr"in t&&("true"===(process.env.ENABLE_FLAC||"").toLowerCase()?',
    '(t.downloadMaxbr<999e3&&(t.downloadMaxbr=999e3)):(0===t.downloadMaxbr&&(t.downloadMaxbr=32e4))),',
    '"dl"in t&&"downloadMaxbr"in t&&t.dl<t.downloadMaxbr&&(t.dl=t.downloadMaxbr),',
    '"playMaxbr"in t&&("true"===(process.env.ENABLE_FLAC||"").toLowerCase()?',
    '(t.playMaxbr<999e3&&(t.playMaxbr=999e3)):(0===t.playMaxbr&&(t.playMaxbr=32e4))),',
    '"pl"in t&&"playMaxbr"in t&&t.pl<t.playMaxbr&&(t.pl=t.playMaxbr),'
) -join ''

$oldLevel = @(
    '"flLevel"in t&&"none"===t.flLevel&&(t.flLevel="exhigh"),',
    '"plLevel"in t&&"none"===t.plLevel&&(t.plLevel="exhigh"),',
    '"dlLevel"in t&&"none"===t.dlLevel&&(t.dlLevel="exhigh")'
) -join ''

$newLevel = @(
    '("true"===(process.env.ENABLE_FLAC||"").toLowerCase()?',
    '("flLevel"in t&&"lossless"!==t.flLevel&&(t.flLevel="lossless"),',
    '"plLevel"in t&&"lossless"!==t.plLevel&&(t.plLevel="lossless"),',
    '"dlLevel"in t&&"lossless"!==t.dlLevel&&(t.dlLevel="lossless"),',
    't.playMaxLevel="lossless",',
    't.downloadMaxLevel="lossless",',
    't.maxBrLevel="lossless"):',
    '("flLevel"in t&&"none"===t.flLevel&&(t.flLevel="exhigh"),',
    '"plLevel"in t&&"none"===t.plLevel&&(t.plLevel="exhigh"),',
    '"dlLevel"in t&&"none"===t.dlLevel&&(t.dlLevel="exhigh")))'
) -join ''

$text = [IO.File]::ReadAllText($upstream)
if ($text.IndexOf($oldBr) -lt 0) {
    throw 'Upstream bitrate inject block not found; adjust vendor-unm-app.ps1 for this tag.'
}
if ($text.IndexOf($oldLevel) -lt 0) {
    throw 'Upstream level inject block not found; adjust vendor-unm-app.ps1 for this tag.'
}

$out = $text.Replace($oldBr, $newBr).Replace($oldLevel, $newLevel)
if ($out.IndexOf('plLevel"in t&&"lossless"!==t.plLevel') -lt 0) {
    throw 'Privilege lossless patch did not apply.'
}

[IO.File]::WriteAllText($patched, $out)
Copy-Item -LiteralPath $patched -Destination $coreJs -Force
Write-Output "wrote $patched"
Write-Output "wrote $coreJs"
Write-Output ("bytes=" + (Get-Item -LiteralPath $coreJs).Length)
