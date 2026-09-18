# Run-WO09Case.ps1 — WO-09 (2D stability): one doctest selection of CosmicTests or
# CosmicRenderTests as an isolated, deadline-bounded acceptance child (C01–C06 and
# the retained re-runs), mirroring WO-08's Run-WO08Render.ps1.
#
# What the wrapper adds on top of the WO-04 runner's Invoke-AcceptanceCase:
#   * H03 — refuses to run if COSMIC_UPDATE_GOLDENS is set, and SHA-256 hashes
#     tests/render/goldens before and after the child so a golden the comparator
#     wrote (its missing-golden path) or any regenerated golden is a MUTATION that
#     fails the case (applied to the headless exe too — it is cheap and uniform);
#   * copies every <name>.actual.png / <name>.diff.png the comparator dropped next
#     to the goldens into <Output>\diagnostics (and fails);
#   * points COSMIC_WO09_EVIDENCE_DIR (and the WO-08 helpers' COSMIC_WO08_EVIDENCE_DIR)
#     at <Output>\captures so the cases' reviewable artefacts (fuzz culprits, depth
#     ladders, sentinel frames, light/odd-buffer captures) land in evidence;
#   * optional -Env "NAME=value;NAME2=value" child environment (fuzz volume, probe depth).
# The child's own doctest summary line is forwarded so the runner's minTests oracle
# sees it. Doctest filters split on commas: never put a comma in -TestCase/-TestSuite.
param(
    [Parameter(Mandatory=$true)][string]$Bin,
    [Parameter(Mandatory=$true)][string]$Output,
    [string]$Exe = 'CosmicTests.exe',
    [string]$TestSuite = '',                           # optional --test-suite filter
    [string]$TestCase = '',                            # optional --test-case filter
    [string]$TestCaseExclude = '',                     # optional --test-case-exclude filter
    [int]$MinTests = 1,
    [int]$DeadlineSec = 120,
    [switch]$NoSkip,                                   # run skip(true) cases (audio, perf)
    [string]$Env = '',                                 # "K=V;K2=V2" for the child
    [string]$Tier = 'U',
    [string]$Requires = 'windows',                   # comma-separated capability list
    [string]$CaseId = 'wo09')
