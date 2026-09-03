[CmdletBinding()]
param(
    [switch]$RestoreShortcuts,
    [string]$NcmPath,
    [string[]]$ShortcutPath,
    [string]$UnmSourcePath,
    [switch]$NoLaunch,
    [switch]$Elevated
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$package = [IO.Path]::GetFullPath($PSScriptRoot)
$launcher = Join-Path $package 'ncm-unblock.exe'
$configuration = Join-Path $package 'ncm-unblock.ini'
$coreDirectory = Join-Path $package 'core'
$unm = Join-Path $coreDirectory 'unblockneteasemusic-win-x64.exe'
$stateDirectory = Join-Path $package '.setup-state'
$backupDirectory = Join-Path $stateDirectory 'shortcuts'
$manifestPath = Join-Path $stateDirectory 'shortcuts.json'
$unmUri = 'https://github.com/UnblockNeteaseMusic/server/releases/download/v0.28.0/unblockneteasemusic-win-x64.exe'
$unmSize = 37902453
$ncmVersion = '2.9.7.199711'

function Test-IsAdministrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Restart-Elevated {
    $arguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', ('"{0}"' -f $PSCommandPath), '-Elevated')
    if ($RestoreShortcuts) { $arguments += '-RestoreShortcuts' }
    if ($NoLaunch) { $arguments += '-NoLaunch' }
    if ($NcmPath) { $arguments += @('-NcmPath', ('"{0}"' -f $NcmPath)) }
    if ($UnmSourcePath) { $arguments += @('-UnmSourcePath', ('"{0}"' -f $UnmSourcePath)) }
    $process = Start-Process -FilePath 'powershell.exe' -Verb RunAs -Wait -PassThru -ArgumentList $arguments
    exit $process.ExitCode
}

function Get-ShortcutInfo([string]$Path) {
    $shell = New-Object -ComObject WScript.Shell
    $shortcut = $shell.CreateShortcut($Path)
    return [pscustomobject]@{
        Path = [IO.Path]::GetFullPath($Path)
        TargetPath = $shortcut.TargetPath
    }
}

function Get-NcmCandidates {
    $candidates = [Collections.Generic.List[string]]::new()
    if ($NcmPath) { $candidates.Add($NcmPath) }

    if (Test-Path -LiteralPath $configuration) {
        $inNcm = $false
        foreach ($line in [IO.File]::ReadAllLines($configuration)) {
            $trimmed = $line.Trim()
            if ($trimmed -match '^\[(.+)\]$') {
                $inNcm = $Matches[1] -ieq 'ncm'
            } elseif ($inNcm -and $trimmed -match '^path\s*=\s*(.+)$') {
                $candidates.Add($Matches[1].Trim())
                break
            }
        }
    }

    $shortcutRoots = @(
        [Environment]::GetFolderPath('CommonDesktopDirectory'),
        [Environment]::GetFolderPath('CommonPrograms'),
        [Environment]::GetFolderPath('DesktopDirectory'),
        [Environment]::GetFolderPath('Programs')
    ) | Where-Object { $_ -and (Test-Path -LiteralPath $_) } | Select-Object -Unique
    foreach ($root in $shortcutRoots) {
        foreach ($item in Get-ChildItem -LiteralPath $root -Filter '*.lnk' -File -Recurse -ErrorAction SilentlyContinue) {
            try {
                $info = Get-ShortcutInfo $item.FullName
                if ([IO.Path]::GetFileName($info.TargetPath) -ieq 'cloudmusic.exe') {
                    $candidates.Add($info.TargetPath)
                }
            } catch {}
        }
    }

    foreach ($base in @(${env:ProgramFiles(x86)}, $env:ProgramFiles)) {
        if ($base) { $candidates.Add((Join-Path $base 'Netease\CloudMusic\cloudmusic.exe')) }
    }
    return $candidates | Where-Object { $_ } | Select-Object -Unique
}

function Resolve-NcmPath {
    foreach ($candidate in Get-NcmCandidates) {
        try {
            $fullPath = [IO.Path]::GetFullPath($candidate)
            if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) { continue }
            $item = Get-Item -LiteralPath $fullPath
            if ($item.VersionInfo.FileVersion -ne $ncmVersion) { continue }
            if ((Get-AuthenticodeSignature -LiteralPath $fullPath).Status -ne 'Valid') { continue }
            return $fullPath
        } catch {}
    }
    throw "Signed NetEase Cloud Music $ncmVersion was not found. Re-run setup.ps1 with -NcmPath 'C:\path\cloudmusic.exe'."
}

function Set-NcmConfiguration([string]$Path) {
    if (-not (Test-Path -LiteralPath $configuration -PathType Leaf)) {
        throw 'ncm-unblock.ini is missing from the package.'
    }
    $lines = [Collections.Generic.List[string]]::new()
    $lines.AddRange([string[]][IO.File]::ReadAllLines($configuration))
    $inNcm = $false
    $replaced = $false
    for ($index = 0; $index -lt $lines.Count; ++$index) {
        $trimmed = $lines[$index].Trim()
        if ($trimmed -match '^\[(.+)\]$') {
            $inNcm = $Matches[1] -ieq 'ncm'
        } elseif ($inNcm -and $trimmed -match '^path\s*=') {
            $lines[$index] = "path = $Path"
            $replaced = $true
            break
        }
    }
    if (-not $replaced) { throw 'The [ncm] path entry is missing from ncm-unblock.ini.' }
    [IO.File]::WriteAllLines($configuration, $lines, [Text.UTF8Encoding]::new($false))
}

