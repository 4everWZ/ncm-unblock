[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$RecoveryPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Test-ByteArrayEqual {
    param([byte[]]$Left, [byte[]]$Right)

    if ($Left.Length -ne $Right.Length) {
        return $false
    }
    for ($index = 0; $index -lt $Left.Length; ++$index) {
        if ($Left[$index] -ne $Right[$index]) {
            return $false
        }
    }
    return $true
}

function Read-FileExclusively {
    param([string]$Path)

    $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::None)
    try {
        if ($stream.Length -gt [int]::MaxValue) {
            throw 'Recovery source is unexpectedly large.'
        }
        $bytes = [byte[]]::new([int]$stream.Length)
        $offset = 0
        while ($offset -lt $bytes.Length) {
            $read = $stream.Read($bytes, $offset, $bytes.Length - $offset)
            if ($read -eq 0) {
                throw 'Recovery source ended before the snapshot was complete.'
            }
            $offset += $read
        }
        return ,$bytes
    } finally {
        $stream.Dispose()
    }
}

function Wait-AllNcmExit {
    param([int]$TimeoutSeconds)

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $emptySnapshots = 0
    do {
        if (@(Get-Process -Name cloudmusic, cloudmusic_reporter -ErrorAction SilentlyContinue).Count -eq 0) {
            ++$emptySnapshots
            if ($emptySnapshots -ge 3) {
                return $true
            }
        } else {
            $emptySnapshots = 0
        }
        Start-Sleep -Milliseconds 250
    } while ([DateTime]::UtcNow -lt $deadline)
    return $false
}

function Assert-PrivateSiblingPath {
    param(
        [string]$Path,
        [string]$ExpectedParent,
        [string]$ExpectedPrefix
    )

    $resolved = [IO.Path]::GetFullPath($Path)
    if ([IO.Path]::GetDirectoryName($resolved) -ine $ExpectedParent -or
        [IO.Path]::GetFileName($resolved) -notmatch ('^' + [regex]::Escape($ExpectedPrefix) + '[0-9a-f]{32}$')) {
        throw "Recovery manifest contains an invalid private sibling path: $resolved"
    }
    return $resolved
}

