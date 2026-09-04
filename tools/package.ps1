[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$outputRoot = [IO.Path]::GetFullPath((Join-Path $repository 'out'))
$packageName = 'unblock-lite-0.1.2-x64'
$stage = [IO.Path]::GetFullPath((Join-Path $outputRoot $packageName))
$pluginStage = [IO.Path]::GetFullPath((Join-Path $stage 'plugin-root'))
$pluginFile = [IO.Path]::GetFullPath((Join-Path $stage 'UnblockLite.plugin'))
$archive = [IO.Path]::GetFullPath((Join-Path $outputRoot ($packageName + '.zip')))
$hostExe = [IO.Path]::GetFullPath((Join-Path $repository 'build\x64-release\src\host\unm-host.exe'))
$expectedStagePrefix = $outputRoot.TrimEnd(
    [IO.Path]::DirectorySeparatorChar,
    [IO.Path]::AltDirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar

if (-not $stage.StartsWith($expectedStagePrefix, [StringComparison]::OrdinalIgnoreCase) -or
    [IO.Path]::GetFileName($stage) -ne $packageName -or
    [IO.Path]::GetDirectoryName($archive) -ine $outputRoot -or
    [IO.Path]::GetFileName($archive) -ne ($packageName + '.zip') -or
    [IO.Path]::GetFileName($pluginFile) -ne 'UnblockLite.plugin') {
    throw 'Package output paths failed workspace-bound validation.'
}

& (Join-Path $PSScriptRoot 'build.ps1') -Configuration Release
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

if (-not (Test-Path -LiteralPath $hostExe)) {
    throw "Release host was not built: $hostExe"
}

[void](New-Item -ItemType Directory -Path $outputRoot -Force)
if (Test-Path -LiteralPath $stage) {
    Remove-Item -LiteralPath $stage -Recurse
}
if (Test-Path -LiteralPath $archive) {
    Remove-Item -LiteralPath $archive
}

[void](New-Item -ItemType Directory -Path $pluginStage -Force)
Copy-Item -LiteralPath (Join-Path $repository 'plugin\manifest.json') -Destination (Join-Path $pluginStage 'manifest.json')
Copy-Item -LiteralPath (Join-Path $repository 'plugin\main.js') -Destination (Join-Path $pluginStage 'main.js')
Copy-Item -LiteralPath (Join-Path $repository 'core\README.txt') -Destination (Join-Path $pluginStage 'README.txt')
[void](New-Item -ItemType Directory -Path (Join-Path $pluginStage 'native') -Force)
Copy-Item -LiteralPath $hostExe -Destination (Join-Path $pluginStage 'native\unm-host.exe')
[void](New-Item -ItemType Directory -Path (Join-Path $pluginStage 'core') -Force)
Copy-Item -LiteralPath (Join-Path $repository 'core\README.txt') -Destination (Join-Path $pluginStage 'core\README.txt')
Copy-Item -LiteralPath (Join-Path $repository 'README.md') -Destination (Join-Path $stage 'README.md')

# BetterNCM loads only *.plugin zip archives whose entries are rooted at the archive root.
if (Test-Path -LiteralPath $pluginFile) {
    Remove-Item -LiteralPath $pluginFile
}
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::CreateFromDirectory(
    $pluginStage,
    $pluginFile,
    [System.IO.Compression.CompressionLevel]::Optimal,
    $false)

$expectedPluginEntries = @(
    'core/README.txt',
    'main.js',
    'manifest.json',
    'native/unm-host.exe',
    'README.txt'
) | Sort-Object

$pluginEntries = @()
$zip = [System.IO.Compression.ZipFile]::OpenRead($pluginFile)
try {
    foreach ($entry in $zip.Entries) {
        if ($entry.FullName.EndsWith('/')) {
            continue
        }
        $pluginEntries += $entry.FullName.Replace('\', '/')
    }
} finally {
    $zip.Dispose()
}
$pluginEntries = @($pluginEntries | Sort-Object)
if (Compare-Object -ReferenceObject $expectedPluginEntries -DifferenceObject $pluginEntries) {
    throw 'UnblockLite.plugin contents differ from the explicit plugin manifest.'
}

$expectedFiles = @(
    'README.md',
    'UnblockLite.plugin'
) | Sort-Object
$stagePrefix = $stage.TrimEnd(
    [IO.Path]::DirectorySeparatorChar,
    [IO.Path]::AltDirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
$actualFiles = @(Get-ChildItem -LiteralPath $stage -File | ForEach-Object {
        if (-not $_.FullName.StartsWith($stagePrefix, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Installed file escaped the package stage: $($_.FullName)"
        }
        $_.FullName.Substring($stagePrefix.Length).Replace('\', '/')
    } | Sort-Object)
if (Compare-Object -ReferenceObject $expectedFiles -DifferenceObject $actualFiles) {
    throw 'Installed package contents differ from the explicit runtime manifest.'
}

Compress-Archive -LiteralPath @(
    (Join-Path $stage 'README.md'),
    $pluginFile
) -DestinationPath $archive -CompressionLevel Optimal

$archiveItem = Get-Item -LiteralPath $archive
Write-Output "package=$($archiveItem.FullName)"
Write-Output "plugin=$pluginFile"
Write-Output "bytes=$($archiveItem.Length)"
