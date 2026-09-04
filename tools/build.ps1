[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',

    [switch]$SkipTests
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$vsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vsWhere)) {
    throw 'Visual Studio Installer vswhere.exe was not found.'
}

$installationPath = & $vsWhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $installationPath) {
    throw 'Visual Studio 2022 C++ x86/x64 build tools were not found.'
}

$vsDevCmd = Join-Path $installationPath 'Common7\Tools\VsDevCmd.bat'
if (-not (Test-Path -LiteralPath $vsDevCmd)) {
    throw "VsDevCmd.bat was not found under $installationPath."
}

$environment = & $env:ComSpec /d /s /c `
    ('"' + $vsDevCmd + '" -no_logo -arch=x64 -host_arch=x64 >nul && set')
if ($LASTEXITCODE -ne 0) {
    throw 'Failed to activate the Visual Studio x64 build environment.'
}

foreach ($line in $environment) {
    if ($line -match '^([^=]+)=(.*)$') {
        Set-Item -Path ('Env:' + $matches[1]) -Value $matches[2]
    }
}

$preset = 'x64-' + $Configuration.ToLowerInvariant()

& cmake --preset $preset
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

& cmake --build --preset $preset --parallel
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

if (-not $SkipTests) {
    & ctest --preset $preset
    exit $LASTEXITCODE
}