function Assert-PrivateRecoveryDirectory {
    param([IO.DirectoryInfo]$Directory)

    if (($Directory.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw 'RecoveryPath must not be a reparse point.'
    }
    $directoryAcl = Get-Acl -LiteralPath $Directory.FullName
    $currentSid = [Security.Principal.WindowsIdentity]::GetCurrent().User
    $systemSid = [Security.Principal.SecurityIdentifier]::new('S-1-5-18')
    $ownerSid = $directoryAcl.GetOwner([Security.Principal.SecurityIdentifier])
    if (-not $directoryAcl.AreAccessRulesProtected -or
        $ownerSid -ne $currentSid) {
        throw 'RecoveryPath does not have the expected private owner and protected DACL.'
    }
    $rules = @($directoryAcl.GetAccessRules(
            $true, $false, [Security.Principal.SecurityIdentifier]))
    if ($rules.Count -ne 2) {
        throw 'RecoveryPath does not have the expected two-rule private DACL.'
    }
    foreach ($expectedSid in @($currentSid, $systemSid)) {
        $matching = @($rules | Where-Object {
                $_.IdentityReference -eq $expectedSid -and
                $_.AccessControlType -eq [Security.AccessControl.AccessControlType]::Allow -and
                $_.FileSystemRights -eq [Security.AccessControl.FileSystemRights]::FullControl -and
                $_.InheritanceFlags -eq [Security.AccessControl.InheritanceFlags]'ContainerInherit, ObjectInherit' -and
                $_.PropagationFlags -eq [Security.AccessControl.PropagationFlags]::None
            })
        if ($matching.Count -ne 1) {
            throw 'RecoveryPath private DACL does not match the experiment contract.'
        }
    }
}

function Assert-NoReparsePath {
    param(
        [string]$Path,
        [string]$BasePath
    )

    $resolvedPath = [IO.Path]::GetFullPath($Path)
    $resolvedBase = [IO.Path]::GetFullPath($BasePath).TrimEnd([IO.Path]::DirectorySeparatorChar)
    if ($resolvedPath -ine $resolvedBase -and
        -not $resolvedPath.StartsWith($resolvedBase + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Recovery target resolved outside the expected user-local directory.'
    }
    $cursor = $resolvedPath
    for (;;) {
        $item = Get-Item -LiteralPath $cursor
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Recovery target path must not traverse a reparse point: $cursor"
        }
        if ($cursor -ieq $resolvedBase) {
            break
        }
        $cursor = [IO.Path]::GetDirectoryName($cursor)
    }
}

$tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd([IO.Path]::DirectorySeparatorChar)
$resolvedRecoveryPath = [IO.Path]::GetFullPath((Resolve-Path -LiteralPath $RecoveryPath).Path)
if ([IO.Path]::GetDirectoryName($resolvedRecoveryPath) -ine $tempRoot -or
    [IO.Path]::GetFileName($resolvedRecoveryPath) -notmatch '^ncm-proxy-experiment-[0-9a-f]{32}$') {
    throw 'RecoveryPath must be one exact ncm-proxy-experiment directory directly under the system temporary directory.'
}
$recoveryDirectory = Get-Item -LiteralPath $resolvedRecoveryPath
if (-not $recoveryDirectory.PSIsContainer) {
    throw 'RecoveryPath is not a directory.'
}
Assert-PrivateRecoveryDirectory -Directory $recoveryDirectory

$manifestPath = Join-Path $resolvedRecoveryPath 'recovery.json'
$backupPath = Join-Path $resolvedRecoveryPath 'localdata.backup'
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf) -or
    -not (Test-Path -LiteralPath $backupPath -PathType Leaf)) {
    throw 'The recovery manifest or private backup is missing.'
}
foreach ($bundleFile in @($manifestPath, $backupPath)) {
    $bundleItem = Get-Item -LiteralPath $bundleFile
    if (($bundleItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Recovery bundle file must not be a reparse point: $bundleFile"
    }
}

$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$bundleId = [IO.Path]::GetFileName($resolvedRecoveryPath).Substring('ncm-proxy-experiment-'.Length)
if ([int]$manifest.SchemaVersion -ne 1 -or
    [string]$manifest.BundleId -cne $bundleId -or
    [string]$manifest.SecurityDescriptorScope -cne 'owner-group-dacl') {
    throw 'The recovery manifest schema, bundle identity, or security scope is invalid.'
}
$expectedTargetPath = [IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA 'Netease\CloudMusic\localdata'))
$targetPath = [IO.Path]::GetFullPath([string]$manifest.TargetPath)
if ($targetPath -ine $expectedTargetPath) {
    throw 'The recovery manifest does not target this user profile localdata file.'
}
$targetParent = [IO.Path]::GetDirectoryName($targetPath)
Assert-NoReparsePath -Path $targetParent -BasePath $env:LOCALAPPDATA
$restoreSiblingPath = Assert-PrivateSiblingPath `
    -Path ([string]$manifest.RestoreSiblingPath) `
    -ExpectedParent $targetParent `
    -ExpectedPrefix 'localdata.codex-restore-'
$displacedPath = Assert-PrivateSiblingPath `
    -Path ([string]$manifest.DisplacedPath) `
    -ExpectedParent $targetParent `
    -ExpectedPrefix 'localdata.codex-displaced-'
if ([IO.Path]::GetFileName($restoreSiblingPath) -cne ('localdata.codex-restore-' + $bundleId) -or
    [IO.Path]::GetFileName($displacedPath) -cne ('localdata.codex-displaced-' + $bundleId)) {
    throw 'Private sibling paths do not belong to this recovery bundle.'
}

if (-not (Wait-AllNcmExit -TimeoutSeconds 2)) {
    throw 'Every NCM instance must be stopped before private-state recovery.'
}

$backupBytes = Read-FileExclusively -Path $backupPath
if ($backupBytes.Length -ne [int64]$manifest.Length) {
    throw 'The private backup length does not match its recovery manifest.'
}
$sha256 = [Security.Cryptography.SHA256]::Create()
try {
    $backupSha256 = -join @($sha256.ComputeHash($backupBytes) | ForEach-Object { $_.ToString('x2') })
} finally {
    $sha256.Dispose()
}
if ($backupSha256 -cne [string]$manifest.BackupSha256) {
    throw 'The private backup does not match the recovery manifest integrity record.'
}
$creationTimeUtc = [DateTime]::Parse(
    [string]$manifest.CreationTimeUtc, [Globalization.CultureInfo]::InvariantCulture,
    [Globalization.DateTimeStyles]::RoundtripKind)
