# Run-WO10Sample.ps1 — WO-10 (2D stability): the X01 analysis-sample acceptance.
#
#   1. COPY   — Projects/AnalysisSample (the committed source) is copied to an
#               external folder OUTSIDE the SDK source tree (-External, default
#               <repo>\build\wo10-sample-external\AnalysisSample; fresh per run);
#   2. PACKAGE — Starforge.exe is launched with COSMIC_X01_PACKAGE / COSMIC_X01_PROJECT
#               (Projects/Starforge/src/X01PackageSelfTest.cpp): the REAL File > Package
#               path opens the external project, BuildRunner configures + builds it
#               Release against COSMIC_SDK (cmake -S <external> -B <external>\build
#               -DCOSMIC_SDK_DIR=<sdk> -DGAME_OUTPUT_DIR=<external>\build), and
#               Packager::Stage/Finalize stage <sdk>\dist\AnalysisSample;
#   3. RUN    — the staged AnalysisSample.exe is started from a DIFFERENT working
#               directory (a scratch dir, not the dist dir) with COSMIC_X01_SELFTEST
#               (Projects/AnalysisSample/src/X01SelfTest.cpp): load -> play -> pause ->
#               scrub (exact + midpoint) -> export PNG -> finish, JSON result;
#   4. ORACLE — this wrapper, OUT OF PROCESS: re-evaluates the F-TRAJECTORY equations in
#               double for every scrub the app reported and compares the app's marker
#               (float replay) / plot (double series) values against ITS OWN reference
#               within the declared bounds; decodes the exported PNG with System.Drawing
#               and checks the marker colour at the pixel it derives from the reported
#               projection window + the double reference (not the app's pixel), the clear
#               colour far away, and the image size; recomputes the analytic F-SERIES-LARGE
#               extrema from the equations in the metadata; hashes the packaged
#               trajectory.csv against the committed fixture.
# Every artefact lands under -Output. A missing GPU is the runner's capability probe's
# ENVIRONMENT_BLOCKED (the manifest requires gpu-gl), never a pass.
param(
    [Parameter(Mandatory=$true)][string]$Bin,
    [Parameter(Mandatory=$true)][string]$Output,
    [string]$External = '',
    [int]$PackageTimeoutSec = 900,
    [int]$RunTimeoutSec = 180)
