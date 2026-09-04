[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$outputRoot = [IO.Path]::GetFullPath((Join-Path $repository 'out'))
$packageName = 'unblock-lite-0.1.0-x64'
$stage = [IO.Path]::GetFullPath((Join-Path $outputRoot $packageName))
$archive = [IO.Path]::GetFullPath((Join-Path $outputRoot ($packageName + '.zip')))
$expectedStagePrefix = $outputRoot.TrimEnd(
    [IO.Path]::DirectorySeparatorChar,
    [IO.Path]::AltDirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar

if (-not $stage.StartsWith($expectedStagePrefix, [StringComparison]::OrdinalIgnoreCase) -or
    [IO.Path]::GetFileName($stage) -ne $packageName -or
    [IO.Path]::GetDirectoryName($archive) -ine $outputRoot -or
    [IO.Path]::GetFileName($archive) -ne ($packageName + '.zip')) {
    throw 'Package output paths failed workspace-bound validation.'
}

& (Join-Path $PSScriptRoot 'build.ps1') -Configuration Release
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

[void](New-Item -ItemType Directory -Path $outputRoot -Force)
if (Test-Path -LiteralPath $stage) {
    Remove-Item -LiteralPath $stage -Recurse
}
if (Test-Path -LiteralPath $archive) {
    Remove-Item -LiteralPath $archive
}

& cmake --install (Join-Path $repository 'build\x64-release') `
    --prefix $stage --component Runtime
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$expectedFiles = @(
    'README.md',
    'UnblockLite/core/README.txt',
    'UnblockLite/main.js',
    'UnblockLite/manifest.json',
    'UnblockLite/native/unm-host.exe'
) | Sort-Object
$actualFiles = @(Get-ChildItem -LiteralPath $stage -File -Recurse | ForEach-Object {
        [IO.Path]::GetRelativePath($stage, $_.FullName).Replace('\', '/')
    } | Sort-Object)
if (Compare-Object -ReferenceObject $expectedFiles -DifferenceObject $actualFiles) {
    throw 'Installed package contents differ from the explicit runtime manifest.'
}

Compress-Archive -LiteralPath $stage -DestinationPath $archive -CompressionLevel Optimal
$archiveItem = Get-Item -LiteralPath $archive
Write-Output "package=$($archiveItem.FullName)"
Write-Output "bytes=$($archiveItem.Length)"
