# Run-WO07FailLoad.ps1 — WO-07 (2D stability): L03 broken runtime-plugin recovery.
#
# Runs each broken-load case in fresh isolated CosmicTests.exe children and asserts the
# host recovered (LoadProjectDLL rejected the plugin, fell back to a live launcher, and
# closed cleanly). Cases: 0 missing DLL, 1 a DLL missing the engine export signatures,
# 2 a DLL whose CreatePluginLayer returns nullptr. Each is run IterationsPerCase times.
param(
    [Parameter(Mandatory=$true)][string]$Bin,
    [Parameter(Mandatory=$true)][string]$Output,
    [int]$IterationsPerCase = 5)
$ErrorActionPreference = 'Stop'
$acceptance = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Import-Module (Join-Path $acceptance 'AcceptanceRunner.psm1') -Force
$repo = [IO.Path]::GetFullPath((Join-Path $acceptance '..\..')).TrimEnd('\') + '\'
foreach ($location in @($Bin, $Output)) {
    if (-not [IO.Path]::GetFullPath($location).StartsWith($repo, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'WO-07 fixtures and artifacts must remain inside Cosmic'
    }
}
if ($IterationsPerCase -lt 1) { throw 'Invalid iteration count' }

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

foreach ($ci in 0,1,2) {
    for ($i = 0; $i -lt $IterationsPerCase; $i++) {
        $env:COSMIC_WO07_L03_CASE = "$ci"
        $id = "l03-c$ci-i$i"
        $temp = Join-Path $scratch $id
        New-Item -ItemType Directory -Force -Path $temp | Out-Null
        Initialize-ChildAssets $temp
        $caseObj = [pscustomobject]@{
            id = $id; description = "broken plugin load recovery (case $ci)"; tier = 'G';
            requires = @('windows','gpu-gl');
            command = (Join-Path $Bin 'CosmicTests.exe'); workingDir = $temp;
            args = @('--test-case=WO-07 L03 host*', '--no-skip=true', '--no-colors');
            minTests = 1; expectExit = 0; deadlineSec = 60
        }
        $vars = @{ BIN = $Bin; RUNTEMP = $temp; USERDATA = $temp; ACCEPTANCE = $acceptance }
        $record = Invoke-AcceptanceCase -Case $caseObj -Caps @{ windows = $true; 'gpu-gl' = $true } `
            -Vars $vars -RunDir $Output -AcceptanceDir $acceptance -AcceptanceMode
        [void]$records.Add($record)
        if ($record.verdict -ne 'PASSED') {
            $failed++
            Write-Host "$id $($record.verdict): $($record.detail)"
            $records | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $Output 'children.json') -Encoding utf8
            exit 1
        }
    }
    Write-Host "case $ci completed=$IterationsPerCase"
}

$total = $records.Count
$records | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $Output 'children.json') -Encoding utf8
Write-Host "[doctest] test cases: $total | $($total - $failed) passed | $failed failed"
exit $failed
