# Run-WO10Case.ps1 — WO-10 (2D stability): one doctest selection of CosmicTests or
# CosmicRenderTests as an isolated, deadline-bounded acceptance child (N01–N04 and
# the retained re-runs), extending WO-09's Run-WO09Case.ps1.
#
# What the wrapper adds on top of the WO-04 runner's Invoke-AcceptanceCase:
#   * H03 — refuses to run if COSMIC_UPDATE_GOLDENS is set, and SHA-256 hashes
#     tests/render/goldens before and after the child so a golden the comparator
#     wrote (its missing-golden path) or any regenerated golden is a MUTATION that
#     fails the case;
#   * copies every <name>.actual.png / <name>.diff.png the comparator dropped next
#     to the goldens into <Output>\diagnostics (and fails);
#   * points COSMIC_WO10_EVIDENCE_DIR (and the WO-08/09 helpers' variables) at
#     <Output>\captures so the cases' reviewable artefacts land in evidence;
#   * -Env "NAME=value;NAME2=value" child environment (e.g. COSMIC_WO10_RUNG=60hz);
#   * -IsolateCwd — the child runs from a scratch directory with the runtime assets
#     junction-linked in (the WO-07 pattern), because an Application-hosting case
#     writes logs/ and imgui.ini into its CWD;
#   * -TestCaseExcludeMulti "A|B" — several exclude patterns (joined with the comma
#     doctest splits filters on; the single-pattern parameters still refuse commas);
#   * -CompareRuns — the child is launched TWICE (two processes, captures\run-1 and
#     captures\run-2) and every file the two runs wrote is byte-compared; any
#     difference fails the case (N04's same-build reproducibility).
# The child's own doctest summary line is forwarded so the runner's minTests oracle
# sees it. Doctest filters split on commas: never put a comma in -TestCase/-TestSuite.
param(
    [Parameter(Mandatory=$true)][string]$Bin,
    [Parameter(Mandatory=$true)][string]$Output,
    [string]$Exe = 'CosmicTests.exe',
    [string]$TestSuite = '',                           # optional --test-suite filter
    [string]$TestCase = '',                            # optional --test-case filter
    [string]$TestCaseExclude = '',                     # optional --test-case-exclude filter
    [string]$TestCaseExcludeMulti = '',                # several exclude patterns, '|'-separated (joined with the comma doctest splits on)
    [int]$MinTests = 1,
    [int]$DeadlineSec = 120,
    [switch]$NoSkip,                                   # run skip(true) cases (host rungs)
    [string]$Env = '',                                 # "K=V;K2=V2" for the child
    [string]$Tier = 'U',
    [string]$Requires = 'windows',                     # comma-separated capability list
    [string]$CaseId = 'wo10',
    [switch]$IsolateCwd,
    [switch]$CompareRuns)
