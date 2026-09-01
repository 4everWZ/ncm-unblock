[CmdletBinding()]
param(
    [string]$Configuration = 'Release',

    [ValidateRange(1000, 120000)]
    [int]$TimeoutMilliseconds = 30000
)

# Exercises the production WinMM proxy against this host's real system module.
#
# The proxy is staged into a private temporary directory beside a copy of the
# probe, because the searched application directory is the directory of the
# running image. Nothing is installed and no NCM process is involved.

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$build = Join-Path $PSScriptRoot "..\build\win32-$($Configuration.ToLowerInvariant())"
$proxy = Join-Path $build 'src\winmm_proxy\winmm.dll'
$probe = Join-Path $build 'tests\winmm_forwarder_probe.exe'
foreach ($required in @($proxy, $probe)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required artifact was not built: $required. Run tools/build.ps1 -Configuration $Configuration first."
    }
}

$stage = Join-Path ([System.IO.Path]::GetTempPath()) ("ncm-unblock-297-winmm-system-" + [System.Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stage | Out-Null
try {
    $stagedProbe = Join-Path $stage 'winmm_forwarder_probe.exe'
    $stagedProxy = Join-Path $stage 'winmm.dll'
    $report = Join-Path $stage 'report.txt'
    Copy-Item -LiteralPath $probe -Destination $stagedProbe
    Copy-Item -LiteralPath $proxy -Destination $stagedProxy

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $stagedProbe
    # Windows PowerShell has no ArgumentList on ProcessStartInfo; none of these
    # staged paths contain a quote or a trailing backslash.
    $startInfo.Arguments = "systembackend `"$stagedProxy`" `"$report`""
    $startInfo.UseShellExecute = $false
    $startInfo.WorkingDirectory = $stage

    $process = [System.Diagnostics.Process]::Start($startInfo)
    try {
        if (-not $process.WaitForExit($TimeoutMilliseconds)) {
            $process.Kill($true)
            throw 'The system-backend probe did not finish within its budget.'
        }
        $exitCode = $process.ExitCode
    }
    finally {
        $process.Dispose()
    }

    $text = if (Test-Path -LiteralPath $report) { (Get-Content -Raw -LiteralPath $report).Trim() } else { '' }
    if ($exitCode -ne 0) {
        throw "The system-backend probe failed (exit $exitCode): $text"
    }

    $system = Get-Item -LiteralPath (Join-Path $env:SystemRoot 'SysWOW64\winmm.dll')
    Write-Output $text
    Write-Output "host-winmm-version: $($system.VersionInfo.FileVersion)"
}
finally {
    Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue
}
