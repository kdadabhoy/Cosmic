# Run-WO09Lifecycle.ps1 — WO-09 (2D stability): the C05 real-project lifecycle
# acceptance (create -> import -> save -> reopen -> play -> stop -> undo -> redo ->
# delete), driven inside the REAL Starforge editor by its own commands.
#
# Launches Starforge.exe with COSMIC_C05_SELFTEST set (the built-in harness in
# Projects/Starforge/src/C05ProjectLifecycleSelfTest.cpp scaffolds a real project
# through NewProjectAt and drives the sequence through Commands::*, SaveScene,
# CloseProject / OpenProjectPath, PlayScene / StopScene and the CommandStack).
#
# This wrapper adds the OUT-OF-PROCESS oracle: after the editor exits it parses the
# scene the editor saved (scenes/Main.cscene) with PowerShell's own JSON reader — not
# the engine serializer — and checks the expected table the harness wrote: every
# "present" entity exists exactly once with its fields, the deleted tilemap is
# absent, the button is a child of the canvas, and the imported prefab's 3D
# MeshRenderer / DirectionalLight and unknown FutureThing blocks are verbatim.
#
# The editor writes './logs', './.starforge' relative to its CWD, so it runs from an
# isolated child CWD with the runtime assets junction-linked in (the WO-07 L02
# pattern); the scaffolded project lives under -ProjectRoot (short, repo-local).
param(
    [Parameter(Mandatory=$true)][string]$Bin,
    [Parameter(Mandatory=$true)][string]$Output,
    [int]$TimeoutSec = 240,
    [string]$ProjectRoot = '',
    [string]$Prefab = ''            # default: <repo>\tests\fixtures\wo09\content\prefabs\Imported.cprefab
)
$ErrorActionPreference = 'Stop'
$acceptance = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$repo = [IO.Path]::GetFullPath((Join-Path $acceptance '..\..')).TrimEnd('\') + '\'
if (-not $ProjectRoot) { $ProjectRoot = Join-Path $Output 'c05' }
if (-not $Prefab) { $Prefab = Join-Path $repo 'tests\fixtures\wo09\content\prefabs\Imported.cprefab' }
foreach ($location in @($Bin, $Output, $ProjectRoot)) {
    if (-not [IO.Path]::GetFullPath($location).StartsWith($repo, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'WO-09 fixtures and artifacts must remain inside Cosmic'
    }
}
if (-not (Test-Path -LiteralPath $Prefab)) { throw "prefab fixture missing: $Prefab" }

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
function Remove-ChildTree([string]$Child) {
    # Junctions must be removed as links (never recursed into: they point at the
    # real runtime assets). Files/dirs that are ours are removed normally.
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

$result   = Join-Path $Output 'c05-result.json'
$expected = Join-Path $Output 'c05-expected-final.json'
$stdout   = Join-Path $Output 'c05-stdout.log'
foreach ($f in @($result, $expected)) { if (Test-Path -LiteralPath $f) { Remove-Item -LiteralPath $f -Force } }

$env:COSMIC_C05_SELFTEST     = $result
$env:COSMIC_C05_PROJECT_ROOT = $ProjectRoot
$env:COSMIC_C05_PREFAB       = $Prefab
$env:COSMIC_SDK              = $repo.TrimEnd('\')
$exe = Join-Path $child 'Starforge.exe'
$sw = [Diagnostics.Stopwatch]::StartNew()
$proc = Start-Process -FilePath $exe -WorkingDirectory $child -PassThru -NoNewWindow `
    -RedirectStandardOutput $stdout -RedirectStandardError (Join-Path $Output 'c05-stderr.log')
$null = $proc.Handle
$timedOut = $false
if (-not $proc.WaitForExit($TimeoutSec * 1000)) {
    try { $proc.Kill($true) } catch {}
    $timedOut = $true
}
$proc.WaitForExit()
$code = $proc.ExitCode
$elapsed = [int]$sw.Elapsed.TotalSeconds
foreach ($k in @('COSMIC_C05_SELFTEST','COSMIC_C05_PROJECT_ROOT','COSMIC_C05_PREFAB')) { Remove-Item -Path ("Env:" + $k) -ErrorAction SilentlyContinue }

$verdict = 'FAIL'; $res = $null
if (Test-Path -LiteralPath $result) {
    try { $res = Get-Content -LiteralPath $result -Raw | ConvertFrom-Json; $verdict = $res.verdict } catch {}
}
Write-Host "C05 self-test: exit=$code verdict=$verdict elapsed=${elapsed}s timedOut=$timedOut result=$result"
if ($res) {
    Write-Host ("  failed_checks={0} steps={1} total_seconds={2}" -f $res.failed_checks, @($res.steps).Count, $res.total_seconds)
    foreach ($s in @($res.steps)) { if ($s) { Write-Host ("  step {0}: {1:N1} ms" -f $s.step, $s.ms) } }
    foreach ($c in @($res.checks_failed)) { if ($c) { Write-Host "  FAILED CHECK: $c" } }
}
if (Test-Path -LiteralPath $stdout) {
    Get-Content -LiteralPath $stdout | Where-Object { $_ -match 'C05_SELFTEST_RESULT|FAIL|imgui-error|Assertion' } | Select-Object -First 40 | ForEach-Object { Write-Host "  $_" }
}

# ---- Independent oracle: the saved scene, parsed here, against the expected table ----
$oracleOk = $false
$oracleWhy = 'no expected-final file'
if (Test-Path -LiteralPath $expected) {
    try {
        $exp = Get-Content -LiteralPath $expected -Raw | ConvertFrom-Json
        if (-not (Test-Path -LiteralPath $exp.scene)) { throw "saved scene missing: $($exp.scene)" }
        $t0 = [Diagnostics.Stopwatch]::StartNew()
        $scene = Get-Content -LiteralPath $exp.scene -Raw | ConvertFrom-Json
        $parseMs = $t0.Elapsed.TotalMilliseconds
        $problems = @()
        $byId = @{}
        foreach ($e in @($scene.entities)) {
            if ($byId.ContainsKey($e.id)) { $problems += "duplicate entity id $($e.id)" }
            $byId[$e.id] = $e
        }
        foreach ($p in $exp.present.PSObject.Properties) {
            if (-not $byId.ContainsKey($p.Value)) { $problems += "$($p.Name) ($($p.Value)) is missing from the saved scene" }
        }
        foreach ($a in @($exp.absent)) { if ($byId.ContainsKey($a)) { $problems += "deleted entity $a is still in the saved scene" } }
        # Every Relationship child link must name an entity in the file (no stale reference).
        foreach ($e in @($scene.entities)) {
            $rel = $e.components.Relationship
            if ($rel -and $rel.Children) { foreach ($c in @($rel.Children)) { if (-not $byId.ContainsKey($c)) { $problems += "entity $($e.id) links a missing child $c" } } }
        }
        if ($byId.ContainsKey($exp.present.sprite)) {
            $s = $byId[$exp.present.sprite].components
            if ($s.Tag.Tag -ne $exp.sprite.Tag) { $problems += "sprite Tag=$($s.Tag.Tag)" }
            for ($i = 0; $i -lt 3; $i++) { if ([math]::Abs([double]$s.Transform.Position[$i] - [double]$exp.sprite.Position[$i]) -gt 1e-5) { $problems += "sprite Position[$i]=$($s.Transform.Position[$i])" } }
            if ([int]$s.SpriteRenderer.ZOrder -ne [int]$exp.sprite.ZOrder) { $problems += "sprite ZOrder=$($s.SpriteRenderer.ZOrder)" }
            if ($s.SpriteRenderer.TexturePath -ne $exp.sprite.TexturePath) { $problems += "sprite TexturePath=$($s.SpriteRenderer.TexturePath)" }
            if (-not $s.SpriteRenderer.FlipX) { $problems += 'sprite FlipX lost' }
        }
        if ($byId.ContainsKey($exp.present.lamp)) {
            $l = $byId[$exp.present.lamp].components
            if ([math]::Abs([double]$l.Light2D.Radius - [double]$exp.lamp.Radius) -gt 1e-5) { $problems += "lamp Radius=$($l.Light2D.Radius)" }
        }
        if ($byId.ContainsKey($exp.present.canvas) -and $byId.ContainsKey($exp.present.button)) {
            $kids = @($byId[$exp.present.canvas].components.Relationship.Children)
            if ($kids -notcontains $exp.present.button) { $problems += 'button is not listed among the canvas children' }
            if ($byId[$exp.present.button].components.UiButton.Signal -ne $exp.button.Signal) { $problems += 'button Signal lost' }
        }
        if ($byId.ContainsKey($exp.present.imported)) {
            $c = $byId[$exp.present.imported].components
            if (-not $c.FutureThing) { $problems += 'imported FutureThing block missing' }
            else {
                if ($c.FutureThing.Text -ne $exp.imported.FutureThing.Text) { $problems += 'FutureThing.Text changed' }
                if (-not $c.FutureThing.Flag) { $problems += 'FutureThing.Flag changed' }
                $deep = @($c.FutureThing.Nested.Deep)
                if ($deep.Count -ne 3 -or $deep[0] -ne 1 -or $deep[2] -ne 3) { $problems += 'FutureThing.Nested.Deep changed' }
            }
            if (-not $c.MeshRenderer) { $problems += 'imported 3D MeshRenderer block missing' }
            else {
                if ($c.MeshRenderer.MeshPath -ne $exp.imported.MeshRenderer.MeshPath) { $problems += 'MeshRenderer.MeshPath changed' }
                if ($c.MeshRenderer.CastShadows -ne $false) { $problems += 'MeshRenderer.CastShadows changed' }
            }
            $kids = @($c.Relationship.Children)
            if ($kids -notcontains $exp.present.importedChild) { $problems += 'imported child not linked to the imported root' }
        }
        if ($byId.ContainsKey($exp.present.importedChild)) {
            $c = $byId[$exp.present.importedChild].components
            if (-not $c.DirectionalLight -or [math]::Abs([double]$c.DirectionalLight.Intensity - 3.5) -gt 1e-5) { $problems += 'imported child 3D DirectionalLight block lost' }
            if (-not $c.Light2D -or [math]::Abs([double]$c.Light2D.Radius - 2.5) -gt 1e-5) { $problems += 'imported child Light2D lost' }
        }
        if ($parseMs -gt 10000) { $problems += "out-of-process parse took $parseMs ms" }
        if ($problems.Count -eq 0) { $oracleOk = $true; $oracleWhy = "scene '$($exp.scene)' matches the expected table ($(@($scene.entities).Count) entities, parsed in $([int]$parseMs) ms)" }
        else { $oracleWhy = ($problems -join '; ') }
    } catch { $oracleWhy = "oracle error: $($_.Exception.Message)" }
}
Write-Host "C05 independent scene oracle: $(if ($oracleOk) { 'PASS' } else { 'FAIL' }) - $oracleWhy"

# Keep the saved scene as evidence; drop the junction-linked scratch tree.
if ($res -and (Test-Path -LiteralPath (Join-Path $ProjectRoot 'C05Life\scenes\Main.cscene'))) {
    Copy-Item -LiteralPath (Join-Path $ProjectRoot 'C05Life\scenes\Main.cscene') -Destination (Join-Path $Output 'c05-saved-Main.cscene') -Force
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