$ErrorActionPreference = 'Stop'
$acceptance = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Import-Module (Join-Path $acceptance 'AcceptanceRunner.psm1') -Force
$repo = [IO.Path]::GetFullPath((Join-Path $acceptance '..\..')).TrimEnd('\') + '\'
foreach ($location in @($Bin, $Output)) {
    if (-not [IO.Path]::GetFullPath($location).StartsWith($repo, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'WO-09 fixtures and artifacts must remain inside Cosmic'
    }
}
if ($env:COSMIC_UPDATE_GOLDENS) {
    Write-Host '[FATAL] COSMIC_UPDATE_GOLDENS is set in the environment - an acceptance run never regenerates goldens (H03).'
    exit 3
}
if (($TestSuite + $TestCase + $TestCaseExclude) -match ',') {
    Write-Host '[FATAL] a doctest filter must not contain a comma (doctest splits filters on commas).'
    exit 3
}
$goldenDir = Join-Path $repo 'tests\render\goldens'
if (-not (Test-Path -LiteralPath $goldenDir)) { throw "golden dir missing: $goldenDir" }

New-Item -ItemType Directory -Force -Path $Output | Out-Null
$captures = Join-Path $Output 'captures'
New-Item -ItemType Directory -Force -Path $captures | Out-Null
$temp = Join-Path $Output ('scratch-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $temp | Out-Null

Get-ChildItem -LiteralPath $goldenDir -Filter '*.actual.png' | Remove-Item -Force
Get-ChildItem -LiteralPath $goldenDir -Filter '*.diff.png'   | Remove-Item -Force
$before = Get-DirHashManifest $goldenDir
$before.GetEnumerator() | Sort-Object Name | ForEach-Object { '{0}  {1}' -f $_.Value, $_.Name } |
    Set-Content -LiteralPath (Join-Path $Output 'goldens-sha256-before.txt') -Encoding utf8

$childArgs = @('--reporters=console', '--no-intro', '--no-colors')
if ($TestSuite)       { $childArgs += "--test-suite=$TestSuite" }
if ($TestCase)        { $childArgs += "--test-case=$TestCase" }
if ($TestCaseExclude) { $childArgs += "--test-case-exclude=$TestCaseExclude" }
if ($NoSkip)          { $childArgs += '--no-skip=true' }

$env:COSMIC_WO09_EVIDENCE_DIR = $captures
$env:COSMIC_WO08_EVIDENCE_DIR = $captures
$setVars = @()
if ($Env) {
    foreach ($pair in $Env.Split(';')) {
        if (-not $pair) { continue }
        $k, $v = $pair.Split('=', 2)
        Set-Item -Path ("Env:" + $k) -Value $v
        $setVars += $k
    }
}

$reqList = @($Requires.Split(',') | Where-Object { $_ })
$caseObj = [pscustomobject]@{
    id = $CaseId; description = "WO-09 $CaseId via $Exe"; tier = $Tier;
    requires = $reqList;
    command = (Join-Path $Bin $Exe); workingDir = $Bin;
    args = $childArgs;
    minTests = $MinTests; expectExit = 0; deadlineSec = $DeadlineSec
}
# Capabilities come from the runner's own probe (gpu-gl, audio-device, ...), never
# asserted by this wrapper: a missing one is ENVIRONMENT_BLOCKED here as well.
$probe = Get-AcceptanceCapabilities
$caps = @{}
foreach ($k in $probe.caps.Keys) { $caps[$k] = $probe.caps[$k] }
$vars = @{ BIN = $Bin; RUNTEMP = $temp; USERDATA = $temp; ACCEPTANCE = $acceptance }
$record = Invoke-AcceptanceCase -Case $caseObj -Caps $caps -Vars $vars -RunDir $Output -AcceptanceDir $acceptance -AcceptanceMode

foreach ($k in $setVars) { Remove-Item -Path ("Env:" + $k) -ErrorAction SilentlyContinue }
Remove-Item Env:COSMIC_WO09_EVIDENCE_DIR -ErrorAction SilentlyContinue
Remove-Item Env:COSMIC_WO08_EVIDENCE_DIR -ErrorAction SilentlyContinue

# Golden mutation gate (H03).
$after = Get-DirHashManifest $goldenDir
$after.GetEnumerator() | Sort-Object Name | ForEach-Object { '{0}  {1}' -f $_.Value, $_.Name } |
    Set-Content -LiteralPath (Join-Path $Output 'goldens-sha256-after.txt') -Encoding utf8
$mutated = @()
foreach ($k in (@($before.Keys) + @($after.Keys) | Select-Object -Unique)) {
    $b = if ($before.Contains($k)) { $before[$k] } else { $null }
    $a = if ($after.Contains($k))  { $after[$k] }  else { $null }
    if ($b -ne $a) { $mutated += $k }
}
$diagnostics = @(Get-ChildItem -LiteralPath $goldenDir -Filter '*.actual.png') + @(Get-ChildItem -LiteralPath $goldenDir -Filter '*.diff.png')
if ($diagnostics.Count -gt 0) {
    $diagDir = Join-Path $Output 'diagnostics'
    New-Item -ItemType Directory -Force -Path $diagDir | Out-Null
    foreach ($d in $diagnostics) { Copy-Item -LiteralPath $d.FullName -Destination $diagDir -Force }
}
foreach ($m in $mutated) {
    if (-not $before.Contains($m) -and $after.Contains($m)) {
        $written = Join-Path $goldenDir $m
        $diagDir = Join-Path $Output 'diagnostics'
        New-Item -ItemType Directory -Force -Path $diagDir | Out-Null
        Copy-Item -LiteralPath $written -Destination (Join-Path $diagDir ($m + '.written-by-comparator.png')) -Force
        Remove-Item -LiteralPath $written -Force
    }
}

# Forward the child's doctest summary + messages so the runner's oracle can parse it.
if (Test-Path -LiteralPath $record.stdout_log) {
    Get-Content -LiteralPath $record.stdout_log | Where-Object { $_ -match '^\[doctest\] (test cases|assertions)|MESSAGE:|CRASHED|THREW|ERROR:' } |
        Select-Object -First 200 | ForEach-Object { Write-Host "  $_" }
}
$record['golden_mutated']       = ($mutated.Count -gt 0)
$record['golden_mutations']     = @($mutated)
$record['golden_diagnostics']   = @($diagnostics | ForEach-Object { $_.Name })
$record['golden_sha256_before'] = (Join-Path $Output 'goldens-sha256-before.txt')
$record['golden_sha256_after']  = (Join-Path $Output 'goldens-sha256-after.txt')
@($record) | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $Output 'children.json') -Encoding utf8

Remove-Item -LiteralPath $temp -Recurse -Force -ErrorAction SilentlyContinue

if ($mutated.Count -gt 0) {
    Write-Host ("[FAIL] golden dir mutated during the run: {0}" -f ($mutated -join ', '))
    exit 1
}
if ($diagnostics.Count -gt 0) {
    Write-Host ("[FAIL] golden mismatch diagnostics written: {0}" -f (($diagnostics | ForEach-Object { $_.Name }) -join ', '))
    exit 1
}
if ($record.verdict -ne 'PASSED') {
    Write-Host "$CaseId $($record.verdict): $($record.detail)"
    exit 1
}
Write-Host "$CaseId PASSED"
exit 0
