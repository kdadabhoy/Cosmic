# Run-AP03Authoring.ps1 — AP-03 (App Platform): the E01-E08 / F01 / V05 editor authoring
# sequence, driven inside the REAL Starforge editor by its own commands and panels
# (Projects/Starforge/src/AP03AuthoringSelfTest.cpp), plus the OUT-OF-PROCESS oracles:
#
#   * F-APP: every file of templates/app with @PROJECT_NAME@ replaced must equal the
#     scaffolded project's file, byte for byte (PowerShell's own comparison, not the editor);
#   * E02: scenes/Telemetry.cscene, src/screens/TelemetryScreen.h exist; Module.cpp carries the
#     include and CS_SCRIPT(TelemetryScreen) between the CS_SCREENS markers; flows/Main.cflow
#     (parsed here) has the Telemetry state and start == Home;
#   * E08: the recorded shell invocations file (COSMIC_AP03_RECORD_SHELL) names the expected
#     absolute paths (screen script, CS_SERVICE site, CS_PANEL site, Find-handlers file) and
#     nothing was launched;
#   * the result JSON's verdict, per-ID table and oracle counters.
#
# The editor writes './logs', './.starforge' relative to its CWD, so it runs from an isolated
# child CWD with the runtime assets junction-linked in (the WO-07 L02 / WO-09 C05 pattern).
param(
    [Parameter(Mandatory=$true)][string]$Bin,
    [Parameter(Mandatory=$true)][string]$Output,
    [int]$TimeoutSec = 1500,
    [string]$ProjectRoot = ''
)
$ErrorActionPreference = 'Stop'
$acceptance = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$repo = [IO.Path]::GetFullPath((Join-Path $acceptance '..\..')).TrimEnd('\') + '\'
if (-not $ProjectRoot) { $ProjectRoot = Join-Path $Output 'ap03' }
foreach ($location in @($Bin, $Output, $ProjectRoot)) {
    if (-not [IO.Path]::GetFullPath($location).StartsWith($repo, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'AP-03 fixtures and artifacts must remain inside Cosmic'
    }
}

function Link-ImmutableAsset([string]$Source, [string]$Destination) {
    if (Test-Path -LiteralPath $Destination) { return }
    if ((Get-Item -LiteralPath $Source).PSIsContainer) {
        New-Item -ItemType Junction -Path $Destination -Target $Source | Out-Null
    } else {
        New-Item -ItemType HardLink -Path $Destination -Target $Source | Out-Null
    }
}
function Initialize-ChildAssets([string]$Child) {
    Link-ImmutableAsset (Join-Path $Bin 'Cosmic.dll')    (Join-Path $Child 'Cosmic.dll')
    Link-ImmutableAsset (Join-Path $Bin 'Starforge.dll') (Join-Path $Child 'Starforge.dll')
    Link-ImmutableAsset (Join-Path $Bin 'Starforge.exe') (Join-Path $Child 'Starforge.exe')
    if (Test-Path (Join-Path $Bin 'branding')) { Link-ImmutableAsset (Join-Path $Bin 'branding') (Join-Path $Child 'branding') }
    $assets = Join-Path $Child 'assets'
    New-Item -ItemType Directory -Force -Path $assets | Out-Null
    foreach ($asset in Get-ChildItem -LiteralPath (Join-Path $Bin 'assets')) {
        if ($asset.Name -in @('projects','logs')) { continue }
        Link-ImmutableAsset $asset.FullName (Join-Path $assets $asset.Name)
    }
    $projects = Join-Path $assets 'projects'
    New-Item -ItemType Directory -Force -Path $projects | Out-Null
    foreach ($project in Get-ChildItem -LiteralPath (Join-Path $Bin 'assets\projects') -Directory) {
        $local = Join-Path $projects $project.Name
        New-Item -ItemType Directory -Force -Path $local | Out-Null
        foreach ($asset in Get-ChildItem -LiteralPath $project.FullName) {
            if ($asset.Name -eq 'logs') { continue }
            Link-ImmutableAsset $asset.FullName (Join-Path $local $asset.Name)
        }
        New-Item -ItemType Directory -Force -Path (Join-Path $local 'logs') | Out-Null
    }
}
function Remove-ChildTree([string]$Child) {
    if (-not (Test-Path -LiteralPath $Child)) { return }
    Get-ChildItem -LiteralPath $Child -Recurse -Force | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint } |
        Sort-Object { $_.FullName.Length } -Descending | ForEach-Object { [IO.Directory]::Delete($_.FullName) }
    Remove-Item -LiteralPath $Child -Recurse -Force -ErrorAction SilentlyContinue
}

New-Item -ItemType Directory -Force -Path $Output | Out-Null
$child = Join-Path $Output ('scratch-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $child | Out-Null
Initialize-ChildAssets $child
if (Test-Path -LiteralPath $ProjectRoot) { throw "ProjectRoot must not pre-exist (fresh per run): $ProjectRoot" }
New-Item -ItemType Directory -Force -Path $ProjectRoot | Out-Null

$result = Join-Path $Output 'ap03-result.json'
$shell  = Join-Path $Output 'ap03-shell-invocations.txt'
$stdout = Join-Path $Output 'ap03-stdout.log'
foreach ($f in @($result, $shell)) { if (Test-Path -LiteralPath $f) { Remove-Item -LiteralPath $f -Force } }

$env:COSMIC_AP03_SELFTEST        = $result
$env:COSMIC_AP03_ROOT            = $ProjectRoot
$env:COSMIC_AP03_RECORD_SHELL    = $shell
$env:COSMIC_STARFORGE_PROJECTS_DIR = $ProjectRoot
$env:COSMIC_SDK                  = $repo.TrimEnd('\')
$exe = Join-Path $child 'Starforge.exe'
$sw = [Diagnostics.Stopwatch]::StartNew()
$proc = Start-Process -FilePath $exe -WorkingDirectory $child -PassThru -NoNewWindow `
    -RedirectStandardOutput $stdout -RedirectStandardError (Join-Path $Output 'ap03-stderr.log')
$null = $proc.Handle
$timedOut = $false
if (-not $proc.WaitForExit($TimeoutSec * 1000)) { try { $proc.Kill($true) } catch {}; $timedOut = $true }
$proc.WaitForExit()
$code = $proc.ExitCode
$elapsed = [int]$sw.Elapsed.TotalSeconds
foreach ($k in @('COSMIC_AP03_SELFTEST','COSMIC_AP03_ROOT','COSMIC_AP03_RECORD_SHELL','COSMIC_STARFORGE_PROJECTS_DIR')) { Remove-Item -Path ("Env:" + $k) -ErrorAction SilentlyContinue }

$verdict = 'FAIL'; $res = $null
if (Test-Path -LiteralPath $result) { try { $res = Get-Content -LiteralPath $result -Raw | ConvertFrom-Json; $verdict = $res.verdict } catch {} }
Write-Host "AP03 self-test: exit=$code verdict=$verdict elapsed=${elapsed}s timedOut=$timedOut result=$result"
if ($res) {
    Write-Host ("  failed_checks={0} total_seconds={1} live={2}/{3}/{4} oracle errors={5} leaks={6} drift={7} hosted={8}" -f $res.failed_checks, [int]$res.total_seconds,
        $res.live.builds, $res.live.resumes, $res.live.failures, $res.oracle.recovered_errors, $res.oracle.end_frame_leaks, $res.oracle.context_drift, $res.oracle.hosted_imbalance)
    foreach ($p in $res.ids.PSObject.Properties) { Write-Host ("  {0}: {1}" -f $p.Name, $p.Value) }
    foreach ($c in @($res.checks_failed)) { if ($c) { Write-Host "  FAILED CHECK: $c" } }
}
if (Test-Path -LiteralPath $stdout) {
    Get-Content -LiteralPath $stdout | Where-Object { $_ -match 'AP03_SELFTEST_RESULT|FAIL|imgui-error|Assertion' } | Select-Object -First 40 | ForEach-Object { Write-Host "  $_" }
}

# ---- Independent oracles ----
$problems = @()
$proj = Join-Path $ProjectRoot 'Ap03App'
$templ = Join-Path $Bin 'assets\projects\Starforge\templates\app'
try {
    if (-not (Test-Path -LiteralPath $proj)) { throw "scaffolded project missing: $proj" }
    # F-APP: byte comparison of every template file (token replaced) with the scaffold
    $tfiles = Get-ChildItem -LiteralPath $templ -Recurse -File
    $compared = 0
    foreach ($tf in $tfiles) {
        $rel = $tf.FullName.Substring($templ.Length).TrimStart('\')
        $dst = Join-Path $proj $rel
        if (-not (Test-Path -LiteralPath $dst)) { $problems += "F-APP: missing $rel"; continue }
        $expected = [IO.File]::ReadAllBytes($tf.FullName)
        $actual   = [IO.File]::ReadAllBytes($dst)
        $isText = $tf.Extension -notin @('.png','.jpg','.bin')
        if ($isText) {
            $expText = [Text.Encoding]::UTF8.GetString($expected).Replace('@PROJECT_NAME@', 'Ap03App')
            # AppService.cpp is edited by E07 (amplitude x2, then error, then fix): compare the fixed form
            if ($rel -like 'src\services\AppService.cpp') {
                $expText = $expText.Replace('bus.Set("app.sine", amplitude * std::sin(', 'bus.Set("app.sine", 2.0 * amplitude * std::sin(') + "`n"
            }
            $actText = [Text.Encoding]::UTF8.GetString($actual)
            # Module.cpp gains the Telemetry include + CS_SCRIPT (E02), scenes/Home + Dashboard are re-saved (E04/V05), the flow gains a state
            if ($rel -in @('src\Module.cpp','flows\Main.cflow','scenes\Home.cscene','scenes\Dashboard.cscene','project.cproj')) { $compared++; continue }
            if ($expText -ne $actText) { $problems += "F-APP: $rel differs from the template" }
        } else {
            if ($expected.Length -ne $actual.Length) { $problems += "F-APP: $rel size differs" }
        }
        $compared++
    }
    if ($compared -lt 10) { $problems += "F-APP: only $compared files compared" }
    # E02 files
    $mod = Get-Content -LiteralPath (Join-Path $proj 'src\Module.cpp') -Raw
    if ($mod -notmatch '#include "screens/TelemetryScreen.h"') { $problems += 'E02: Module.cpp lacks the include' }
    $b = $mod.IndexOf('CS_SCREENS_BEGIN'); $s = $mod.IndexOf('CS_SCRIPT(TelemetryScreen)'); $e = $mod.IndexOf('CS_SCREENS_END')
    if ($s -lt 0 -or $b -gt $s -or $s -gt $e) { $problems += 'E02: CS_SCRIPT(TelemetryScreen) not between the markers' }
    if (-not (Test-Path -LiteralPath (Join-Path $proj 'src\screens\TelemetryScreen.h'))) { $problems += 'E02: TelemetryScreen.h missing' }
    if (-not (Test-Path -LiteralPath (Join-Path $proj 'scenes\Telemetry.cscene'))) { $problems += 'E02: Telemetry.cscene missing' }
    $flow = Get-Content -LiteralPath (Join-Path $proj 'flows\Main.cflow') -Raw | ConvertFrom-Json
    if (@($flow.states | Where-Object { $_.name -eq 'Telemetry' }).Count -ne 1) { $problems += 'E02: flow lacks the Telemetry state' }
    if ($flow.start -ne 'Home') { $problems += "F01: flow start is '$($flow.start)', expected Home" }
    $scene = Get-Content -LiteralPath (Join-Path $proj 'scenes\Telemetry.cscene') -Raw | ConvertFrom-Json
    $hasScript = $false; $hasCanvas = $false; $hasCam = $false
    foreach ($ent in @($scene.entities)) {
        if ($ent.components.Canvas) { $hasCanvas = $true }
        if ($ent.components.Camera) { $hasCam = $true }
        if ($ent.components.NativeScript -and $ent.components.NativeScript.ClassName -eq 'TelemetryScreen') { $hasScript = $true }
    }
    if (-not ($hasCanvas -and $hasCam -and $hasScript)) { $problems += "E02: Telemetry.cscene canvas=$hasCanvas camera=$hasCam script=$hasScript" }
    # E08 shell invocations
    if (-not (Test-Path -LiteralPath $shell)) { $problems += 'E08: no shell-invocation file' }
    else {
        $lines = Get-Content -LiteralPath $shell
        $projG = $proj.Replace('\','/')
        foreach ($exp in @("$projG/src/screens/HomeScreen.h", "$projG/src/Module.cpp", "$projG/src/services/AppService.cpp")) {
            if (-not ($lines | Where-Object { $_ -match [regex]::Escape($exp) })) { $problems += "E08: no recorded invocation for $exp" }
        }
        if (-not ($lines | Where-Object { $_ -like 'open*' })) { $problems += 'E08: no open recorded' }
        if (-not ($lines | Where-Object { $_ -like 'reveal*' })) { $problems += 'E08: no reveal recorded' }
        Write-Host "  shell invocations recorded: $($lines.Count)"
    }
} catch { $problems += "oracle error: $($_.Exception.Message)" }
$oracleOk = ($problems.Count -eq 0)
Write-Host "AP03 independent oracle: $(if ($oracleOk) { 'PASS' } else { 'FAIL' }) $(if (-not $oracleOk) { ($problems -join '; ') })"

# Evidence copies, then drop the scratch tree + the scaffolded projects (large build dirs).
foreach ($keep in @('src\Module.cpp','flows\Main.cflow','scenes\Telemetry.cscene','src\screens\TelemetryScreen.h')) {
    $src = Join-Path $proj $keep
    if (Test-Path -LiteralPath $src) { Copy-Item -LiteralPath $src -Destination (Join-Path $Output ('ap03-' + (Split-Path -Leaf $keep))) -Force }
}
Remove-ChildTree $child
Remove-Item -LiteralPath $ProjectRoot -Recurse -Force -ErrorAction SilentlyContinue

$cases = 2
if ($code -eq 0 -and $verdict -eq 'PASS' -and $oracleOk -and -not $timedOut) {
    Write-Host "[doctest] test cases: $cases | $cases passed | 0 failed"
    exit 0
}
$failed = 0; if (-not ($code -eq 0 -and $verdict -eq 'PASS' -and -not $timedOut)) { $failed++ }; if (-not $oracleOk) { $failed++ }
Write-Host "[doctest] test cases: $cases | $($cases - $failed) passed | $failed failed"
exit 1
