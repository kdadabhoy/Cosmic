# Run-UX02Editor.ps1 — UX-02 (UX & Shipping): the ED01 / ED03 / ED04 / ED05 editor sequence,
# driven inside the REAL Starforge editor by its own commands and panels
# (Projects/Starforge/src/UX02EditorSelfTest.cpp), plus the OUT-OF-PROCESS oracles:
#
#   * the fixture copies: tests/fixtures/ux02 -> <ProjectRoot>\Ux02Fixture (hashed: paths + bytes)
#     and Projects/PendulumLab -> <ProjectRoot>\PendulumLab (src/, scenes/, flows/, project.cproj);
#   * ED05 window-close leg: the editor ends with a dirty scene through Application::Close (the
#     title-bar X path, which no layer can veto); OnDetach must leave the autosave copy the result
#     JSON names (detach_autosave_expected), holding the marker entity (detach_marker);
#   * the fixture tree is unchanged by the run (the editor works on the copy);
#   * the result JSON's verdict, per-ID table and oracle counters.
#
# The editor writes './logs', './.starforge' and user:// (editor.toml, autosave/) relative to its
# CWD, so it runs from an isolated child CWD with the runtime assets junction-linked in (the
# WO-07 L02 / WO-09 C05 / AP-03 pattern) — a lane never touches the real editor.toml.
# The ED01 sprite leg moves the REAL OS cursor (SetCursorPos) and posts mouse messages to the
# editor window (the GUIDE pattern): do not touch the mouse while it runs.
param(
    [Parameter(Mandatory=$true)][string]$Bin,
    [Parameter(Mandatory=$true)][string]$Output,
    [int]$TimeoutSec = 900,
    [string]$ProjectRoot = '',
    [string]$Shots = ''
)
$ErrorActionPreference = 'Stop'
$acceptance = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$repo = [IO.Path]::GetFullPath((Join-Path $acceptance '..\..')).TrimEnd('\') + '\'
if (-not $ProjectRoot) { $ProjectRoot = Join-Path $Output 'ux02' }
if (-not $Shots) { $Shots = Join-Path $ProjectRoot 'shots' }
foreach ($location in @($Bin, $Output, $ProjectRoot, $Shots)) {
    if (-not [IO.Path]::GetFullPath($location).StartsWith($repo, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'UX-02 fixtures and artifacts must remain inside Cosmic'
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
function Get-TreeHash([string]$Root) {
    # SHA-256 over "relative/path\n<bytes>" of every file, sorted by path (ordinal).
    $sha = [Security.Cryptography.SHA256]::Create()
    $ms = New-Object IO.MemoryStream
    $files = Get-ChildItem -LiteralPath $Root -Recurse -File | Sort-Object { $_.FullName.Substring($Root.Length).Replace('\','/') } -CaseSensitive
    foreach ($f in $files) {
        $rel = [Text.Encoding]::UTF8.GetBytes($f.FullName.Substring($Root.Length).TrimStart('\').Replace('\','/') + "`n")
        $ms.Write($rel, 0, $rel.Length)
        $b = [IO.File]::ReadAllBytes($f.FullName); $ms.Write($b, 0, $b.Length)
    }
    $h = $sha.ComputeHash($ms.ToArray())
    return (($h | ForEach-Object { $_.ToString('x2') }) -join '').Substring(0, 16) + " ($($files.Count) files)"
}

New-Item -ItemType Directory -Force -Path $Output | Out-Null
$child = Join-Path $Output ('scratch-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $child | Out-Null
Initialize-ChildAssets $child
if (Test-Path -LiteralPath $ProjectRoot) { throw "ProjectRoot must not pre-exist (fresh per run): $ProjectRoot" }
New-Item -ItemType Directory -Force -Path $ProjectRoot | Out-Null

# Fixture copies (the editor edits the copies, never the tree).
$fixtureSrc = Join-Path $repo 'tests\fixtures\ux02'
$fixtureHashBefore = Get-TreeHash $fixtureSrc
Copy-Item -LiteralPath $fixtureSrc -Destination (Join-Path $ProjectRoot 'Ux02Fixture') -Recurse
$plSrc = Join-Path $repo 'Projects\PendulumLab'
$plDst = Join-Path $ProjectRoot 'PendulumLab'
New-Item -ItemType Directory -Force -Path $plDst | Out-Null
foreach ($part in @('src','scenes','flows','project.cproj','CMakeLists.txt','README.md')) {
    $p = Join-Path $plSrc $part
    if (Test-Path -LiteralPath $p) { Copy-Item -LiteralPath $p -Destination (Join-Path $plDst $part) -Recurse }
}
Write-Host "UX02 fixture tests/fixtures/ux02 sha256/16: $fixtureHashBefore"

$result = Join-Path $Output 'ux02-result.json'
$shell  = Join-Path $Output 'ux02-shell-invocations.txt'
$stdout = Join-Path $Output 'ux02-stdout.log'
foreach ($f in @($result, $shell)) { if (Test-Path -LiteralPath $f) { Remove-Item -LiteralPath $f -Force } }

$env:COSMIC_UX02_SELFTEST          = $result
$env:COSMIC_UX02_ROOT              = $ProjectRoot
$env:COSMIC_UX02_SHOTS             = $Shots
$env:COSMIC_AP03_RECORD_SHELL      = $shell
$env:COSMIC_STARFORGE_PROJECTS_DIR = $ProjectRoot
$env:COSMIC_SDK                    = $repo.TrimEnd('\')
$exe = Join-Path $child 'Starforge.exe'
$sw = [Diagnostics.Stopwatch]::StartNew()
$proc = Start-Process -FilePath $exe -WorkingDirectory $child -PassThru -NoNewWindow `
    -RedirectStandardOutput $stdout -RedirectStandardError (Join-Path $Output 'ux02-stderr.log')
$null = $proc.Handle
$timedOut = $false
if (-not $proc.WaitForExit($TimeoutSec * 1000)) { try { $proc.Kill($true) } catch {}; $timedOut = $true }
$proc.WaitForExit()
$code = $proc.ExitCode
$elapsed = [int]$sw.Elapsed.TotalSeconds
foreach ($k in @('COSMIC_UX02_SELFTEST','COSMIC_UX02_ROOT','COSMIC_UX02_SHOTS','COSMIC_AP03_RECORD_SHELL','COSMIC_STARFORGE_PROJECTS_DIR')) { Remove-Item -Path ("Env:" + $k) -ErrorAction SilentlyContinue }

$verdict = 'FAIL'; $res = $null
if (Test-Path -LiteralPath $result) { try { $res = Get-Content -LiteralPath $result -Raw | ConvertFrom-Json; $verdict = $res.verdict } catch {} }
Write-Host "UX02 self-test: exit=$code verdict=$verdict elapsed=${elapsed}s timedOut=$timedOut result=$result"
if ($res) {
    Write-Host ("  failed_checks={0} total_seconds={1} oracle errors={2} leaks={3} drift={4}" -f $res.failed_checks, [int]$res.total_seconds,
        $res.oracle.recovered_errors, $res.oracle.end_frame_leaks, $res.oracle.context_drift)
    foreach ($p in $res.ids.PSObject.Properties) { Write-Host ("  {0}: {1}" -f $p.Name, $p.Value) }
    foreach ($p in $res.numbers.PSObject.Properties) { Write-Host ("  {0} = {1}" -f $p.Name, $p.Value) }
    foreach ($c in @($res.checks_failed)) { if ($c) { Write-Host "  FAILED CHECK: $c" } }
}
if (Test-Path -LiteralPath $stdout) {
    Get-Content -LiteralPath $stdout | Where-Object { $_ -match 'UX02_SELFTEST_RESULT|FAIL|imgui-error|Assertion' } | Select-Object -First 40 | ForEach-Object { Write-Host "  $_" }
}

# ---- Independent oracles ----
$problems = @()
try {
    # ED05 window-close leg: OnDetach's autosave copy of the dirty scene
    if (-not $res -or -not $res.detach_autosave_expected) { $problems += 'ED05 detach: the result names no expected autosave copy' }
    elseif ($code -eq 0 -and -not $timedOut) {
        $copy = $res.detach_autosave_expected
        if (-not (Test-Path -LiteralPath $copy)) { $problems += "ED05 detach: no autosave copy at $copy after the window-close path" }
        else {
            $txt = Get-Content -LiteralPath $copy -Raw
            if ($txt -notmatch [regex]::Escape($res.detach_marker)) { $problems += "ED05 detach: the copy lacks the marker entity $($res.detach_marker)" }
            else { Write-Host "  ED05 detach: autosave copy present with the marker ($copy, $((Get-Item -LiteralPath $copy).Length) bytes)" }
        }
    }
    $fixtureHashAfter = Get-TreeHash $fixtureSrc
    if ($fixtureHashAfter -ne $fixtureHashBefore) { $problems += "fixture tree changed during the run ($fixtureHashBefore -> $fixtureHashAfter)" }
} catch { $problems += "oracle error: $($_.Exception.Message)" }
$oracleOk = ($problems.Count -eq 0)
Write-Host "UX02 independent oracle: $(if ($oracleOk) { 'PASS' } else { 'FAIL' }) $(if (-not $oracleOk) { ($problems -join '; ') })"
Set-Content -LiteralPath (Join-Path $Output 'ux02-wrapper-oracle.txt') -Encoding utf8 -Value @(
    "fixture tests/fixtures/ux02 sha256/16: $fixtureHashBefore",
    "exit=$code verdict=$verdict elapsed=${elapsed}s timedOut=$timedOut",
    "independent oracle: $(if ($oracleOk) { 'PASS' } else { 'FAIL: ' + ($problems -join '; ') })")

Remove-ChildTree $child
Remove-Item -LiteralPath (Join-Path $ProjectRoot 'Ux02App') -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath (Join-Path $ProjectRoot 'PendulumLab') -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath (Join-Path $ProjectRoot 'Ux02Fixture') -Recurse -Force -ErrorAction SilentlyContinue

$cases = 2
if ($code -eq 0 -and $verdict -eq 'PASS' -and $oracleOk -and -not $timedOut) {
    Write-Host "[doctest] test cases: $cases | $cases passed | 0 failed"
    exit 0
}
$failed = 0; if (-not ($code -eq 0 -and $verdict -eq 'PASS' -and -not $timedOut)) { $failed++ }; if (-not $oracleOk) { $failed++ }
Write-Host "[doctest] test cases: $cases | $($cases - $failed) passed | $failed failed"
exit 1
