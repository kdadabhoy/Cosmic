# Run-AP02Render.ps1 — AP-02 (App Platform): one CosmicRenderTests suite (V03 goldens + sentinels, E05 A/B) as an
# isolated, deadline-bounded acceptance child. Modelled on Run-WO08Render.ps1 (WO-08):
#
# What the wrapper adds on top of the WO-04 runner's Invoke-AcceptanceCase:
#   * H03 — refuses to run at all if COSMIC_UPDATE_GOLDENS is set in the environment, and
#     SHA-256 hashes tests/render/goldens before and after the child so a golden the
#     comparator wrote (its missing-golden path writes the capture before failing) or any
#     regenerated golden is flagged as a MUTATION and fails the case;
#   * copies every <name>.actual.png / <name>.diff.png the comparator dropped next to the
#     goldens into <Output>\diagnostics (and fails), so a golden mismatch is reviewable;
#   * points COSMIC_WO08_EVIDENCE_DIR (the shared Wo08::WriteEvidence hook) at <Output>\captures
#     so every widget frame the cases render (goldens, E05 preview/live/other) lands in evidence;
#   * -NoSkip / -PerfOut are kept for parity with the WO-08 wrapper (unused by AP-02 cases).
# The child's own doctest summary line is forwarded so the runner's minTests oracle sees it.
param(
    [Parameter(Mandatory=$true)][string]$Bin,
    [Parameter(Mandatory=$true)][string]$Output,
    [Parameter(Mandatory=$true)][string]$Suite,        # doctest --test-suite filter ("AP-02 V03")
    [string]$Exe = 'CosmicRenderTests.exe',
    [string]$TestCase = '',                            # optional --test-case filter
    [string]$TestCaseExclude = '',                     # optional --test-case-exclude filter
    [int]$MinTests = 1,
    [int]$DeadlineSec = 120,
    [switch]$NoSkip,                                   # parity: run skip(true) cases
    [string]$PerfOut = '',                             # parity: COSMIC_WO08_PERF_OUT base path
    [string]$CaseId = 'ap02')
$ErrorActionPreference = 'Stop'
$acceptance = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Import-Module (Join-Path $acceptance 'AcceptanceRunner.psm1') -Force
$repo = [IO.Path]::GetFullPath((Join-Path $acceptance '..\..')).TrimEnd('\') + '\'
foreach ($location in @($Bin, $Output)) {
    if (-not [IO.Path]::GetFullPath($location).StartsWith($repo, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'AP-02 fixtures and artifacts must remain inside Cosmic'
    }
}
if ($env:COSMIC_UPDATE_GOLDENS) {
    Write-Host '[FATAL] COSMIC_UPDATE_GOLDENS is set in the environment - an acceptance run never regenerates goldens (H03).'
    exit 3
}
$goldenDir = Join-Path $repo 'tests\render\goldens'
if (-not (Test-Path -LiteralPath $goldenDir)) { throw "golden dir missing: $goldenDir" }

New-Item -ItemType Directory -Force -Path $Output | Out-Null
$captures = Join-Path $Output 'captures'
New-Item -ItemType Directory -Force -Path $captures | Out-Null
$temp = Join-Path $Output ('scratch-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $temp | Out-Null

# Nothing stale may masquerade as this run's diagnostics.
Get-ChildItem -LiteralPath $goldenDir -Filter '*.actual.png' | Remove-Item -Force
Get-ChildItem -LiteralPath $goldenDir -Filter '*.diff.png'   | Remove-Item -Force
$before = Get-DirHashManifest $goldenDir
$before.GetEnumerator() | Sort-Object Name | ForEach-Object { '{0}  {1}' -f $_.Value, $_.Name } |
    Set-Content -LiteralPath (Join-Path $Output 'goldens-sha256-before.txt') -Encoding utf8

$childArgs = @("--test-suite=$Suite", '--reporters=console', '--no-intro', '--no-colors')
if ($TestCase)        { $childArgs += "--test-case=$TestCase" }
if ($TestCaseExclude) { $childArgs += "--test-case-exclude=$TestCaseExclude" }
if ($NoSkip)          { $childArgs += '--no-skip=true' }

$env:COSMIC_WO08_EVIDENCE_DIR = $captures
if ($PerfOut) { $env:COSMIC_WO08_PERF_OUT = $PerfOut } else { Remove-Item Env:COSMIC_WO08_PERF_OUT -ErrorAction SilentlyContinue }

$caseObj = [pscustomobject]@{
    id = $CaseId; description = "AP-02 $Suite via $Exe"; tier = 'G';
    requires = @('windows','gpu-gl');
    command = (Join-Path $Bin $Exe); workingDir = $Bin;
    args = $childArgs;
    minTests = $MinTests; expectExit = 0; deadlineSec = $DeadlineSec
}
$vars = @{ BIN = $Bin; RUNTEMP = $temp; USERDATA = $temp; ACCEPTANCE = $acceptance }
$record = Invoke-AcceptanceCase -Case $caseObj -Caps @{ windows = $true; 'gpu-gl' = $true } `
    -Vars $vars -RunDir $Output -AcceptanceDir $acceptance -AcceptanceMode

# Golden mutation gate (H03): hash after, compare, and collect diagnostics.
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
# A golden the comparator WROTE for a missing name is a mutation too: remove it so the
# working tree is exactly what it was, and report it.
foreach ($m in $mutated) {
    if (-not $before.Contains($m) -and $after.Contains($m)) {
        $written = Join-Path $goldenDir $m
        $diagDir = Join-Path $Output 'diagnostics'
        New-Item -ItemType Directory -Force -Path $diagDir | Out-Null
        Copy-Item -LiteralPath $written -Destination (Join-Path $diagDir ($m + '.written-by-comparator.png')) -Force
        Remove-Item -LiteralPath $written -Force
    }
}

# Forward the child's doctest summary so the runner's minTests oracle can parse it.
if (Test-Path -LiteralPath $record.stdout_log) {
    Get-Content -LiteralPath $record.stdout_log | Where-Object { $_ -match '^\[doctest\] (test cases|assertions)|MESSAGE:|R07 ' } | ForEach-Object { Write-Host "  $_" }
}
$record['golden_mutated']     = ($mutated.Count -gt 0)
$record['golden_mutations']   = @($mutated)
$record['golden_diagnostics'] = @($diagnostics | ForEach-Object { $_.Name })
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
Write-Host "$CaseId PASSED ($Suite)"
exit 0
