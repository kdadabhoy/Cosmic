# Run-WO07UiCycles.ps1 — WO-07 (2D stability): L05 scripted SF_Telem UI cycles.
#
# One fresh isolated CosmicTests.exe child hosts WO07UiCyclesFixture.dll (the real
# Workspace::SF_Telem over the WO-04 FakeSerialTransport) which runs 200 scripted
# cycles: serial closed/open/lost through the production chain, the REAL Navigation
# buttons (Main/Testing/Analysis/Replay/Home) pressed through ImGui item activation,
# replay open (recording it produced itself) + the REAL Unload button, the REAL Browse
# button opening the native IFileDialog which an exe helper cancels, minimize/restore,
# resize, F11 fullscreen (real hotkey message) both ways, and undocking a docked window
# through ImGui's own undock path (re-docked by the next layout). After EVERY action the
# oracle is judged (recovered ImGui errors, end-of-frame stack leaks, context drift,
# per-layer imbalance). The exact action log + screenshots land in -Output.
# The window stays visible; do not run concurrently with other windowed cases.
param(
    [Parameter(Mandatory=$true)][string]$Bin,
    [Parameter(Mandatory=$true)][string]$Output,
    [int]$Cycles = 200)
$ErrorActionPreference = 'Stop'
$acceptance = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Import-Module (Join-Path $acceptance 'AcceptanceRunner.psm1') -Force
$repo = [IO.Path]::GetFullPath((Join-Path $acceptance '..\..')).TrimEnd('\') + '\'
foreach ($location in @($Bin, $Output)) {
    if (-not [IO.Path]::GetFullPath($location).StartsWith($repo, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'WO-07 fixtures and artifacts must remain inside Cosmic'
    }
}
if ($Cycles -lt 1) { throw 'Invalid cycle count' }

function Link-ImmutableAsset([string]$Source, [string]$Destination) {
    if (Test-Path -LiteralPath $Destination) { return }
    if ((Get-Item -LiteralPath $Source).PSIsContainer) {
        New-Item -ItemType Junction -Path $Destination -Target $Source | Out-Null
    } else {
        New-Item -ItemType HardLink -Path $Destination -Target $Source | Out-Null
    }
}
function Initialize-ChildAssets([string]$Child) {
    $assets = Join-Path $Child 'assets'
    New-Item -ItemType Directory -Force -Path $assets | Out-Null
    foreach ($asset in Get-ChildItem -LiteralPath (Join-Path $Bin 'assets')) {
        if ($asset.Name -in @('projects','logs')) { continue }
        Link-ImmutableAsset $asset.FullName (Join-Path $assets $asset.Name)
    }
    # SF_Telem resolves project:// against assets/projects/SF_Telem (photos, themes).
    $projects = Join-Path $assets 'projects'
    New-Item -ItemType Directory -Force -Path $projects | Out-Null
    $sf = Join-Path $Bin 'assets\projects\SF_Telem'
    if (Test-Path -LiteralPath $sf) {
        $local = Join-Path $projects 'SF_Telem'
        New-Item -ItemType Directory -Force -Path $local | Out-Null
        foreach ($asset in Get-ChildItem -LiteralPath $sf) {
            if ($asset.Name -eq 'logs') { continue }
            Link-ImmutableAsset $asset.FullName (Join-Path $local $asset.Name)
        }
    }
}

New-Item -ItemType Directory -Force -Path $Output | Out-Null
$scratch = Join-Path $Output ('scratch-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $scratch | Out-Null
$temp = Join-Path $scratch 'l05'
New-Item -ItemType Directory -Force -Path $temp | Out-Null
Initialize-ChildAssets $temp

$env:COSMIC_WO07_L05_CYCLES = "$Cycles"
$env:COSMIC_WO07_L05_OUT    = $Output          # action log + screenshots
$caseObj = [pscustomobject]@{
    id = 'l05-sftelem'; description = "L05 $Cycles scripted SF_Telem UI cycles with per-action ImGui stack balance"; tier = 'I';
    requires = @('windows','gpu-gl');
    command = (Join-Path $Bin 'CosmicTests.exe'); workingDir = $temp;
    args = @('--test-case=WO-07 L05 host*', '--no-skip=true', '--no-colors');
    minTests = 1; expectExit = 0; deadlineSec = 720
}
$vars = @{ BIN = $Bin; RUNTEMP = $temp; USERDATA = $temp; ACCEPTANCE = $acceptance }
$record = Invoke-AcceptanceCase -Case $caseObj -Caps @{ windows = $true; 'gpu-gl' = $true } `
    -Vars $vars -RunDir $Output -AcceptanceDir $acceptance -AcceptanceMode
@($record) | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $Output 'children.json') -Encoding utf8
if (Test-Path -LiteralPath $record.stdout_log) {
    Get-Content -LiteralPath $record.stdout_log | Where-Object { $_ -match '^WO07 L05|ERROR: CHECK' } | ForEach-Object { Write-Host "  $_" }
}
$actions = Join-Path $Output 'l05-actions.txt'
if (Test-Path -LiteralPath $actions) {
    $lines = @(Get-Content -LiteralPath $actions)
    $failedLines = @($lines | Where-Object { $_ -match 'result=FAIL|^# FAIL' })
    Write-Host ("  action log: {0} lines, {1} failed actions, screenshots: {2}" -f $lines.Count, $failedLines.Count, @(Get-ChildItem -LiteralPath $Output -Filter 'l05-shot-*.png').Count)
    $failedLines | Select-Object -First 10 | ForEach-Object { Write-Host "  $_" }
}
if ($record.verdict -ne 'PASSED') {
    Write-Host "l05-sftelem $($record.verdict): $($record.detail)"
    Write-Host "[doctest] test cases: 1 | 0 passed | 1 failed"
    exit 1
}
Write-Host "[doctest] test cases: 1 | 1 passed | 0 failed"
exit 0
