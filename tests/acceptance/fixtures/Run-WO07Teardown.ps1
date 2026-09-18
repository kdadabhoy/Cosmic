# Run-WO07Teardown.ps1 — WO-07 (2D stability): L04 teardown during live
# background activity (runtime-plugin lifecycle).
#
# Each iteration is a fresh isolated CosmicTests.exe child. Two hosts (see
# tests/test_wo07_l04.cpp):
#   * "teardown during live background activity", COSMIC_WO07_L04_CASE = mode:
#       0 reload   — TransitionToLauncher requested from INSIDE a selection dispatch while
#                    a job is blocked on an exe-owned barrier, the watcher is delivering,
#                    serial bytes stream in over the fake transport, and selection / log /
#                    hotkey / EventBus listeners fire every frame; then a quiescence window
#                    keeps every path firing to prove no stale fixture callback runs.
#       1 close    — WM_CLOSE posted from inside the dispatch while the job is blocked;
#                    the exe releases it 200 ms later; whole close must fit the 2 s bar.
#       2 careless — the plugin never joins its job; the exe releases it 300 ms AFTER
#                    detach; the engine must still not let the worker run into freed code.
#   * "module registry entries are gone before FreeLibrary and reload cleanly": a real
#     CS_MODULE module hosted by the real PlayerLayer, loaded/unloaded twice (KI-29).
# The child writes to its CWD, so each runs from an isolated dir with runtime assets
# junction-linked in (Cosmic.dll + the fixture DLLs load from the exe's real dir).
param(
    [Parameter(Mandatory=$true)][string]$Bin,
    [Parameter(Mandatory=$true)][string]$Output,
    [int]$ReloadCount   = 20,
    [int]$CloseCount    = 10,
    [int]$CarelessCount = 10,
    [int]$ModuleCount   = 10)
$ErrorActionPreference = 'Stop'
$acceptance = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Import-Module (Join-Path $acceptance 'AcceptanceRunner.psm1') -Force
$repo = [IO.Path]::GetFullPath((Join-Path $acceptance '..\..')).TrimEnd('\') + '\'
foreach ($location in @($Bin, $Output)) {
    if (-not [IO.Path]::GetFullPath($location).StartsWith($repo, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'WO-07 fixtures and artifacts must remain inside Cosmic'
    }
}
if ($ReloadCount -lt 1 -or $CloseCount -lt 0 -or $CarelessCount -lt 0 -or $ModuleCount -lt 0) { throw 'Invalid cycle counts' }

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

function Run-Child([string]$Id, [string]$Description, [string]$Filter, [hashtable]$Env) {
    foreach ($k in $Env.Keys) { Set-Item -Path ("Env:" + $k) -Value $Env[$k] }
    $temp = Join-Path $scratch $Id
    New-Item -ItemType Directory -Force -Path $temp | Out-Null
    Initialize-ChildAssets $temp
    $caseObj = [pscustomobject]@{
        id = $Id; description = $Description; tier = 'G';
        requires = @('windows','gpu-gl');
        command = (Join-Path $Bin 'CosmicTests.exe'); workingDir = $temp;
        args = @("--test-case=$Filter", '--no-skip=true', '--no-colors');
        minTests = 1; expectExit = 0; deadlineSec = 60
    }
    $vars = @{ BIN = $Bin; RUNTEMP = $temp; USERDATA = $temp; ACCEPTANCE = $acceptance }
    $record = Invoke-AcceptanceCase -Case $caseObj -Caps @{ windows = $true; 'gpu-gl' = $true } `
        -Vars $vars -RunDir $Output -AcceptanceDir $acceptance -AcceptanceMode
    [void]$records.Add($record)
    if ($record.verdict -ne 'PASSED') {
        $script:failed++
        Write-Host "$Id $($record.verdict): $($record.detail)"
        $records | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $Output 'children.json') -Encoding utf8
        return $false
    }
    return $true
}

$live = 'WO-07 L04 host: teardown during live background activity'
for ($i = 0; $i -lt $ReloadCount; $i++)   { if (-not (Run-Child "reload-i$i"   'L04 reload from inside a dispatch with everything live'   $live @{ COSMIC_WO07_L04_CASE = '0' })) { exit 1 } }
Write-Host "reload (mode 0) cycles completed=$ReloadCount"
for ($i = 0; $i -lt $CloseCount; $i++)    { if (-not (Run-Child "close-i$i"    'L04 WM_CLOSE while the job is blocked'                    $live @{ COSMIC_WO07_L04_CASE = '1' })) { exit 1 } }
Write-Host "close (mode 1) cycles completed=$CloseCount"
for ($i = 0; $i -lt $CarelessCount; $i++) { if (-not (Run-Child "careless-i$i" 'L04 careless plugin: job released only after detach'      $live @{ COSMIC_WO07_L04_CASE = '2' })) { exit 1 } }
Write-Host "careless (mode 2) cycles completed=$CarelessCount"
$mod = 'WO-07 L04 host: module registry entries are gone before FreeLibrary and reload cleanly'
for ($i = 0; $i -lt $ModuleCount; $i++)   { if (-not (Run-Child "module-i$i"   'L04 real CS_MODULE module: registry clean after each unload' $mod @{ COSMIC_WO07_L04_CASE = '0' })) { exit 1 } }
Write-Host "module registry cycles completed=$ModuleCount"

$total = $records.Count
$records | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $Output 'children.json') -Encoding utf8
Write-Host "[doctest] test cases: $total | $($total - $failed) passed | $failed failed"
exit $failed
