# Run-RepeatCase.ps1 — AP-P1: run ONE doctest selection several times in a row, in
# separate processes, and fail if any run fails.
#
# Why this exists: a suite that only passes on a machine's first run is not green, it
# is lucky. KI-57 was exactly that — WO-06 D01 leaves a bad-version scene.bin in
# %TEMP%\wo06\fallback, so the second process to run it loaded the leftover and failed
# a REQUIRE although nothing in the product had changed. ci.yml runs the units twice
# (Debug then Release) in one job, so the false red was live in CI.
#
# The child is the REAL test binary with the REAL selection — this wrapper adds
# repetition and an honest verdict, nothing else. It does NOT clear any scratch
# between runs: the whole point is that the product/test must be idempotent by itself.
# The LAST run's doctest summary line is forwarded so the runner's minTests oracle
# sees a real count.
param(
    [Parameter(Mandatory=$true)][string]$Bin,
    [Parameter(Mandatory=$true)][string]$TestCase,
    [string]$Exe      = 'CosmicTests.exe',
    [int]$Times       = 2,
    [int]$MinTests    = 1)

$ErrorActionPreference = 'Stop'
if ($TestCase -match ',') {
    Write-Host '[FATAL] a doctest filter must not contain a comma (doctest splits filters on commas).'
    exit 3
}
$exePath = Join-Path $Bin $Exe
if (-not (Test-Path -LiteralPath $exePath)) {
    Write-Host "[FATAL] test binary not found: $exePath"
    exit 3
}

$lastSummary = ''
for ($i = 1; $i -le $Times; $i++) {
    Write-Host "=== repeat $i/$Times : $Exe --test-case=`"$TestCase`" ==="
    $out = & $exePath "--test-case=$TestCase" '--no-intro' '--no-colors' 2>&1 | Out-String
    $code = $LASTEXITCODE
    Write-Host $out
    if ($code -ne 0) {
        Write-Host "[FAIL] repeat $i/$Times exited $code - the selection is not idempotent (KI-57 class)."
        exit 1
    }
    $m = [regex]::Match($out, 'test cases:\s*([0-9]+)\s*\|\s*([0-9]+) passed\s*\|\s*([0-9]+) failed')
    if (-not $m.Success) {
        Write-Host "[FAIL] repeat ${i}: no doctest summary in the child output."
        exit 1
    }
    $ran = [int]$m.Groups[1].Value
    if ($ran -lt $MinTests) {
        Write-Host "[FAIL] repeat ${i}: ran $ran test case(s), below the declared floor of $MinTests."
        exit 1
    }
    $lastSummary = $m.Value
}
# Forward one doctest-shaped summary for the runner's own minTests oracle.
Write-Host ("[doctest] " + $lastSummary + " | 0 skipped")
Write-Host "[PASS] $Times identical runs, all green."
exit 0
