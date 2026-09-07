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

# Gate allied max-level writes to privilege-shaped objects only. Unconditional
# assigns ran on every JSON node (JSON.stringify walker), polluting vip/info,
# song roots, etc. with playMaxLevel — enough for PC username menu to full-refresh.
$newLevel = @(
    '("true"===(process.env.ENABLE_FLAC||"").toLowerCase()?',
    '("flLevel"in t&&"lossless"!==t.flLevel&&(t.flLevel="lossless"),',
    '"plLevel"in t&&"lossless"!==t.plLevel&&(t.plLevel="lossless"),',
    '"dlLevel"in t&&"lossless"!==t.dlLevel&&(t.dlLevel="lossless"),',
    '("plLevel"in t||"playMaxbr"in t||"downloadMaxbr"in t||"maxbr"in t)&&(t.playMaxLevel="lossless",t.downloadMaxLevel="lossless",t.maxBrLevel="lossless",t.playMaxBrLevel="lossless",t.downloadMaxBrLevel="lossless"),',
    '"maxbr"in t&&t.maxbr<999e3&&(t.maxbr=999e3)):',
    '("flLevel"in t&&"none"===t.flLevel&&(t.flLevel="exhigh"),',
    '"plLevel"in t&&"none"===t.plLevel&&(t.plLevel="exhigh"),',
    '"dlLevel"in t&&"none"===t.dlLevel&&(t.dlLevel="exhigh")))'
) -join ''

# tryMatch URL body: PC quality chip reads data[].level / encodeType, which stay null upstream.
$oldTryMatch = 'e.br=t.br||128e3,e.size=t.size,e.code=200,e.freeTrialInfo=null,t'
$newTryMatch = @(
    'e.br=t.br||128e3,',
    'e.size=t.size,',
    'e.code=200,',
    'e.freeTrialInfo=null,',
    'e.level=999e3===e.br||"flac"===e.type?"lossless":320e3<=e.br?"exhigh":192e3<=e.br?"higher":"standard",',
    'e.encodeType=e.type||"mp3",',
    't'
) -join ''

# LOCAL_VIP expireTime: upstream uses now+1y on every response. That makes
# vip/info membership fields change on each username-menu fetch; NCM 2.10.12
# then force-refreshes the shell after the dropdown opens. Day-bucket the base.
$oldVipExpire = 'let i=(t.data.now||new Date().getTime())+316224e5'
$newVipExpire = 'let i=Math.floor((t.data.now||new Date().getTime())/864e5)*864e5+316224e5'

$text = [IO.File]::ReadAllText($upstream)
if ($text.IndexOf($oldBr) -lt 0) {
    throw 'Upstream bitrate inject block not found; adjust vendor-unm-app.ps1 for this tag.'
}
if ($text.IndexOf($oldLevel) -lt 0) {
    throw 'Upstream level inject block not found; adjust vendor-unm-app.ps1 for this tag.'
}
if ($text.IndexOf($oldTryMatch) -lt 0) {
    throw 'Upstream tryMatch URL body block not found; adjust vendor-unm-app.ps1 for this tag.'
}
if ($text.IndexOf($oldVipExpire) -lt 0) {
    throw 'Upstream LOCAL_VIP expireTime expression not found; adjust vendor-unm-app.ps1 for this tag.'
}

$out = $text.Replace($oldBr, $newBr).Replace($oldLevel, $newLevel).Replace($oldTryMatch, $newTryMatch).Replace($oldVipExpire, $newVipExpire)
if ($out.IndexOf('plLevel"in t&&"lossless"!==t.plLevel') -lt 0) {
    throw 'Privilege lossless patch did not apply.'
}
if ($out.IndexOf('("plLevel"in t||"playMaxbr"in t||"downloadMaxbr"in t||"maxbr"in t)&&') -lt 0) {
    throw 'Gated playMaxLevel patch did not apply.'
}
if ($out.IndexOf('e.level=999e3===e.br') -lt 0) {
    throw 'tryMatch level/encodeType patch did not apply.'
}
if ($out.IndexOf('Math.floor((t.data.now||new Date().getTime())/864e5)*864e5+316224e5') -lt 0) {
    throw 'LOCAL_VIP day-stable expireTime patch did not apply.'
}

[IO.File]::WriteAllText($patched, $out)
Copy-Item -LiteralPath $patched -Destination $coreJs -Force
Write-Output "wrote $patched"
Write-Output "wrote $coreJs"
Write-Output ("bytes=" + (Get-Item -LiteralPath $coreJs).Length)
