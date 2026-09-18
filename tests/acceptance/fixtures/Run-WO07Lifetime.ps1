# Run-WO07Lifetime.ps1 — WO-07 (2D stability): L01 runtime-plugin (F-LIFETIME) teardown.
#
# Each iteration is a fresh isolated CosmicTests.exe child that loads
# WO07LifetimeFixture.dll into a real Application and exercises ONE runtime
# load/unload cycle:
#   case 0 (reload)       — TransitionToLauncher → UnloadProjectDLL, quiescence, post-unload probe.
#   case 1 (fresh launch) — warm up, then close the window; full Shutdown unloads the plugin.
# The child asserts the teardown contract (OnDetach before destructors before FreeLibrary,
# every owned resource released, no callback after unload, scenario threads back to baseline).
# The child writes to its CWD, so each runs from an isolated dir with runtime assets
# junction-linked in (Cosmic.dll + the fixture DLL load from the exe's real dir).
param(
    [Parameter(Mandatory=$true)][string]$Bin,
    [Parameter(Mandatory=$true)][string]$Output,
    [int]$ReloadCount = 100,
    [int]$FreshCount  = 10)
$ErrorActionPreference = 'Stop'
$acceptance = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Import-Module (Join-Path $acceptance 'AcceptanceRunner.psm1') -Force
$repo = [IO.Path]::GetFullPath((Join-Path $acceptance '..\..')).TrimEnd('\') + '\'
foreach ($location in @($Bin, $Output)) {
    if (-not [IO.Path]::GetFullPath($location).StartsWith($repo, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'WO-07 fixtures and artifacts must remain inside Cosmic'
    }
}
if ($ReloadCount -lt 1 -or $FreshCount -lt 0) { throw 'Invalid cycle counts' }

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
}

New-Item -ItemType Directory -Force -Path $Output | Out-Null
$scratch = Join-Path $Output ('scratch-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $scratch | Out-Null
$records = New-Object System.Collections.ArrayList
$failed = 0

function Run-Cycle([int]$Case, [int]$Index) {
    $env:COSMIC_WO07_LIFETIME_CASE = "$Case"
    $id = "c$Case-i$Index"
    $temp = Join-Path $scratch $id
    New-Item -ItemType Directory -Force -Path $temp | Out-Null
    Initialize-ChildAssets $temp
    $caseObj = [pscustomobject]@{
        id = $id; description = "F-LIFETIME runtime plugin teardown (case $Case)"; tier = 'G';
        requires = @('windows','gpu-gl');
        command = (Join-Path $Bin 'CosmicTests.exe'); workingDir = $temp;
        args = @('--test-case=WO-07 L01 host*', '--no-skip=true', '--no-colors');
        minTests = 1; expectExit = 0; deadlineSec = 60
    }
    $vars = @{ BIN = $Bin; RUNTEMP = $temp; USERDATA = $temp; ACCEPTANCE = $acceptance }
    $record = Invoke-AcceptanceCase -Case $caseObj -Caps @{ windows = $true; 'gpu-gl' = $true } `
        -Vars $vars -RunDir $Output -AcceptanceDir $acceptance -AcceptanceMode
    [void]$records.Add($record)
    if ($record.verdict -ne 'PASSED') {
        $script:failed++
        Write-Host "$id $($record.verdict): $($record.detail)"
        $records | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $Output 'children.json') -Encoding utf8
        return $false
    }
    return $true
}

for ($i = 0; $i -lt $ReloadCount; $i++) { if (-not (Run-Cycle 0 $i)) { exit 1 } }
Write-Host "reload cycles completed=$ReloadCount"
for ($i = 0; $i -lt $FreshCount; $i++) { if (-not (Run-Cycle 1 $i)) { exit 1 } }
Write-Host "fresh launch/close cycles completed=$FreshCount"

$total = $records.Count
$records | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $Output 'children.json') -Encoding utf8
Write-Host "[doctest] test cases: $total | $($total - $failed) passed | $failed failed"
exit $failed
