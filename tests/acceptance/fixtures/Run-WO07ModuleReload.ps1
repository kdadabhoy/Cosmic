# Run-WO07ModuleReload.ps1 — WO-07 (2D stability): L02 editor game-module
# rebuild/reload acceptance, driven inside the REAL Starforge editor.
#
# Launches Starforge.exe with COSMIC_L02_SELFTEST set. The built-in harness
# (Projects/Starforge/src/L02ModuleReloadSelfTest.cpp) scaffolds a real project
# through the editor's own NewProjectAt, then runs N build cycles through the real
# BuildScripts -> BuildRunner (cmake configure + build) -> ReloadModule -> GameModule
# path, changing the module's reflected surface on a fixed schedule and asserting
# the documented reload contract after every reload (see the harness header).
#
# This wrapper adds the OUT-OF-PROCESS oracle: after the editor exits it parses the
# scene the editor saved (scenes/Main.cscene) with PowerShell's own JSON reader — not
# the engine serializer — and compares the probe entity's custom-component block,
# script overrides and Transform against the expected-final values the harness wrote.
#
# The editor writes './logs', './.starforge' relative to its CWD, so it runs from an
# isolated child CWD with the runtime assets junction-linked in. The scaffolded
# project (and its cmake build tree) lives under -ProjectRoot (short, repository-local; see the param note).
param(
    [Parameter(Mandatory=$true)][string]$Bin,
    [Parameter(Mandatory=$true)][string]$Output,
    [int]$Cycles = 52,          # 50 successful + 1 deliberate compile failure + 1 deliberate load failure
    [int]$TimeoutSec = 1740,
    # Where the project is scaffolded (+ its cmake tree). MUST be a SHORT repository-local
    # path: MSBuild's TryCompile/tlog paths under <project>/build exceed MAX_PATH (260) when
    # the project sits under the deep evidence directory. The manifest passes {RUNTEMP}/l02.
    [string]$ProjectRoot = ''
)
$ErrorActionPreference = 'Stop'
$acceptance = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$repo = [IO.Path]::GetFullPath((Join-Path $acceptance '..\..')).TrimEnd('\') + '\'
if (-not $ProjectRoot) { $ProjectRoot = Join-Path $Output 'l02' }
foreach ($location in @($Bin, $Output, $ProjectRoot)) {
    if (-not [IO.Path]::GetFullPath($location).StartsWith($repo, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'WO-07 fixtures and artifacts must remain inside Cosmic'
    }
}
if ($Cycles -lt 1) { throw 'Invalid cycle count' }

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
    if (Test-Path (Join-Path $Bin 'branding')) {
        Link-ImmutableAsset (Join-Path $Bin 'branding') (Join-Path $Child 'branding')
    }
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

New-Item -ItemType Directory -Force -Path $Output | Out-Null
$child = Join-Path $Output ('scratch-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $child | Out-Null
Initialize-ChildAssets $child
$projectRoot = $ProjectRoot                  # the scaffolded project + its cmake tree land here
if (Test-Path -LiteralPath $projectRoot) { throw "ProjectRoot must not pre-exist (fresh per run): $projectRoot" }
New-Item -ItemType Directory -Force -Path $projectRoot | Out-Null

$result   = Join-Path $Output 'l02-result.json'
$expected = Join-Path $Output 'l02-expected-final.json'
$stdout   = Join-Path $Output 'l02-stdout.log'
foreach ($f in @($result, $expected)) { if (Test-Path -LiteralPath $f) { Remove-Item -LiteralPath $f -Force } }

$env:COSMIC_L02_SELFTEST     = $result
$env:COSMIC_L02_PROJECT_ROOT = $projectRoot
$env:COSMIC_L02_CYCLES       = "$Cycles"
$env:COSMIC_SDK              = $repo.TrimEnd('\')   # the editor resolves the SDK from here (CWD is the isolated child)
$exe = Join-Path $child 'Starforge.exe'
$sw = [Diagnostics.Stopwatch]::StartNew()
$proc = Start-Process -FilePath $exe -WorkingDirectory $child -PassThru -NoNewWindow `
    -RedirectStandardOutput $stdout -RedirectStandardError (Join-Path $Output 'l02-stderr.log')
$null = $proc.Handle
if (-not $proc.WaitForExit($TimeoutSec * 1000)) {
    try { $proc.Kill($true) } catch {}
    Write-Host "[doctest] test cases: 1 | 0 passed | 1 failed  (TIMEOUT after ${TimeoutSec}s)"
    exit 1
}
$proc.WaitForExit()
$code = $proc.ExitCode
$elapsed = [int]$sw.Elapsed.TotalSeconds

$verdict = 'FAIL'; $res = $null
if (Test-Path -LiteralPath $result) {
    try { $res = Get-Content -LiteralPath $result -Raw | ConvertFrom-Json; $verdict = $res.verdict } catch {}
}
Write-Host "L02 self-test: exit=$code verdict=$verdict elapsed=${elapsed}s result=$result"
if ($res) {
    Write-Host ("  builds={0} successful_rebuild_reloads={1} reflected_field_changes={2} compile_failure_recovered={3} module_load_failure_recovered={4} failed_checks={5}" -f `
        $res.builds, $res.successful_rebuild_reloads, $res.reflected_field_changes, $res.compile_failure_recovered, $res.module_load_failure_recovered, $res.failed_checks)
    foreach ($c in @($res.checks_failed)) { if ($c) { Write-Host "  FAILED CHECK: $c" } }
}
if (Test-Path -LiteralPath $stdout) {
    Get-Content -LiteralPath $stdout | Where-Object { $_ -match 'L02_SELFTEST_RESULT|FAIL|imgui-error|Assertion' } | Select-Object -First 40 | ForEach-Object { Write-Host "  $_" }
}

# ---- Independent oracle: the saved scene, parsed here, against the expected table ----
$oracleOk = $false
$oracleWhy = 'no expected-final file'
if (Test-Path -LiteralPath $expected) {
    try {
        $exp = Get-Content -LiteralPath $expected -Raw | ConvertFrom-Json
        if (-not (Test-Path -LiteralPath $exp.scene)) { throw "saved scene missing: $($exp.scene)" }
        $scene = Get-Content -LiteralPath $exp.scene -Raw | ConvertFrom-Json
        $probe = $scene.entities | Where-Object { $_.id -eq $exp.probeId } | Select-Object -First 1
        if (-not $probe) { throw "probe entity $($exp.probeId) not in the saved scene" }
        $comps = $probe.components
        $problems = @()
        $pos = @($comps.Transform.Position)
        for ($i = 0; $i -lt 3; $i++) { if ([math]::Abs([double]$pos[$i] - [double]$exp.position[$i]) -gt 1e-5) { $problems += "Transform.Position[$i]=$($pos[$i]) expected $($exp.position[$i])" } }
        $block = $comps.L02Component
        if (-not $block) { $problems += 'L02Component block missing' }
        else {
            $expNames = @($exp.L02Component.PSObject.Properties | ForEach-Object { $_.Name })
            $gotNames = @($block.PSObject.Properties | ForEach-Object { $_.Name })
            foreach ($n in $expNames) {
                if ($gotNames -notcontains $n) { $problems += "L02Component.$n missing"; continue }
                $ev = $exp.L02Component.$n; $gv = $block.$n
                if ($ev -is [System.Array]) {
                    for ($i = 0; $i -lt $ev.Count; $i++) { if ([math]::Abs([double]$gv[$i] - [double]$ev[$i]) -gt 1e-5) { $problems += "L02Component.$n[$i]=$($gv[$i]) expected $($ev[$i])" } }
                } elseif ($ev -is [string] -or $ev -is [bool]) {
                    if ("$gv" -ne "$ev") { $problems += "L02Component.$n=$gv expected $ev" }
                } else {
                    if ([math]::Abs([double]$gv - [double]$ev) -gt 1e-5) { $problems += "L02Component.$n=$gv expected $ev" }
                }
            }
            foreach ($n in $gotNames) { if ($expNames -notcontains $n) { $problems += "L02Component has unexpected field $n" } }
        }
        if ($exp.L02ExtraOpaque) {
            $x = $comps.L02Extra
            if (-not $x -or $null -eq $x.Gain) { $problems += 'opaque L02Extra block (Gain) not preserved in the saved scene' }
        }
        $ns = $comps.NativeScript
        if (-not $ns -or $ns.ClassName -ne $exp.NativeScript.ClassName) { $problems += "NativeScript.ClassName=$($ns.ClassName)" }
        else {
            foreach ($p in $exp.NativeScript.Fields.PSObject.Properties) {
                $gv = $ns.Fields.($p.Name)
                if ($null -eq $gv) { $problems += "NativeScript.Fields.$($p.Name) missing"; continue }
                if ([math]::Abs([double]$gv - [double]$p.Value) -gt 1e-5) { $problems += "NativeScript.Fields.$($p.Name)=$gv expected $($p.Value)" }
            }
        }
        if ($problems.Count -eq 0) { $oracleOk = $true; $oracleWhy = "scene '$($exp.scene)' matches the expected table ($($expNames.Count) component fields, position, script overrides)" }
        else { $oracleWhy = ($problems -join '; ') }
    } catch { $oracleWhy = "oracle error: $($_.Exception.Message)" }
}
Write-Host "L02 independent scene oracle: $(if ($oracleOk) { 'PASS' } else { 'FAIL' }) - $oracleWhy"

# Pass requires a clean exit, a PASS verdict, and the independent oracle agreeing.
$cases = 2
if ($code -eq 0 -and $verdict -eq 'PASS' -and $oracleOk) {
    Write-Host "[doctest] test cases: $cases | $cases passed | 0 failed"
    exit 0
}
$failed = 0; if (-not ($code -eq 0 -and $verdict -eq 'PASS')) { $failed++ }; if (-not $oracleOk) { $failed++ }
Write-Host "[doctest] test cases: $cases | $($cases - $failed) passed | $failed failed"
exit 1