$ErrorActionPreference = 'Stop'
$acceptance = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Import-Module (Join-Path $acceptance 'AcceptanceRunner.psm1') -Force
$repo = [IO.Path]::GetFullPath((Join-Path $acceptance '..\..')).TrimEnd('\') + '\'
if (-not $External) { $External = Join-Path $repo 'build\wo10-sample-external\AnalysisSample' }
foreach ($location in @($Bin, $Output, $External)) {
    if (-not [IO.Path]::GetFullPath($location).StartsWith($repo, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'WO-10 fixtures and artifacts must remain inside Cosmic'
    }
}
$source = Join-Path $repo 'Projects\AnalysisSample'
if (-not (Test-Path -LiteralPath (Join-Path $source 'project.cproj'))) { throw "sample source missing: $source" }
$fixtureCsv = Join-Path $source 'data\trajectory.csv'

function Link-ImmutableAsset([string]$Source, [string]$Destination) {
    if (Test-Path -LiteralPath $Destination) { return }
    if ((Get-Item -LiteralPath $Source).PSIsContainer) { New-Item -ItemType Junction -Path $Destination -Target $Source | Out-Null }
    else { New-Item -ItemType HardLink -Path $Destination -Target $Source | Out-Null }
}
function Initialize-EditorChild([string]$Child) {
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
function Start-Bounded([string]$Exe, [string]$WorkDir, [string]$Stdout, [string]$Stderr, [int]$TimeoutSec) {
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $proc = Start-Process -FilePath $Exe -WorkingDirectory $WorkDir -PassThru -NoNewWindow -RedirectStandardOutput $Stdout -RedirectStandardError $Stderr
    $null = $proc.Handle
    $timedOut = $false
    if (-not $proc.WaitForExit($TimeoutSec * 1000)) { try { $proc.Kill($true) } catch {}; $timedOut = $true }
    $proc.WaitForExit()
    return @{ code = $proc.ExitCode; timedOut = $timedOut; seconds = [int]$sw.Elapsed.TotalSeconds }
}

New-Item -ItemType Directory -Force -Path $Output | Out-Null
$problems = New-Object System.Collections.ArrayList
$cases = 4   # package, run, numeric oracle, image+series oracle
$passed = 0

# ---- 1. Copy the sample source outside the SDK tree --------------------------
if (Test-Path -LiteralPath $External) { Remove-Item -LiteralPath $External -Recurse -Force }
New-Item -ItemType Directory -Force -Path $External | Out-Null
Copy-Item -Path (Join-Path $source '*') -Destination $External -Recurse -Force
Get-ChildItem -LiteralPath $External -Recurse -Force -Directory | Where-Object { $_.Name -eq 'build' } | Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
Write-Host "X01 external project: $External (copied from $source)"

# ---- 2. Package through the real editor path -----------------------------------
$child = Join-Path $Output ('scratch-editor-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $child | Out-Null
Initialize-EditorChild $child
$pkgResult = Join-Path $Output 'x01-package-result.json'
if (Test-Path -LiteralPath $pkgResult) { Remove-Item -LiteralPath $pkgResult -Force }
$env:COSMIC_X01_PACKAGE = $pkgResult
$env:COSMIC_X01_PROJECT = $External
$env:COSMIC_SDK         = $repo.TrimEnd('\')
$pkg = Start-Bounded (Join-Path $child 'Starforge.exe') $child (Join-Path $Output 'x01-package-stdout.log') (Join-Path $Output 'x01-package-stderr.log') $PackageTimeoutSec
foreach ($k in @('COSMIC_X01_PACKAGE','COSMIC_X01_PROJECT')) { Remove-Item -Path ("Env:" + $k) -ErrorAction SilentlyContinue }
$pkgRes = $null; $pkgVerdict = 'FAIL'
if (Test-Path -LiteralPath $pkgResult) { try { $pkgRes = Get-Content -LiteralPath $pkgResult -Raw | ConvertFrom-Json; $pkgVerdict = $pkgRes.verdict } catch {} }
Write-Host "X01 package (editor path): exit=$($pkg.code) verdict=$pkgVerdict elapsed=$($pkg.seconds)s timedOut=$($pkg.timedOut)"
if ($pkgRes) {
    Write-Host ("  dist={0} build_seconds={1} failed_checks={2}" -f $pkgRes.dist, $pkgRes.build_seconds, $pkgRes.failed_checks)
    foreach ($c in @($pkgRes.checks_failed)) { if ($c) { Write-Host "  FAILED CHECK: $c" } }
}
Remove-ChildTree $child
if ($pkg.code -eq 0 -and $pkgVerdict -eq 'PASS' -and -not $pkg.timedOut -and $pkgRes -and (Test-Path -LiteralPath $pkgRes.exe)) { $passed++ }
else { [void]$problems.Add("package step failed (exit $($pkg.code), verdict $pkgVerdict, timedOut $($pkg.timedOut))"); }
# The external build really happened outside the SDK source tree.
$extDll = Join-Path $External 'build\Release\AnalysisSample.dll'
if (-not (Test-Path -LiteralPath $extDll)) { [void]$problems.Add("external build output missing: $extDll") }
# Hash the packaged fixture against the committed one.
$stagedCsv = if ($pkgRes) { Join-Path $pkgRes.dist 'assets\projects\AnalysisSample\data\trajectory.csv' } else { '' }
$fixtureHash = Get-FileSha256 $fixtureCsv
$stagedHash = if ($stagedCsv -and (Test-Path -LiteralPath $stagedCsv)) { Get-FileSha256 $stagedCsv } else { 'MISSING' }
Write-Host "  trajectory.csv sha256: committed $fixtureHash / staged $stagedHash"
if ($fixtureHash -ne $stagedHash) { [void]$problems.Add('staged trajectory.csv differs from the committed fixture') }

# ---- 3. Run the staged package from a DIFFERENT working directory ---------------
$runResult = Join-Path $Output 'x01-result.json'
$runOut = Join-Path $Output 'x01-output'
if (Test-Path -LiteralPath $runResult) { Remove-Item -LiteralPath $runResult -Force }
if (Test-Path -LiteralPath $runOut) { Remove-Item -LiteralPath $runOut -Recurse -Force }
New-Item -ItemType Directory -Force -Path $runOut | Out-Null
$elsewhere = Join-Path $Output ('scratch-cwd-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $elsewhere | Out-Null
$runRes = $null; $runVerdict = 'FAIL'; $run = @{ code = -1; timedOut = $false; seconds = 0 }
if ($pkgRes -and (Test-Path -LiteralPath $pkgRes.exe)) {
    $env:COSMIC_X01_SELFTEST = $runResult
    $env:COSMIC_X01_OUTPUT   = $runOut
    $run = Start-Bounded $pkgRes.exe $elsewhere (Join-Path $Output 'x01-run-stdout.log') (Join-Path $Output 'x01-run-stderr.log') $RunTimeoutSec
    foreach ($k in @('COSMIC_X01_SELFTEST','COSMIC_X01_OUTPUT')) { Remove-Item -Path ("Env:" + $k) -ErrorAction SilentlyContinue }
    if (Test-Path -LiteralPath $runResult) { try { $runRes = Get-Content -LiteralPath $runResult -Raw | ConvertFrom-Json; $runVerdict = $runRes.verdict } catch {} }
}
Write-Host "X01 packaged run (cwd=$elsewhere): exit=$($run.code) verdict=$runVerdict elapsed=$($run.seconds)s timedOut=$($run.timedOut)"
if ($runRes) {
    Write-Host ("  exe={0} cwd(app)={1} user_root={2} rows={3} scrubs={4} failed_checks={5}" -f $runRes.exe, $runRes.cwd, $runRes.user_root, $runRes.rows, @($runRes.scrubs).Count, $runRes.failed_checks)
    foreach ($c in @($runRes.checks_failed)) { if ($c) { Write-Host "  FAILED CHECK: $c" } }
}
Remove-Item -LiteralPath $elsewhere -Recurse -Force -ErrorAction SilentlyContinue
if ($run.code -eq 0 -and $runVerdict -eq 'PASS' -and -not $run.timedOut -and $runRes) { $passed++ }
else { [void]$problems.Add("packaged run failed (exit $($run.code), verdict $runVerdict, timedOut $($run.timedOut))") }

# ---- 4a. Numeric oracle (independent double evaluation) -------------------------
$g = 9.80665
function Ref([double]$t) { return @{ x = 30.0 * $t; y = 50.0 * $t - 0.5 * $g * $t * $t; vx = 30.0; vy = 50.0 - $g * $t } }
function UlpF([double]$v) { $f = [single][math]::Abs($v); if ($f -eq 0) { return [double][math]::Pow(2, -149) }; $bits = [BitConverter]::ToInt32([BitConverter]::GetBytes($f), 0); $next = [BitConverter]::ToSingle([BitConverter]::GetBytes($bits + 1), 0); return [double]($next - $f) }
$ulpD = [math]::Pow(2, -16)   # the double ulp at 1e11 (2^36 < 1e11 < 2^37)
$numOk = $false; $numWhy = 'no run result'
if ($runRes) {
    $issues = @(); $n = 0; $worstMarker = 0.0; $worstPlot = 0.0
    if ($runRes.rows -ne 1201) { $issues += "rows $($runRes.rows)" }
    if ([math]::Abs([double]$runRes.origin_offset_m - 1e11) -gt 1) { $issues += "origin offset $($runRes.origin_offset_m)" }
    foreach ($s in @($runRes.scrubs)) {
        $n++
        $t = [double]$s.t; $r = Ref $t
        $mid = [bool]$s.midpoint
        # The plot's selection is a chord between rows: exact at a sample (x linear, y parabola => chord error g/8 dt^2 at a midpoint), + the 1e11 double rounding.
        $chordY = if ($mid) { $g / 8.0 * (1.0 / 120.0) * (1.0 / 120.0) } else { 0.0 }
        $px = [math]::Abs([double]$s.plot.x - $r.x); $py = [math]::Abs([double]$s.plot.y - $r.y)
        if ($px -gt $ulpD + 1e-9 -or $py -gt $chordY + $ulpD + 1e-9) { $issues += ("scrub {0}: plot vs equations {1:E2}/{2:E2}" -f $t, $px, $py) }
        $worstPlot = [math]::Max($worstPlot, [math]::Max($px, $py))
        # The marker (float replay) vs the oracle's chord: float value + float interpolation (2 ulps) + float time (2 ulps * rate) + the generic float bar.
        $chordX = $r.x; $chordYv = if ($mid) { $r.y - $chordY } else { $r.y }
        $barX = 1e-6 + 1e-5 * [math]::Abs($chordX) + 2.0 * 30.0 * (UlpF $t) + 2.0 * (UlpF $chordX) + $ulpD
        $barY = 1e-6 + 1e-5 * [math]::Abs($chordYv) + 2.0 * 50.0 * (UlpF $t) + 2.0 * (UlpF $chordYv) + $ulpD
        $mx = [math]::Abs([double]$s.marker.x - $chordX); $my = [math]::Abs([double]$s.marker.y - $chordYv)
        if ($mx -gt $barX -or $my -gt $barY) { $issues += ("scrub {0}: marker vs oracle {1:E2}/{2:E2} (bars {3:E2}/{4:E2})" -f $t, $mx, $my, $barX, $barY) }
        $worstMarker = [math]::Max($worstMarker, [math]::Max($mx, $my))
        # The displayed local value of the 1e11 world value.
        if ([math]::Abs([double]$s.local.x - $r.x) -gt $ulpD + (UlpF $r.x) -or [math]::Abs([double]$s.local.y - $r.y) -gt $ulpD + (UlpF $r.y)) { $issues += "scrub ${t}: 1e11 local display" }
        if ([math]::Abs([double]$s.marker.vx - 30.0) -gt 1e-4 -or [math]::Abs([double]$s.marker.vy - $r.vy) -gt 1e-3 + 2.0 * $g * (UlpF $t)) { $issues += "scrub ${t}: velocity" }
    }
    if ($n -lt 12) { $issues += "only $n scrubs reported" }
    $expectedTimes = @(0, 1, 119, 120, 600, 611, 1199, 1200 | ForEach-Object { $_ / 120.0 }) + @(0.5, 120.5, 611.5, 1199.5 | ForEach-Object { $_ / 120.0 })
    $got = @($runRes.scrubs | ForEach-Object { [double]$_.t })
    foreach ($e in $expectedTimes) { if (@($got | Where-Object { [math]::Abs($_ - $e) -lt 1e-12 }).Count -eq 0) { $issues += "scrub time $e not reported" } }
    if ([int]$runRes.play.monotonic_violations -ne 0 -or [double]$runRes.play.end -lt 0.4 -or [double]$runRes.play.paused_at -ne [double]$runRes.play.end) { $issues += 'play/pause record' }
    if ($issues.Count -eq 0) { $numOk = $true; $numWhy = ("{0} scrubs re-evaluated in double: worst marker-vs-oracle {1:E2} m, worst plot-vs-equations {2:E2} m" -f $n, $worstMarker, $worstPlot) }
    else { $numWhy = ($issues -join '; ') }
}
Write-Host "X01 numeric oracle (out of process): $(if ($numOk) { 'PASS' } else { 'FAIL' }) - $numWhy"
if ($numOk) { $passed++ } else { [void]$problems.Add("numeric oracle: $numWhy") }

# ---- 4b. Image + series oracle ---------------------------------------------------
$imgOk = $false; $imgWhy = 'no export'
if ($runRes -and $runRes.export -and $runRes.export.ok) {
    try {
        Add-Type -AssemblyName System.Drawing
        $issues = @()
        $e = $runRes.export
        $png = $e.path
        if (-not (Test-Path -LiteralPath $png)) { throw "exported PNG missing: $png" }
        $bmp = New-Object System.Drawing.Bitmap $png
        try {
            if ($bmp.Width -ne [int]$e.width -or $bmp.Height -ne [int]$e.height) { $issues += "PNG size $($bmp.Width)x$($bmp.Height) != reported $($e.width)x$($e.height)" }
            # The marker pixel from the DOUBLE reference + the reported projection window (not the app's pixel).
            $t = [double]$e.t; $r = Ref $t
            $xmin = [double]$e.window.xmin; $xmax = [double]$e.window.xmax; $ymin = [double]$e.window.ymin; $ymax = [double]$e.window.ymax
            $px = ($r.x - $xmin) / ($xmax - $xmin) * [double]$e.width
            $py = (1.0 - ($r.y - $ymin) / ($ymax - $ymin)) * [double]$e.height
            if ([math]::Abs($px - [double]$e.marker_px[0]) -gt 0.5 -or [math]::Abs($py - [double]$e.marker_px[1]) -gt 0.5) { $issues += ("marker px from the reference ({0:F2},{1:F2}) vs reported ({2:F2},{3:F2})" -f $px, $py, $e.marker_px[0], $e.marker_px[1]) }
            $ix = [int][math]::Floor($px); $iy = [int][math]::Floor($py)
            $c = $bmp.GetPixel($ix, $iy)
            $mc = $e.marker_color
            if ([math]::Abs($c.R - [int]$mc[0]) -gt 40 -or [math]::Abs($c.G - [int]$mc[1]) -gt 40 -or [math]::Abs($c.B - [int]$mc[2]) -gt 40) { $issues += "pixel at the reference marker position ($ix,$iy) = ($($c.R),$($c.G),$($c.B)), marker colour ($($mc -join ','))" }
            # The marker is a disc of the reported radius: 3 px off-centre is still marker colour; 3 radii away is not.
            $c2 = $bmp.GetPixel($ix + 3, $iy)
            if ([math]::Abs($c2.R - [int]$mc[0]) -gt 60) { $issues += "pixel 3 px right of the marker is not marker colour" }
            $far = $bmp.GetPixel(2, 2); $cc = $e.clear_color
            if ([math]::Abs($far.R - [int]$cc[0]) -gt 8 -or [math]::Abs($far.G - [int]$cc[1]) -gt 8 -or [math]::Abs($far.B - [int]$cc[2]) -gt 8) { $issues += "far pixel (2,2) = ($($far.R),$($far.G),$($far.B)) is not the clear colour" }
            $rad = [int][math]::Ceiling([double]$e.marker_radius_px)
            $off = $bmp.GetPixel([math]::Min($ix + 4 * $rad, $bmp.Width - 1), [math]::Max($iy - 4 * $rad, 0))
            if ([math]::Abs($off.R - [int]$mc[0]) -le 40 -and [math]::Abs($off.G - [int]$mc[1]) -le 40 -and [math]::Abs($off.B - [int]$mc[2]) -le 40) { $issues += 'marker colour found 4 radii away (marker not localised)' }
            # user:// resolves relative to the app's own directory (portable mode: <exe>/user).
            $userCopy = $e.user_copy
            if (-not [IO.Path]::IsPathRooted($userCopy)) { $userCopy = Join-Path $runRes.cwd $userCopy }
            if (Test-Path -LiteralPath $userCopy) { $u = Get-FileSha256 $userCopy; $p = Get-FileSha256 $png; if ($u -ne $p) { $issues += 'user:// copy differs' } } else { $issues += "user:// export copy missing: $userCopy" }
        } finally { $bmp.Dispose() }
        # Series metadata: recompute the analytic extrema from the equations it declares.
        $meta = Get-Content -LiteralPath $runRes.series_meta -Raw | ConvertFrom-Json
        if ([int]$meta.samples -ne 100000 -or [int]$meta.channels -ne 8 -or @($meta.channel).Count -ne 8) { $issues += 'series meta shape' }
        else {
            $ch = @($meta.channel)
            $check = { param($i, $minV, $maxV, $minMod, $maxMod, $period)
                $c = $ch[$i]
                if ([math]::Abs([double]$c.min.value - $minV) -gt 1e-9 -or ([int]$c.min.index % $period) -ne $minMod) { $script:issues += "series $($c.name) min $($c.min.value)@$($c.min.index)" }
                if ([math]::Abs([double]$c.max.value - $maxV) -gt 1e-9 -or ([int]$c.max.index % $period) -ne $maxMod) { $script:issues += "series $($c.name) max $($c.max.value)@$($c.max.index)" } }
            & $check 0 -1.0 1.0 750 250 1000
            & $check 1 -1.0 1.0 500 0 1000
            & $check 2 0.0 99.999 0 99999 100000
            & $check 3 0.0 0.999 0 999 1000
            & $check 4 -1.0 1.0 0 50000 100000
            if (@($ch[3].discontinuities).Count -ne 99 -or [int]$ch[3].discontinuities[0] -ne 1000) { $issues += 'sawtooth discontinuities' }
            if (@($ch[4].discontinuities).Count -ne 1 -or [int]$ch[4].discontinuities[0] -ne 50000) { $issues += 'step discontinuity' }
            # sin(2 pi t) at index 250 really is 1 (the equation, evaluated here).
            if ([math]::Abs([math]::Sin(2.0 * [math]::PI * 0.25) - 1.0) -gt 1e-12) { $issues += 'oracle sin' }
            if (-not $ch[6].seeded -or -not $ch[7].seeded) { $issues += 'noise channels not seeded' }
            if ($meta.seed -ne '0xA105E21') { $issues += "seed $($meta.seed)" }
        }
        if ($issues.Count -eq 0) { $imgOk = $true; $imgWhy = "PNG $($e.width)x$($e.height): marker colour at the reference pixel ($ix,$iy), clear colour far away, user:// copy identical; series metadata extrema/discontinuities/seed as the equations say" }
        else { $imgWhy = ($issues -join '; ') }
    } catch { $imgWhy = "oracle error: $($_.Exception.Message)" }
}
Write-Host "X01 image + series oracle (out of process): $(if ($imgOk) { 'PASS' } else { 'FAIL' }) - $imgWhy"
if ($imgOk) { $passed++ } else { [void]$problems.Add("image/series oracle: $imgWhy") }

# Evidence copies (the PNG, the metadata, the recording listing).
if ($runRes -and $runRes.export -and (Test-Path -LiteralPath $runRes.export.path)) { Copy-Item -LiteralPath $runRes.export.path -Destination (Join-Path $Output 'x01-trajectory-4.5s.png') -Force }
if ($runRes -and (Test-Path -LiteralPath $runRes.series_meta)) { Copy-Item -LiteralPath $runRes.series_meta -Destination (Join-Path $Output 'series-large.meta.json') -Force }
if ($pkgRes -and (Test-Path -LiteralPath $pkgRes.dist)) {
    Get-ChildItem -LiteralPath $pkgRes.dist -Recurse -File | ForEach-Object { '{0}  {1}' -f $_.Length, $_.FullName.Substring($pkgRes.dist.Length).TrimStart('\','/') } |
        Set-Content -LiteralPath (Join-Path $Output 'x01-package-payload.txt') -Encoding utf8
}
if (Test-Path -LiteralPath (Join-Path $Output 'x01-output')) { Remove-Item -LiteralPath (Join-Path $Output 'x01-output') -Recurse -Force -ErrorAction SilentlyContinue }

$failed = $cases - $passed
Write-Host "[doctest] test cases: $cases | $passed passed | $failed failed"
if ($failed -eq 0) { exit 0 }
foreach ($p in $problems) { Write-Host "  PROBLEM: $p" }
exit 1
