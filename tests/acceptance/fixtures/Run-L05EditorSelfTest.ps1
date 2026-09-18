# Run-L05EditorSelfTest.ps1 — WO-07 (2D stability): the editor half of L05 — scripted
# UI cycles in the REAL Starforge editor with a per-action ImGui balance oracle.
#
# Launches Starforge.exe with COSMIC_L05_SELFTEST set. The built-in harness
# (Projects/Starforge/src/L05EditorSelfTest.cpp) opens a 2D edit scene and, for N
# cycles, clicks every real viewport-strip chip (snap Move/Rotate/Scale, Grid,
# Colliders, World/Local) through Dear ImGui mouse events at the rects the strip
# reports, confirming each toggle; applies the built-in layout presets (dock/undock
# churn); hides/shows the viewport; enters/stops Play; minimizes/restores, resizes and
# toggles fullscreen with the real F11 hotkey. After EVERY action: no recovered ImGui
# error, no end-of-frame stack leak, no context drift, strip + per-chip depths balanced.
# The action log + screenshots land in -Output.
# The editor writes to './logs', './.starforge', './recordings' relative to its CWD
# and ignores COSMIC_USER_DATA, so it runs from an isolated child CWD with the
# runtime assets junction-linked in (never copying or touching real user data).
param(
    [Parameter(Mandatory=$true)][string]$Bin,
    [Parameter(Mandatory=$true)][string]$Output,
    [int]$TimeoutSec = 600,
    [int]$Cycles = 200
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

$result  = Join-Path $Output 'l05-editor-result.json'
$stdout  = Join-Path $Output 'l05-editor-stdout.log'
if (Test-Path -LiteralPath $result) { Remove-Item -LiteralPath $result -Force }

$env:COSMIC_L05_SELFTEST = $result
$env:COSMIC_L05_OUT      = $Output
$env:COSMIC_L05_CYCLES   = "$Cycles"
$exe = Join-Path $child 'Starforge.exe'
$proc = Start-Process -FilePath $exe -WorkingDirectory $child -PassThru -NoNewWindow `
    -RedirectStandardOutput $stdout -RedirectStandardError (Join-Path $Output 'l05-editor-stderr.log')
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
Write-Host "L05 editor self-test: exit=$code verdict=$verdict result=$result"
if (Test-Path -LiteralPath $stdout) {
    Get-Content -LiteralPath $stdout | Where-Object { $_ -match 'L05_EDITOR_RESULT|imgui-error|PopStyle|Assertion' } | ForEach-Object { Write-Host "  $_" }
}
$actions = Join-Path $Output 'l05-editor-actions.txt'
if (Test-Path -LiteralPath $actions) {
    $lines = @(Get-Content -LiteralPath $actions)
    $failedLines = @($lines | Where-Object { $_ -match 'result=FAIL' })
    Write-Host ("  action log: {0} lines, {1} failed actions, screenshots: {2}" -f $lines.Count, $failedLines.Count, @(Get-ChildItem -LiteralPath $Output -Filter 'l05-editor-shot-*.png').Count)
    $failedLines | Select-Object -First 10 | ForEach-Object { Write-Host "  $_" }
}

# Pass requires BOTH a clean exit and a PASS verdict in the result file — a crash
# (Debug abort on a recovered ImGui error) or a nonzero exit is a failure, never a pass.
if ($code -eq 0 -and $verdict -eq 'PASS') {
    Write-Host "[doctest] test cases: 1 | 1 passed | 0 failed"
    exit 0
}
Write-Host "[doctest] test cases: 1 | 0 passed | 1 failed"
exit 1
