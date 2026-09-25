# Run-UX01Editor.ps1 — UX-01 (UX & Shipping): the flow-editor / Editors-host cases FE03, FE04
# and FE05 driven inside the REAL Starforge editor (Projects/Starforge/src/UX01EditorSelfTest.cpp),
# plus the OUT-OF-PROCESS oracles:
#
#   * the result JSON's verdict, per-ID table (FE03 / FE04 / FE05) and WO-07 oracle counters;
#   * FE04: the PNG exists and is the 1920x1080 editor window (PowerShell reads its size);
#   * the PendulumLab copy's flows/Main.cflow is byte-identical to Projects/PendulumLab's
#     (opening, hiding, re-opening and closing the flow document never wrote it);
#   * FE05: exactly one new .cflow was created by New > Flow and it parses as a one-state flow.
#
# The editor writes './logs', './.starforge' and user:// (imgui.ini, layouts, the project
# library) relative to its CWD, so it runs from a fresh child CWD with the runtime assets
# junction-linked in (the WO-07 L02 / AP-03 pattern): fresh prefs every run.
param(
    [Parameter(Mandatory=$true)][string]$Bin,
    [Parameter(Mandatory=$true)][string]$Output,
    [int]$TimeoutSec = 600,
    [string]$ProjectRoot = ''
)
$ErrorActionPreference = 'Stop'
$acceptance = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$repo = [IO.Path]::GetFullPath((Join-Path $acceptance '..\..')).TrimEnd('\') + '\'
if (-not $ProjectRoot) { $ProjectRoot = Join-Path $Output 'ux01' }
foreach ($location in @($Bin, $Output, $ProjectRoot)) {
    if (-not [IO.Path]::GetFullPath($location).StartsWith($repo, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'UX-01 fixtures and artifacts must remain inside Cosmic'
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

$result = Join-Path $Output 'ux01-result.json'
$png    = Join-Path $Output 'fe04-flow-editor.png'
$stdout = Join-Path $Output 'ux01-stdout.log'
foreach ($f in @($result, $png)) { if (Test-Path -LiteralPath $f) { Remove-Item -LiteralPath $f -Force } }

$env:COSMIC_UX01_SELFTEST = $result
$env:COSMIC_UX01_ROOT     = $ProjectRoot
$env:COSMIC_UX01_SHOTS    = $Output
$env:COSMIC_STARFORGE_PROJECTS_DIR = $ProjectRoot
$env:COSMIC_SDK           = $repo.TrimEnd('\')
$exe = Join-Path $child 'Starforge.exe'
$sw = [Diagnostics.Stopwatch]::StartNew()
$proc = Start-Process -FilePath $exe -WorkingDirectory $child -PassThru -NoNewWindow `
    -RedirectStandardOutput $stdout -RedirectStandardError (Join-Path $Output 'ux01-stderr.log')
$null = $proc.Handle
$timedOut = $false
if (-not $proc.WaitForExit($TimeoutSec * 1000)) { try { $proc.Kill($true) } catch {}; $timedOut = $true }
$proc.WaitForExit()
$code = $proc.ExitCode
$elapsed = [int]$sw.Elapsed.TotalSeconds
foreach ($k in @('COSMIC_UX01_SELFTEST','COSMIC_UX01_ROOT','COSMIC_UX01_SHOTS','COSMIC_STARFORGE_PROJECTS_DIR')) { Remove-Item -Path ("Env:" + $k) -ErrorAction SilentlyContinue }

$verdict = 'FAIL'; $res = $null
if (Test-Path -LiteralPath $result) { try { $res = Get-Content -LiteralPath $result -Raw | ConvertFrom-Json; $verdict = $res.verdict } catch {} }
Write-Host "UX01 self-test: exit=$code verdict=$verdict elapsed=${elapsed}s timedOut=$timedOut result=$result"
if ($res) {
    Write-Host ("  failed_checks={0} total_seconds={1} oracle errors={2} leaks={3} drift={4}" -f $res.failed_checks, [int]$res.total_seconds,
        $res.oracle.recovered_errors, $res.oracle.end_frame_leaks, $res.oracle.context_drift)
    foreach ($p in $res.ids.PSObject.Properties) { Write-Host ("  {0}: {1}" -f $p.Name, $p.Value) }
    foreach ($p in $res.measured.PSObject.Properties) { Write-Host ("  measured {0}: {1}" -f $p.Name, $p.Value) }
    foreach ($c in @($res.checks_failed)) { if ($c) { Write-Host "  FAILED CHECK: $c" } }
}
if (Test-Path -LiteralPath $stdout) {
    Get-Content -LiteralPath $stdout | Where-Object { $_ -match 'UX01_SELFTEST_RESULT|FAIL|imgui-error|Assertion' } | Select-Object -First 40 | ForEach-Object { Write-Host "  $_" }
}

# ---- Independent oracles ----
$problems = @()
$proj = Join-Path $ProjectRoot 'PendulumLab'
try {
    # FE04 PNG: present, the 1920x1080 editor window
    if (-not (Test-Path -LiteralPath $png)) { $problems += 'FE04: no PNG' }
    else {
        Add-Type -AssemblyName System.Drawing
        $img = [System.Drawing.Image]::FromFile($png)
        $dims = "$($img.Width)x$($img.Height)"; $img.Dispose()
        Write-Host "  FE04 PNG $dims"
        if ($dims -ne '1920x1080') { $problems += "FE04: PNG is $dims, expected the 1920x1080 window" }
    }
    # the flow document was opened, hidden, re-opened and closed without a write
    $srcFlow = Join-Path $repo 'Projects\PendulumLab\flows\Main.cflow'
    $dstFlow = Join-Path $proj 'flows\Main.cflow'
    if (-not (Test-Path -LiteralPath $dstFlow)) { $problems += 'copy: flows/Main.cflow missing' }
    elseif ((Get-FileHash -Algorithm SHA256 $srcFlow).Hash -ne (Get-FileHash -Algorithm SHA256 $dstFlow).Hash) { $problems += 'FE03: flows/Main.cflow was rewritten' }
    # FE05: exactly one new flow, a one-state flow
    $new = @(Get-ChildItem -LiteralPath $proj -Recurse -File -Filter '*.cflow' | Where-Object { $_.FullName -ne $dstFlow })
    if ($new.Count -ne 1) { $problems += "FE05: $($new.Count) new .cflow file(s), expected 1" }
    else {
        $j = Get-Content -LiteralPath $new[0].FullName -Raw | ConvertFrom-Json
        if (@($j.states).Count -ne 1 -or $j.start -ne 'Start') { $problems += 'FE05: the new flow is not the default one-state flow' }
        Write-Host "  FE05 new flow: $($new[0].FullName.Substring($proj.Length + 1))"
    }
} catch { $problems += "oracle error: $($_.Exception.Message)" }
$oracleOk = ($problems.Count -eq 0)
Write-Host "UX01 independent oracle: $(if ($oracleOk) { 'PASS' } else { 'FAIL' }) $(if (-not $oracleOk) { ($problems -join '; ') })"

Remove-ChildTree $child
Remove-Item -LiteralPath $ProjectRoot -Recurse -Force -ErrorAction SilentlyContinue

# One doctest-style summary per acceptance ID (FE03 / FE04 / FE05) + the independent oracle.
$cases = 4
$failed = 0
foreach ($id in @('FE03','FE04','FE05')) { if (-not ($res -and $res.ids.$id -eq 'PASS')) { $failed++ } }
if (-not ($code -eq 0 -and $verdict -eq 'PASS' -and -not $timedOut) -and $failed -eq 0) { $failed++ }
if (-not $oracleOk) { $failed++ }
if ($failed -gt $cases) { $failed = $cases }
Write-Host "[doctest] test cases: $cases | $($cases - $failed) passed | $failed failed"
exit $(if ($failed -eq 0) { 0 } else { 1 })
