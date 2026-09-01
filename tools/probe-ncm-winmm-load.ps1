[CmdletBinding()]
param(
    [string]$NcmPath = 'D:\Program Files (x86)\Netease\CloudMusic\cloudmusic.exe',

    [string]$ProbePath = (Join-Path $PSScriptRoot '..\build\win32-release\src\module_load_probe\ncm_module_load_probe.exe'),

    [ValidateRange(100, 60000)]
    [int]$TimeoutMilliseconds = 5000
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Get-OrdinaryFile {
    param(
        [Parameter(Mandatory)]
        [string]$LiteralPath,

        [Parameter(Mandatory)]
        [string]$Description
    )

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

function Assert-ValidSignature {
    param(
        [Parameter(Mandatory)]
        [System.IO.FileInfo]$File,

        [Parameter(Mandatory)]
        [string]$Description
    )

    $signature = Get-AuthenticodeSignature -LiteralPath $File.FullName
    if ($signature.Status -ne [System.Management.Automation.SignatureStatus]::Valid) {
        throw "$Description does not have a valid Authenticode signature: $($signature.Status)"
    }
}

function Set-PrivateDirectoryAcl {
    param(
        [Parameter(Mandatory)]
        [System.IO.DirectoryInfo]$Directory
    )

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

function Invoke-LoadProbe {
    param(
        [Parameter(Mandatory)]
        [string]$Target,

        [Parameter(Mandatory)]
        [string]$ExpectedModule
    )

    $output = & $script:probe.FullName $Target 'winmm.dll' $ExpectedModule `
        $TimeoutMilliseconds 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "module-load probe failed with exit code $LASTEXITCODE`: $($output -join ' ')"
    }
    return $output
}

function Remove-OwnedProbeDirectory {
    param(
        [Parameter(Mandatory)]
        [System.IO.DirectoryInfo]$Root,

        [Parameter(Mandatory)]
        [string[]]$AllowedPaths,

        [Parameter(Mandatory)]
        [string[]]$DirectoriesDeepestFirst
    )

    if (($Root.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "refusing cleanup because the owned root became a reparse point: $($Root.FullName)"
    }
    $allowed = [System.Collections.Generic.HashSet[string]]::new(
        [System.StringComparer]::OrdinalIgnoreCase)
    foreach ($path in $AllowedPaths) {
        [void]$allowed.Add([System.IO.Path]::GetFullPath($path))
    }
    $observed = @(Get-ChildItem -LiteralPath $Root.FullName -Force -Recurse)
    $unknown = @($observed | Where-Object {
        -not $allowed.Contains([System.IO.Path]::GetFullPath($_.FullName))
    })
    if ($unknown.Count -ne 0) {
        throw "owned probe directory contains unknown material and was preserved: $($Root.FullName)"
    }
    $reparse = @($observed | Where-Object {
        ($_.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0
    })
    if ($reparse.Count -ne 0) {
        throw "owned probe directory contains a reparse point and was preserved: $($Root.FullName)"
    }

    foreach ($path in $AllowedPaths) {
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            Remove-Item -LiteralPath $path -Force
        }
    }
    foreach ($path in $DirectoriesDeepestFirst) {
        if (Test-Path -LiteralPath $path -PathType Container) {
            Remove-Item -LiteralPath $path -Force
        }
    }
}

$script:probe = Get-OrdinaryFile -LiteralPath $ProbePath -Description 'module-load probe'
$ncm = Get-OrdinaryFile -LiteralPath $NcmPath -Description 'NCM target'
if ($ncm.VersionInfo.FileVersion -ne '2.9.7.199711') {
    throw "unsupported NCM version: $($ncm.VersionInfo.FileVersion)"
}
Assert-ValidSignature -File $ncm -Description 'NCM target'

$runningNcm = @(Get-CimInstance Win32_Process -Filter "Name = 'cloudmusic.exe'")
if ($runningNcm.Count -ne 0) {
    $processIds = ($runningNcm.ProcessId | Sort-Object) -join ', '
    throw "refusing the loader experiment while pre-existing cloudmusic.exe processes are active: $processIds"
}

$systemWinmmPath = Join-Path $env:WINDIR 'SysWOW64\winmm.dll'
$systemWinmm = Get-OrdinaryFile -LiteralPath $systemWinmmPath -Description 'x86 system WinMM'
if ($systemWinmm.VersionInfo.OriginalFilename -ine 'WINMM.DLL') {
    throw "unexpected x86 system WinMM identity: $($systemWinmm.VersionInfo.OriginalFilename)"
}
Assert-ValidSignature -File $systemWinmm -Description 'x86 system WinMM'

$temporaryParent = [System.IO.Path]::GetFullPath($env:TEMP)
$rootPath = Join-Path $temporaryParent (
    'ncm-unblock-297-winmm-probe-' + [guid]::NewGuid().ToString('N'))
$environmentNames = @('LOCALAPPDATA', 'APPDATA', 'TEMP', 'TMP')
$savedEnvironment = @{}
$environmentChanged = $false
$root = $null
$allowedPaths = @()
$directoriesDeepestFirst = @()
$runError = $null
$controlOutput = $null
$treatmentOutput = $null
try {
    $root = New-Item -ItemType Directory -Path $rootPath
    $directoriesDeepestFirst = @($root.FullName)
    Set-PrivateDirectoryAcl -Directory $root

    $controlDirectory = Join-Path $root.FullName 'control'
    $treatmentDirectory = Join-Path $root.FullName 'treatment'
    $profileDirectory = Join-Path $root.FullName 'profile'
    $localAppDataDirectory = Join-Path $profileDirectory 'Local'
    $appDataDirectory = Join-Path $profileDirectory 'Roaming'
    $processTempDirectory = Join-Path $profileDirectory 'Temp'
    $controlTarget = Join-Path $controlDirectory 'cloudmusic.exe'
    $treatmentTarget = Join-Path $treatmentDirectory 'cloudmusic.exe'
    $treatmentWinmm = Join-Path $treatmentDirectory 'winmm.dll'
    $allowedPaths = @(
        $controlDirectory, $treatmentDirectory, $profileDirectory,
        $localAppDataDirectory, $appDataDirectory, $processTempDirectory,
        $controlTarget, $treatmentTarget, $treatmentWinmm)
    $directoriesDeepestFirst = @(
        $localAppDataDirectory, $appDataDirectory, $processTempDirectory,
        $controlDirectory, $treatmentDirectory, $profileDirectory, $root.FullName)

    foreach ($directory in @(
        $controlDirectory, $treatmentDirectory, $profileDirectory,
        $localAppDataDirectory, $appDataDirectory, $processTempDirectory)) {
        [void](New-Item -ItemType Directory -Path $directory)
    }
    Copy-Item -LiteralPath $ncm.FullName -Destination $controlTarget
    Copy-Item -LiteralPath $ncm.FullName -Destination $treatmentTarget
    Copy-Item -LiteralPath $systemWinmm.FullName -Destination $treatmentWinmm

    $controlTargetItem = Get-OrdinaryFile -LiteralPath $controlTarget -Description 'control NCM copy'
    $treatmentTargetItem = Get-OrdinaryFile -LiteralPath $treatmentTarget -Description 'treatment NCM copy'
    $treatmentWinmmItem = Get-OrdinaryFile -LiteralPath $treatmentWinmm -Description 'treatment WinMM copy'
    foreach ($copy in @($controlTargetItem, $treatmentTargetItem)) {
        if ($copy.Length -ne $ncm.Length -or $copy.VersionInfo.FileVersion -ne '2.9.7.199711') {
            throw "isolated NCM copy identity check failed: $($copy.FullName)"
        }
        Assert-ValidSignature -File $copy -Description 'isolated NCM copy'
    }
    if ($treatmentWinmmItem.Length -ne $systemWinmm.Length) {
        throw 'isolated WinMM copy length check failed'
    }
    Assert-ValidSignature -File $treatmentWinmmItem -Description 'isolated WinMM copy'

    foreach ($name in $environmentNames) {
        $entry = Get-Item -LiteralPath "Env:$name" -ErrorAction SilentlyContinue
        $savedEnvironment[$name] = if ($null -eq $entry) { $null } else { $entry.Value }
    }
    $environmentChanged = $true
    $env:LOCALAPPDATA = $localAppDataDirectory
    $env:APPDATA = $appDataDirectory
    $env:TEMP = $processTempDirectory
    $env:TMP = $processTempDirectory

    $controlOutput = Invoke-LoadProbe -Target $controlTarget `
        -ExpectedModule $systemWinmm.FullName
    $treatmentOutput = Invoke-LoadProbe -Target $treatmentTarget `
        -ExpectedModule $treatmentWinmm
} catch {
    $runError = $_
} finally {
    $cleanupErrors = [System.Collections.Generic.List[string]]::new()
    if ($environmentChanged) {
        foreach ($name in $environmentNames) {
            try {
                if ($null -eq $savedEnvironment[$name]) {
                    Remove-Item -LiteralPath "Env:$name" -ErrorAction Stop
                } else {
                    Set-Item -LiteralPath "Env:$name" -Value $savedEnvironment[$name]
                }
            } catch {
                if ($null -ne $savedEnvironment[$name] -or
                    (Test-Path -LiteralPath "Env:$name")) {
                    $cleanupErrors.Add(
                        "failed to restore environment variable $name`: $($_.Exception.Message)")
                }
            }
        }
    }

    if ($null -ne $root -and (Test-Path -LiteralPath $root.FullName -PathType Container)) {
        try {
            Remove-OwnedProbeDirectory -Root $root -AllowedPaths $allowedPaths `
                -DirectoriesDeepestFirst $directoriesDeepestFirst
        } catch {
            $cleanupErrors.Add("probe-directory cleanup failed: $($_.Exception.Message)")
        }
    }
    if ($cleanupErrors.Count -ne 0) {
        $cleanupMessage = $cleanupErrors -join '; '
        if ($null -ne $runError) {
            throw "$($runError.Exception.Message); cleanup failure: $cleanupMessage"
        }
        throw "cleanup failure: $cleanupMessage"
    }
}

if ($null -ne $runError) {
    throw $runError
}

Write-Output 'control (system WinMM):'
$controlOutput | ForEach-Object { Write-Output "  $_" }
Write-Output 'treatment (application-directory WinMM):'
$treatmentOutput | ForEach-Object { Write-Output "  $_" }