function Install-Unm {
    [void](New-Item -ItemType Directory -Path $coreDirectory -Force)
    if ((Test-Path -LiteralPath $unm -PathType Leaf) -and (Get-Item -LiteralPath $unm).Length -eq $unmSize) {
        return
    }
    $temporary = Join-Path $coreDirectory 'unblockneteasemusic-win-x64.exe.download'
    try {
        if ($UnmSourcePath) {
            Copy-Item -LiteralPath $UnmSourcePath -Destination $temporary -Force
        } else {
            Invoke-WebRequest -UseBasicParsing -Uri $unmUri -OutFile $temporary
        }
        if ((Get-Item -LiteralPath $temporary).Length -ne $unmSize) {
            throw 'The downloaded UNM file does not match the expected official v0.28.0 asset size.'
        }
        Move-Item -LiteralPath $temporary -Destination $unm -Force
    } finally {
        if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Force }
    }
}

function Get-ManagedShortcutPaths([string]$ResolvedNcmPath) {
    if ($ShortcutPath) {
        return $ShortcutPath | ForEach-Object { [IO.Path]::GetFullPath($_) } | Select-Object -Unique
    }
    $roots = @(
        [Environment]::GetFolderPath('CommonDesktopDirectory'),
        [Environment]::GetFolderPath('CommonPrograms'),
        [Environment]::GetFolderPath('DesktopDirectory'),
        [Environment]::GetFolderPath('Programs')
    ) | Where-Object { $_ -and (Test-Path -LiteralPath $_) } | Select-Object -Unique
    $paths = [Collections.Generic.List[string]]::new()
    foreach ($root in $roots) {
        foreach ($item in Get-ChildItem -LiteralPath $root -Filter '*.lnk' -File -Recurse -ErrorAction SilentlyContinue) {
            try {
                $info = Get-ShortcutInfo $item.FullName
                $targetName = [IO.Path]::GetFileName($info.TargetPath)
                if ($info.TargetPath -ieq $ResolvedNcmPath -or $targetName -ieq 'ncm-unblock.exe') {
                    $paths.Add($item.FullName)
                }
            } catch {}
        }
    }
    if ($paths.Count -eq 0) {
        $paths.Add((Join-Path ([Environment]::GetFolderPath('DesktopDirectory')) '网易云音乐（解锁）.lnk'))
        $paths.Add((Join-Path ([Environment]::GetFolderPath('Programs')) '网易云音乐（解锁）.lnk'))
    }
    return $paths | Select-Object -Unique
}

function Install-Shortcuts([string]$ResolvedNcmPath) {
    $paths = @(Get-ManagedShortcutPaths $ResolvedNcmPath)
    [void](New-Item -ItemType Directory -Path $backupDirectory -Force)
    $records = @()
    if (Test-Path -LiteralPath $manifestPath) {
        $records = @((Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json))
    }
    foreach ($path in $paths) {
        $existing = @($records | Where-Object { $_.Path -ieq $path })
        if ($existing.Count -ne 0) { continue }
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            $backupName = ('{0:D3}.lnk' -f $records.Count)
            Copy-Item -LiteralPath $path -Destination (Join-Path $backupDirectory $backupName)
            $records += [pscustomobject]@{ Path = $path; Backup = $backupName; Created = $false }
        } else {
            $records += [pscustomobject]@{ Path = $path; Backup = ''; Created = $true }
        }
    }
    [void](New-Item -ItemType Directory -Path $stateDirectory -Force)
    [IO.File]::WriteAllText($manifestPath, ($records | ConvertTo-Json -Depth 3), [Text.UTF8Encoding]::new($false))

    $shell = New-Object -ComObject WScript.Shell
    foreach ($path in $paths) {
        [void](New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($path)) -Force)
        $shortcut = $shell.CreateShortcut($path)
        $shortcut.TargetPath = $launcher
        $shortcut.Arguments = ''
        $shortcut.WorkingDirectory = $package
        $shortcut.IconLocation = "$ResolvedNcmPath,0"
        $shortcut.Description = 'NetEase Cloud Music 2.9.7 with UnblockNeteaseMusic'
        $shortcut.Save()
    }
}

function Restore-ManagedShortcuts {
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        Write-Output 'No managed shortcut backup was found.'
        return
    }
    $records = @((Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json))
    foreach ($record in $records) {
        if ($record.Created) {
            if (Test-Path -LiteralPath $record.Path) { Remove-Item -LiteralPath $record.Path -Force }
        } else {
            $backup = Join-Path $backupDirectory $record.Backup
            if (-not (Test-Path -LiteralPath $backup -PathType Leaf)) {
                throw "Shortcut backup is missing: $backup"
            }
            Copy-Item -LiteralPath $backup -Destination $record.Path -Force
        }
    }
    Write-Output 'Original NetEase Cloud Music shortcuts were restored.'
}

try {
    if (-not $Elevated -and -not $ShortcutPath -and -not (Test-IsAdministrator)) {
        Restart-Elevated
    }
    if ($RestoreShortcuts) {
        Restore-ManagedShortcuts
        exit 0
    }
    if (-not (Test-Path -LiteralPath $launcher -PathType Leaf)) {
        throw 'ncm-unblock.exe is missing. Run setup from the extracted package directory.'
    }
    $resolvedNcmPath = Resolve-NcmPath
    Install-Unm
    Set-NcmConfiguration $resolvedNcmPath
    Install-Shortcuts $resolvedNcmPath
    Write-Output "Setup complete. Managed shortcuts now start: $launcher"
    Write-Output 'In NCM, set Settings > Tools > HTTP proxy > Custom proxy to 127.0.0.1:3412 once.'
    if (-not $NoLaunch) { Start-Process -FilePath $launcher }
    exit 0
} catch {
    Write-Error $_
    exit 1
}
