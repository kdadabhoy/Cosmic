# Run-WO07Plots.ps1 — WO-07 (2D stability): P01 ImPlot lifetime + known data.
#
# Each iteration is a fresh isolated CosmicTests.exe child that loads
# WO07PlotFixture.dll (a real runtime plugin adopting the host's ImGui/ImPlot contexts)
# into a real Application and reloads it 50 times in-process, plotting the known
# time-series / XY / scatter / shaded-band / legend / log-axis / nonfinite / empty
# data under the themed UI on every cycle (see tests/test_wo07_p01.cpp for the
# assertions: adopted contexts stable + non-null, fitted limits == known ranges,
# nonfinite policy, log/linear spacing, legend entries, drawn vertices at known samples,
# ImGui stack balance + zero recovered errors, and a front-buffer pixel probe of the
# presented image). The window stays visible (pixel ownership); do not run this
# concurrently with other windowed cases.
param(
    [Parameter(Mandatory=$true)][string]$Bin,
    [Parameter(Mandatory=$true)][string]$Output,
    [int]$Launches = 3)
$ErrorActionPreference = 'Stop'
$acceptance = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Import-Module (Join-Path $acceptance 'AcceptanceRunner.psm1') -Force
$repo = [IO.Path]::GetFullPath((Join-Path $acceptance '..\..')).TrimEnd('\') + '\'
foreach ($location in @($Bin, $Output)) {
    if (-not [IO.Path]::GetFullPath($location).StartsWith($repo, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'WO-07 fixtures and artifacts must remain inside Cosmic'
    }
}
if ($Launches -lt 1) { throw 'Invalid launch count' }

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

for ($i = 0; $i -lt $Launches; $i++) {
    $id = "p01-i$i"
    $temp = Join-Path $scratch $id
    New-Item -ItemType Directory -Force -Path $temp | Out-Null
    Initialize-ChildAssets $temp
    $caseObj = [pscustomobject]@{
        id = $id; description = 'P01 ImPlot known data under 50 in-process plugin reloads (adopted contexts)'; tier = 'G';
        requires = @('windows','gpu-gl');
        command = (Join-Path $Bin 'CosmicTests.exe'); workingDir = $temp;
        args = @('--test-case=WO-07 P01 host*', '--no-skip=true', '--no-colors');
        minTests = 1; expectExit = 0; deadlineSec = 120
    }
    $vars = @{ BIN = $Bin; RUNTEMP = $temp; USERDATA = $temp; ACCEPTANCE = $acceptance }
    $record = Invoke-AcceptanceCase -Case $caseObj -Caps @{ windows = $true; 'gpu-gl' = $true } `
        -Vars $vars -RunDir $Output -AcceptanceDir $acceptance -AcceptanceMode
    [void]$records.Add($record)
    if (Test-Path -LiteralPath $record.stdout_log) {
        Get-Content -LiteralPath $record.stdout_log | Where-Object { $_ -match '^WO07 P01' } | ForEach-Object { Write-Host "  $_" }
    }
    if ($record.verdict -ne 'PASSED') {
        $failed++
        Write-Host "$id $($record.verdict): $($record.detail)"
        $records | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $Output 'children.json') -Encoding utf8
        exit 1
    }
}
Write-Host "P01 launches completed=$Launches (50 in-process reload/reopen cycles each)"
$total = $records.Count
$records | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $Output 'children.json') -Encoding utf8
Write-Host "[doctest] test cases: $total | $($total - $failed) passed | $failed failed"
exit $failed