$lastWriteTimeUtc = [DateTime]::Parse(
    [string]$manifest.LastWriteTimeUtc, [Globalization.CultureInfo]::InvariantCulture,
    [Globalization.DateTimeStyles]::RoundtripKind)
$attributes = [IO.FileAttributes][int]$manifest.Attributes
$acl = [Security.AccessControl.FileSecurity]::new()
$acl.SetSecurityDescriptorSddlForm(
    [string]$manifest.Sddl,
    [Security.AccessControl.AccessControlSections]'Owner, Group, Access')

$targetExists = Test-Path -LiteralPath $targetPath -PathType Leaf
$needsRestore = -not $targetExists
if ($targetExists) {
    $targetBytes = [IO.File]::ReadAllBytes($targetPath)
    $targetItem = Get-Item -LiteralPath $targetPath
    if (($targetItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw 'The localdata recovery target must not be a reparse point.'
    }
    $targetAcl = Get-Acl -LiteralPath $targetPath
    $needsRestore = -not (Test-ByteArrayEqual -Left $backupBytes -Right $targetBytes) -or
        $targetItem.CreationTimeUtc -ne $creationTimeUtc -or
        $targetItem.LastWriteTimeUtc -ne $lastWriteTimeUtc -or
        $targetItem.Attributes -ne $attributes -or
        $targetAcl.Sddl -cne [string]$manifest.Sddl
}

if ($needsRestore) {
    foreach ($privateSibling in @($restoreSiblingPath, $displacedPath)) {
        if (Test-Path -LiteralPath $privateSibling) {
            throw "Private recovery sibling already exists; refusing to overwrite or delete it: $privateSibling"
        }
    }

    [IO.File]::WriteAllBytes($restoreSiblingPath, $backupBytes)
    $flush = [IO.File]::Open($restoreSiblingPath, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    try {
        $flush.Flush($true)
    } finally {
        $flush.Dispose()
    }

    if (-not (Wait-AllNcmExit -TimeoutSeconds 1)) {
        throw 'An NCM process appeared before atomic recovery.'
    }
    if ($targetExists) {
        [IO.File]::Replace($restoreSiblingPath, $targetPath, $displacedPath, $true)
    } else {
        [IO.File]::Move($restoreSiblingPath, $targetPath)
    }
    Set-Acl -LiteralPath $targetPath -AclObject $acl
    [IO.File]::SetCreationTimeUtc($targetPath, $creationTimeUtc)
    [IO.File]::SetLastWriteTimeUtc($targetPath, $lastWriteTimeUtc)
    [IO.File]::SetAttributes($targetPath, $attributes)
}

if (-not (Wait-AllNcmExit -TimeoutSeconds 1)) {
    throw 'An NCM process appeared during recovery verification.'
}
$restoredBytes = [IO.File]::ReadAllBytes($targetPath)
$restoredItem = Get-Item -LiteralPath $targetPath
$restoredAcl = Get-Acl -LiteralPath $targetPath
$verified = (Test-ByteArrayEqual -Left $backupBytes -Right $restoredBytes) -and
    $restoredItem.CreationTimeUtc -eq $creationTimeUtc -and
    $restoredItem.LastWriteTimeUtc -eq $lastWriteTimeUtc -and
    $restoredItem.Attributes -eq $attributes -and
    $restoredAcl.Sddl -ceq [string]$manifest.Sddl
if (-not $verified) {
    throw 'localdata recovery verification failed; recovery material was retained.'
}

$cleanupPaths = @(
    $restoreSiblingPath,
    $displacedPath,
    (Join-Path $resolvedRecoveryPath 'observer.stdout'),
    (Join-Path $resolvedRecoveryPath 'observer.stderr'),
    $backupPath,
    $manifestPath)
foreach ($path in $cleanupPaths) {
    if (Test-Path -LiteralPath $path -PathType Leaf) {
        Remove-Item -LiteralPath $path
    }
}
Remove-Item -LiteralPath $resolvedRecoveryPath -ErrorAction SilentlyContinue

$retained = Test-Path -LiteralPath $resolvedRecoveryPath
Write-Output 'localdata-restored=true'
Write-Output "recovery-directory-retained=$($retained.ToString().ToLowerInvariant())"
if ($retained) {
    Write-Warning 'Unknown files remain in the recovery directory; they were not removed.'
}
