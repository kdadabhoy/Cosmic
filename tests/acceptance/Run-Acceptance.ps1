<#
.SYNOPSIS
    Cosmic 2D acceptance runner (WO-04). Turns an acceptance manifest into executed,
    observable evidence.

.DESCRIPTION
    A process runner: every case in the manifest is an external child process with an
    independent deadline. A timeout is FAILED (the child tree is killed, the parent
    survives). A required capability the machine lacks is ENVIRONMENT_BLOCKED, never a
    phantom pass. A filtered profile with zero expected cases is a FAILURE. Goldens are
    read-only: acceptance mode refuses -UpdateGoldens and hashes the golden dir before
    and after to prove nothing was regenerated. Results are written as JSON + JUnit with
    the full evidence contract (commit, dirty diff, mode/config, environment, exact
    command + exit code, seed, fixture hash).

    Tiers: U (pure deterministic), W (Windows integration), G (hidden-window GPU),
    I (real editor UI), Q (qualified / soak). Profiles select a set of tiers/cases.

    See docs/plans/2d-stability-2026-09-16/03-Acceptance-Test-Catalog.md.

.EXAMPLE
    ./Run-Acceptance.ps1 -Manifest manifests/selftest.manifest.json -SelfTest -DisableCapability gpu-gl

.NOTES
    Windows PowerShell 5.1 compatible. All paths are parameterized - no absolute user paths.