$ErrorActionPreference = 'Stop'
$acceptance = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Import-Module (Join-Path $acceptance 'AcceptanceRunner.psm1') -Force
$repo = [IO.Path]::GetFullPath((Join-Path $acceptance '..\..')).TrimEnd('\') + '\'
foreach ($location in @($Bin, $Output)) {
    if (-not [IO.Path]::GetFullPath($location).StartsWith($repo, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'WO-10 fixtures and artifacts must remain inside Cosmic'
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
function Remove-ChildTree([string]$Child) {
    if (-not (Test-Path -LiteralPath $Child)) { return }
    Get-ChildItem -LiteralPath $Child -Recurse -Force | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint } |
        Sort-Object { $_.FullName.Length } -Descending | ForEach-Object { [IO.Directory]::Delete($_.FullName) }
    Remove-Item -LiteralPath $Child -Recurse -Force -ErrorAction SilentlyContinue
}

New-Item -ItemType Directory -Force -Path $Output | Out-Null
$captures = Join-Path $Output 'captures'
New-Item -ItemType Directory -Force -Path $captures | Out-Null
$temp = Join-Path $Output ('scratch-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $temp | Out-Null
if ($IsolateCwd) { Initialize-ChildAssets $temp }

Get-ChildItem -LiteralPath $goldenDir -Filter '*.actual.png' | Remove-Item -Force
Get-ChildItem -LiteralPath $goldenDir -Filter '*.diff.png'   | Remove-Item -Force
$before = Get-DirHashManifest $goldenDir
$before.GetEnumerator() | Sort-Object Name | ForEach-Object { '{0}  {1}' -f $_.Value, $_.Name } |
    Set-Content -LiteralPath (Join-Path $Output 'goldens-sha256-before.txt') -Encoding utf8

$childArgs = @('--reporters=console', '--no-intro', '--no-colors')
if ($TestSuite)       { $childArgs += "--test-suite=$TestSuite" }
if ($TestCase)        { $childArgs += "--test-case=$TestCase" }
if ($TestCaseExclude) { $childArgs += "--test-case-exclude=$TestCaseExclude" }
if ($TestCaseExcludeMulti) { $childArgs += ("--test-case-exclude=" + (($TestCaseExcludeMulti.Split('|') | Where-Object { $_ }) -join ',')) }
if ($NoSkip)          { $childArgs += '--no-skip=true' }

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
$probe = Get-AcceptanceCapabilities
$caps = @{}
foreach ($k in $probe.caps.Keys) { $caps[$k] = $probe.caps[$k] }
$workDir = if ($IsolateCwd) { $temp } else { $Bin }
$vars = @{ BIN = $Bin; RUNTEMP = $temp; USERDATA = $temp; ACCEPTANCE = $acceptance }

$runs = if ($CompareRuns) { 2 } else { 1 }
$records = @()
for ($i = 1; $i -le $runs; $i++) {
    $capDir = if ($CompareRuns) { Join-Path $captures ("run-" + $i) } else { $captures }
    New-Item -ItemType Directory -Force -Path $capDir | Out-Null
    $env:COSMIC_WO10_EVIDENCE_DIR = $capDir
    $env:COSMIC_WO09_EVIDENCE_DIR = $capDir
    $env:COSMIC_WO08_EVIDENCE_DIR = $capDir
    $id = if ($CompareRuns) { "$CaseId-run$i" } else { $CaseId }
    $caseObj = [pscustomobject]@{
        id = $id; description = "WO-10 $CaseId via $Exe"; tier = $Tier;
        requires = $reqList;
        command = (Join-Path $Bin $Exe); workingDir = $workDir;
        args = $childArgs;
        minTests = $MinTests; expectExit = 0; deadlineSec = $DeadlineSec
    }
    $record = Invoke-AcceptanceCase -Case $caseObj -Caps $caps -Vars $vars -RunDir $Output -AcceptanceDir $acceptance -AcceptanceMode
    $records += $record
}
foreach ($k in $setVars) { Remove-Item -Path ("Env:" + $k) -ErrorAction SilentlyContinue }
Remove-Item Env:COSMIC_WO10_EVIDENCE_DIR -ErrorAction SilentlyContinue
Remove-Item Env:COSMIC_WO09_EVIDENCE_DIR -ErrorAction SilentlyContinue
Remove-Item Env:COSMIC_WO08_EVIDENCE_DIR -ErrorAction SilentlyContinue

# Same-build reproducibility: every file run-1 wrote must be byte-identical in run-2.
$identical = $true; $compared = @()
if ($CompareRuns) {
    $r1 = Join-Path $captures 'run-1'; $r2 = Join-Path $captures 'run-2'
    foreach ($f in Get-ChildItem -LiteralPath $r1 -File) {
        $twin = Join-Path $r2 $f.Name
        $h1 = Get-FileSha256 $f.FullName
        $h2 = if (Test-Path -LiteralPath $twin) { Get-FileSha256 $twin } else { 'MISSING' }
        $compared += [pscustomobject]@{ file = $f.Name; bytes = $f.Length; run1 = $h1; run2 = $h2; identical = ($h1 -eq $h2) }
        if ($h1 -ne $h2) { $identical = $false }
    }
    if ($compared.Count -eq 0) { $identical = $false }
    $compared | ForEach-Object { '{0}  {1} bytes  run1={2}  run2={3}  identical={4}' -f $_.file, $_.bytes, $_.run1, $_.run2, $_.identical } |
        Set-Content -LiteralPath (Join-Path $Output 'same-build-compare.txt') -Encoding utf8
    foreach ($c in $compared) { Write-Host ("  same-build {0}: {1}" -f $c.file, $(if ($c.identical) { 'identical' } else { 'DIFFERENT' })) }
}

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

# Forward the children's doctest summary + messages so the runner's oracle can parse it.
$passedCases = 0; $failedCases = 0; $allPassed = $true
foreach ($record in $records) {
    if (Test-Path -LiteralPath $record.stdout_log) {
        Get-Content -LiteralPath $record.stdout_log | Where-Object { $_ -match '^\[doctest\] (test cases|assertions)|MESSAGE:|CRASHED|THREW|ERROR:|\[WO10\]' } |
            Select-Object -First 400 | ForEach-Object { Write-Host "  $_" }
    }
    if ($record.verdict -ne 'PASSED') { $allPassed = $false }
}
$records | ForEach-Object {
    $_['golden_mutated']       = ($mutated.Count -gt 0)
    $_['golden_mutations']     = @($mutated)
    $_['golden_diagnostics']   = @($diagnostics | ForEach-Object { $_.Name })
    $_['golden_sha256_before'] = (Join-Path $Output 'goldens-sha256-before.txt')
    $_['golden_sha256_after']  = (Join-Path $Output 'goldens-sha256-after.txt')
    if ($CompareRuns) { $_['same_build_identical'] = $identical; $_['same_build_files'] = @($compared | ForEach-Object { $_.file }) }
}
@($records) | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $Output 'children.json') -Encoding utf8

Remove-ChildTree $temp

if ($mutated.Count -gt 0) {
    Write-Host ("[FAIL] golden dir mutated during the run: {0}" -f ($mutated -join ', '))
    exit 1
}
if ($diagnostics.Count -gt 0) {
    Write-Host ("[FAIL] golden mismatch diagnostics written: {0}" -f (($diagnostics | ForEach-Object { $_.Name }) -join ', '))
    exit 1
}
if ($CompareRuns -and -not $identical) {
    Write-Host "[FAIL] $CaseId same-build reproducibility: the two processes' outputs differ (see same-build-compare.txt)"
    exit 1
}
if (-not $allPassed) {
    foreach ($record in $records) { if ($record.verdict -ne 'PASSED') { Write-Host "$($record.id) $($record.verdict): $($record.detail)" } }
    exit 1
}
if ($CompareRuns) {
    # Two children each printed a summary; the runner's oracle must see one line for the case.
    Write-Host ("  same-build: {0} file(s) byte-identical across two processes" -f $compared.Count)
}
Write-Host "$CaseId PASSED"
exit 0
