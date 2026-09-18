# Run-Ki1SelfTest.ps1 — WO-07 (2D stability): drive the KI-1 snap-chip regression
# in the REAL Starforge editor and report a doctest-style verdict to the runner.
#
# Launches Starforge.exe with COSMIC_KI1_SELFTEST set. That build-in harness opens
# a 2D edit scene, actuates the real viewport-strip snap chip for both toggle
# directions, and asserts the ImGui colour stack stays balanced at the widget:
#   * fixed editor  -> exit 0 (PASS)
#   * Debug + KI-1  -> abort()  (IM_ASSERT "PopStyleColor too many times")
#   * Release + KI-1 -> exit 1  (per-chip colorDelta != 0)
# The editor writes to './logs', './.starforge', './recordings' relative to its CWD
# and ignores COSMIC_USER_DATA, so it runs from an isolated child CWD with the
# runtime assets junction-linked in (never copying or touching real user data).
param(
    [Parameter(Mandatory=$true)][string]$Bin,
    [Parameter(Mandatory=$true)][string]$Output,
    [int]$TimeoutSec = 90
)
$ErrorActionPreference = 'Stop'
$acceptance = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$repo = [IO.Path]::GetFullPath((Join-Path $acceptance '..\..')).TrimEnd('\') + '\'
foreach ($location in @($Bin, $Output)) {
    if (-not [IO.Path]::GetFullPath($location).StartsWith($repo, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'WO-07 fixtures and artifacts must remain inside Cosmic'
    }
}

function Link-ImmutableAsset([string]$Source, [string]$Destination) {
    if (Test-Path -LiteralPath $Destination) { return }
    if ((Get-Item -LiteralPath $Source).PSIsContainer) {
        New-Item -ItemType Junction -Path $Destination -Target $Source | Out-Null
    } else {
        New-Item -ItemType HardLink -Path $Destination -Target $Source | Out-Null
    }
}
function Initialize-ChildAssets([string]$Child) {
    Link-ImmutableAsset (Join-Path $Bin 'Cosmic.dll')    (Join-Path $Child 'Cosmic.dll')
    Link-ImmutableAsset (Join-Path $Bin 'Starforge.dll') (Join-Path $Child 'Starforge.dll')
    Link-ImmutableAsset (Join-Path $Bin 'Starforge.exe') (Join-Path $Child 'Starforge.exe')
    if (Test-Path (Join-Path $Bin 'branding')) {
        Link-ImmutableAsset (Join-Path $Bin 'branding') (Join-Path $Child 'branding')
    }
    $assets = Join-Path $Child 'assets'
    New-Item -ItemType Directory -Force -Path $assets | Out-Null
    foreach ($asset in Get-ChildItem -LiteralPath (Join-Path $Bin 'assets')) {
        if ($asset.Name -in @('projects','logs')) { continue }
        Link-ImmutableAsset $asset.FullName (Join-Path $assets $asset.Name)
    }
    $projects = Join-Path $assets 'projects'
    New-Item -ItemType Directory -Force -Path $projects | Out-Null
    foreach ($project in Get-ChildItem -LiteralPath (Join-Path $Bin 'assets\projects') -Directory) {
        $local = Join-Path $projects $project.Name
        New-Item -ItemType Directory -Force -Path $local | Out-Null
        foreach ($asset in Get-ChildItem -LiteralPath $project.FullName) {
            if ($asset.Name -eq 'logs') { continue }
            Link-ImmutableAsset $asset.FullName (Join-Path $local $asset.Name)
        }
        New-Item -ItemType Directory -Force -Path (Join-Path $local 'logs') | Out-Null
    }
}

New-Item -ItemType Directory -Force -Path $Output | Out-Null
$child = Join-Path $Output ('scratch-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $child | Out-Null
Initialize-ChildAssets $child

$result  = Join-Path $Output 'ki1-result.json'
$stdout  = Join-Path $Output 'ki1-stdout.log'
if (Test-Path -LiteralPath $result) { Remove-Item -LiteralPath $result -Force }

$env:COSMIC_KI1_SELFTEST = $result
$exe = Join-Path $child 'Starforge.exe'
$proc = Start-Process -FilePath $exe -WorkingDirectory $child -PassThru -NoNewWindow `
    -RedirectStandardOutput $stdout -RedirectStandardError (Join-Path $Output 'ki1-stderr.log')
# Cache the native handle: without this, Start-Process -PassThru + redirected streams
# frequently leaves $proc.ExitCode null after a timed WaitForExit (a documented race).
$null = $proc.Handle
if (-not $proc.WaitForExit($TimeoutSec * 1000)) {
    try { $proc.Kill($true) } catch {}
    Write-Host "[doctest] test cases: 1 | 0 passed | 1 failed  (TIMEOUT after ${TimeoutSec}s)"
    exit 1
}
$proc.WaitForExit()          # flush async stdout/stderr redirection, populate ExitCode
$code = $proc.ExitCode

$verdict = 'FAIL'
if (Test-Path -LiteralPath $result) {
    try { $verdict = (Get-Content -LiteralPath $result -Raw | ConvertFrom-Json).verdict } catch {}
}
Write-Host "KI-1 self-test: exit=$code verdict=$verdict result=$result"
if (Test-Path -LiteralPath $stdout) {
    Get-Content -LiteralPath $stdout | Where-Object { $_ -match 'KI1_SELFTEST_RESULT|imgui-error|PopStyleColor|registered' } | ForEach-Object { Write-Host "  $_" }
}

# Pass requires BOTH a clean exit and a PASS verdict in the result file — a crash
# (Debug abort) or a nonzero exit (Release imbalance) is a failure, never a pass.
if ($code -eq 0 -and $verdict -eq 'PASS') {
    Write-Host "[doctest] test cases: 1 | 1 passed | 0 failed"
    exit 0
}
Write-Host "[doctest] test cases: 1 | 0 passed | 1 failed"
exit 1