#>
[CmdletBinding()]
param(
    [string]$Manifest = 'manifests/selftest.manifest.json',
    [string]$Profile  = 'all',                 # 'all' or a profile name; filters cases by profile/tier
    [string[]]$Tier   = @(),                    # optional extra tier filter (U/W/G/I/Q)
    [ValidateSet('Debug','Release')]
    [string]$Config   = 'Release',
    [string]$Mode     = '2D',
    [string]$BinDir,                            # default: <repo>/build/Runtime/<Config>
    [string]$OutDir,                            # default: <acceptance>/_results/run-<stamp>
    [string]$TempRoot,                          # default: system temp; per-run subdir is created under it
    [string]$GoldenDir,                         # default: <acceptance>/goldens
    [string[]]$DisableCapability = @(),         # force a capability OFF (self-test: exercise the blocked branch)
    [bool]$AcceptanceMode = $true,              # goldens read-only; -UpdateGoldens refused
    [switch]$UpdateGoldens,                     # refused while -AcceptanceMode
    [switch]$SelfTest,                          # compare each verdict to the manifest's expectVerdict
    [switch]$KeepArtifacts                      # keep per-run temp/user-data even on all-pass
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$acceptanceDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = (Resolve-Path (Join-Path $acceptanceDir '..\..')).Path
Import-Module (Join-Path $acceptanceDir 'AcceptanceRunner.psm1') -Force

# --- Honest gate: acceptance never regenerates goldens ----------------------------
if ($AcceptanceMode -and $UpdateGoldens) {
    Write-Host "[FATAL] -UpdateGoldens cannot be enabled in acceptance mode (goldens are read-only here)." -ForegroundColor Red
    exit 3
}

# --- Resolve parameterized paths --------------------------------------------------
if (-not $BinDir)    { $BinDir    = Join-Path $repoRoot ("build\Runtime\$Config") }
if (-not $GoldenDir) { $GoldenDir = Join-Path $acceptanceDir 'goldens' }
$stamp = (Get-Date).ToString('yyyyMMdd-HHmmss')
if (-not $OutDir)    { $OutDir    = Join-Path $acceptanceDir ("_results\run-$stamp") }
if (-not $TempRoot)  { $TempRoot  = $env:TEMP }

$runTemp = Join-Path $TempRoot ("cosmic-accept-" + [Guid]::NewGuid().ToString('N'))
$userData = Join-Path $runTemp 'userdata'
$childTemp = Join-Path $runTemp 'temp'
New-Item -ItemType Directory -Path $userData  -Force | Out-Null
New-Item -ItemType Directory -Path $childTemp -Force | Out-Null
New-Item -ItemType Directory -Path $OutDir    -Force | Out-Null

# --- Manifest --------------------------------------------------------------------
$manifestPath = $Manifest
if (-not [System.IO.Path]::IsPathRooted($manifestPath)) { $manifestPath = Join-Path $acceptanceDir $manifestPath }
if (-not (Test-Path -LiteralPath $manifestPath)) {
    Write-Host "[FATAL] Manifest not found: $manifestPath" -ForegroundColor Red
    exit 3
}
$manifestObj = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json

# --- Environment + capabilities --------------------------------------------------
$environment = Get-AcceptanceEnvironment
$capProbe = Get-AcceptanceCapabilities -Disable $DisableCapability
$caps = $capProbe.caps
$capsHt = @{}
foreach ($k in $caps.Keys) { $capsHt[$k] = $caps[$k] }

# --- Commit / dirty-diff provenance ----------------------------------------------
function Get-GitInfo {
    $info = [ordered]@{ commit = $null; dirty = $null; dirty_diff_sha256 = $null }
    try {
        Push-Location $repoRoot
        $info.commit = (& git rev-parse HEAD 2>$null)
        $status = (& git status --porcelain 2>$null)
        $info.dirty = [bool]($status)
        $diff = (& git diff HEAD 2>$null) -join "`n"
        if ($diff) {
            $bytes = [System.Text.Encoding]::UTF8.GetBytes($diff)
            $sha = [System.Security.Cryptography.SHA256]::Create()
            $info.dirty_diff_sha256 = ([System.BitConverter]::ToString($sha.ComputeHash($bytes)) -replace '-','').ToLower()
        }
    } catch { } finally { Pop-Location }
    return $info
}
$git = Get-GitInfo

# --- Token substitution vars (parameterize every path in the manifest) -----------
$vars = @{
    BIN        = $BinDir
    CONFIG     = $Config
    MODE       = $Mode
    ACCEPTANCE = $acceptanceDir
    REPO       = $repoRoot
    RUNTEMP    = $childTemp
    USERDATA   = $userData
}

# --- Select cases by profile / tier ----------------------------------------------
$allCases = @($manifestObj.cases)
$selected = @($allCases | Where-Object {
    $inProfile = ($Profile -eq 'all')
    if (-not $inProfile -and ($_.PSObject.Properties.Name -contains 'profiles')) {
        $inProfile = (@($_.profiles) -contains $Profile)
    }
    $inTier = ($Tier.Count -eq 0) -or ($Tier -contains $_.tier)
    $inProfile -and $inTier
})

Write-Host "=== Cosmic 2D acceptance runner (WO-04) ===" -ForegroundColor Cyan
Write-Host ("manifest={0}  profile={1}  config={2}  mode={3}" -f $manifestObj.manifest, $Profile, $Config, $Mode)
Write-Host ("bin={0}" -f $BinDir)
Write-Host ("commit={0} dirty={1}" -f $git.commit, $git.dirty)
if ($capProbe.forced.Count -gt 0) {
    Write-Host ("forced-unavailable capabilities (self-test): {0}" -f ($capProbe.forced -join ', ')) -ForegroundColor Yellow
}
Write-Host ("selected {0} case(s) of {1}" -f $selected.Count, $allCases.Count)

# --- HONEST GATE: a filtered suite with zero expected tests is a FAILURE ----------
if ($selected.Count -eq 0) {
    Write-Host "[FATAL] Zero cases selected for this profile/tier - a filtered suite with no expected tests is a FAILURE, not an empty pass." -ForegroundColor Red
    exit 2
}

# --- Golden snapshot BEFORE ------------------------------------------------------
$goldenBefore = Get-DirHashManifest $GoldenDir

# --- Run cases -------------------------------------------------------------------
$records = @()
foreach ($case in $selected) {
    Write-Host ("  [RUN ] {0} ({1})" -f $case.id, $case.tier)
    $rec = Invoke-AcceptanceCase -Case $case -Caps $capsHt -Vars $vars -RunDir $OutDir -AcceptanceDir $acceptanceDir -AcceptanceMode:$AcceptanceMode
    $color = switch ($rec.verdict) {
        'PASSED'              { 'Green' }
        'ENVIRONMENT_BLOCKED' { 'Yellow' }
        default               { 'Red' }
    }
    Write-Host ("  [{0}] {1} - {2}" -f $rec.verdict, $case.id, $rec.detail) -ForegroundColor $color
    $records += $rec
}

# --- Golden snapshot AFTER - prove nothing was regenerated ------------------------
$goldenAfter = Get-DirHashManifest $GoldenDir
$goldenMutated = $false
$goldenKeys = @($goldenBefore.Keys) + @($goldenAfter.Keys) | Select-Object -Unique
foreach ($k in $goldenKeys) {
    $b = if ($goldenBefore.Contains($k)) { $goldenBefore[$k] } else { $null }
    $a = if ($goldenAfter.Contains($k))  { $goldenAfter[$k] }  else { $null }
    if ($b -ne $a) { $goldenMutated = $true }
}

# --- Self-test oracle: compare verdict to the manifest's expectVerdict ------------
$selfTestFailures = @()
if ($SelfTest) {
    for ($i = 0; $i -lt $selected.Count; $i++) {
        $case = $selected[$i]; $rec = $records[$i]
        if ($case.PSObject.Properties.Name -contains 'expectVerdict') {
            $rec['expect_verdict'] = $case.expectVerdict
            $ok = ($rec.verdict -eq $case.expectVerdict)
            $rec['selftest_ok'] = $ok
            if (-not $ok) { $selfTestFailures += ("{0}: expected {1}, got {2}" -f $case.id, $case.expectVerdict, $rec.verdict) }
        }
    }
}

# --- Aggregate + write evidence --------------------------------------------------
$counts = [ordered]@{
    total   = $records.Count
    passed  = @($records | Where-Object { $_.verdict -eq 'PASSED' }).Count
    failed  = @($records | Where-Object { $_.verdict -in @('FAILED','TIMEOUT','MISSING','ERROR') }).Count
    blocked = @($records | Where-Object { $_.verdict -eq 'ENVIRONMENT_BLOCKED' }).Count
}

$report = [ordered]@{
    manifest      = $manifestObj.manifest
    profile       = $Profile
    tier_filter   = $Tier
    config        = $Config
    mode          = $Mode
    generated_utc = (Get-Date).ToUniversalTime().ToString('o')
    git           = $git
    environment   = $environment
    capabilities  = $caps
    forced_unavailable = $capProbe.forced
    bin_dir       = $BinDir
    run_temp      = $runTemp
    golden_dir    = $GoldenDir
    golden_before = $goldenBefore
    golden_after  = $goldenAfter
    golden_mutated = $goldenMutated
    acceptance_mode = $AcceptanceMode
    counts        = $counts
    self_test     = [ordered]@{ enabled = [bool]$SelfTest; failures = $selfTestFailures }
    cases         = $records
}

$paths = Write-AcceptanceEvidence -Report $report -OutDir $OutDir

Write-Host ""
Write-Host ("=== SUMMARY: {0} passed / {1} failed / {2} env-blocked (of {3}) ===" -f `
    $counts.passed, $counts.failed, $counts.blocked, $counts.total) -ForegroundColor Cyan
Write-Host ("evidence: {0}" -f $paths.json)
Write-Host ("junit:    {0}" -f $paths.junit)
if ($goldenMutated) { Write-Host "[WARN] golden dir changed during the run - flagged as a mutation!" -ForegroundColor Red }

# --- Exit code -------------------------------------------------------------------
$exit = 0
if ($SelfTest) {
    if ($selfTestFailures.Count -gt 0) {
        Write-Host "[SELF-TEST FAILED] the runner did not classify these as expected:" -ForegroundColor Red
        $selfTestFailures | ForEach-Object { Write-Host ("   - {0}" -f $_) -ForegroundColor Red }
        $exit = 1
    } else {
        Write-Host "[SELF-TEST OK] every case was classified exactly as expected." -ForegroundColor Green
    }
} else {
    if ($counts.failed -gt 0) { $exit = 1 }
}
if ($goldenMutated) { $exit = 1 }

# --- Cleanup (preserve on any failure for diagnostics) ---------------------------
if (($exit -eq 0) -and (-not $KeepArtifacts)) {
    try { Remove-Item -LiteralPath $runTemp -Recurse -Force -ErrorAction SilentlyContinue } catch { }
} else {
    Write-Host ("per-run artifacts preserved: {0}" -f $runTemp)
}

exit $exit
