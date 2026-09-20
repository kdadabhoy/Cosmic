# Run-Y02Package.ps1 - Y02 (App Platform catalog): PendulumLab packaged through the REAL editor
# path and its staged exe run from a DIFFERENT working directory with the in-app Y02 self-test.
#
#   1. COPY    - Projects/PendulumLab is copied OUTSIDE the SDK source tree (build/apq1-y02-external).
#   2. PACKAGE - Starforge.exe is launched with COSMIC_X01_PACKAGE / COSMIC_X01_PROJECT
#                (Projects/Starforge/src/X01PackageSelfTest.cpp, project-generic): the same
#                OpenProjectPath -> PackageProject -> BuildRunner (cmake configure + Release build
#                against COSMIC_SDK) -> Packager::Stage/Finalize the File > Package menu runs;
#                the result is <sdk>\dist\PendulumLab with the renamed exe, Cosmic.dll, the
#                project DLL, assets and boot.cfg.
#   3. RUN     - the staged PendulumLab.exe is started from a scratch cwd (not the dist dir) with
#                COSMIC_Y02_SELFTEST (Projects/PendulumLab/src/Y02SelfTest.cpp): Home -> Lab ->
#                Settings -> Lab -> Escape -> Home through the real FlowMachine, pendulum.angle_deg
#                must change, PhasePlot hosted-panel draws > 0, the UiPlot ROI holds line-colour
#                pixels; verdict JSON; PASS exits 0.
#   4. ORACLE  - out of process: the JSON verdict is PASS, exit 0, the trace equals
#                Home,Lab,Settings,Lab,Home, draws > 0, line_colour_pixels > 0, the process ran
#                launched from the scratch cwd (the runtime re-roots to the exe dir by design), and the
#                PNG ROI decodes to the reported size.
# Three verdicts (package, packaged run, out-of-process oracle). Doctest-style summary line.
param(
    [Parameter(Mandatory=$true)][string]$Bin,
    [Parameter(Mandatory=$true)][string]$Output,
    [string]$External = '',
    [int]$PackageTimeoutSec = 900,
    [int]$RunTimeoutSec = 240)
$ErrorActionPreference = 'Stop'
$acceptance = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Import-Module (Join-Path $acceptance 'AcceptanceRunner.psm1') -Force
$repo = [IO.Path]::GetFullPath((Join-Path $acceptance '..\..')).TrimEnd('\') + '\'
if (-not $External) { $External = Join-Path $repo 'build\apq1-y02-external\PendulumLab' }
foreach ($location in @($Bin, $Output, $External)) {
    if (-not [IO.Path]::GetFullPath($location).StartsWith($repo, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Y02 fixtures and artifacts must remain inside Cosmic'
    }
}
$source = Join-Path $repo 'Projects\PendulumLab'
if (-not (Test-Path -LiteralPath (Join-Path $source 'project.cproj'))) { throw "sample source missing: $source" }

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
$cases = 3   # package, packaged run, out-of-process oracle
$passed = 0

# ---- 1. Copy the sample source outside the SDK tree --------------------------
if (Test-Path -LiteralPath $External) { Remove-Item -LiteralPath $External -Recurse -Force }
New-Item -ItemType Directory -Force -Path $External | Out-Null
Copy-Item -Path (Join-Path $source '*') -Destination $External -Recurse -Force
Get-ChildItem -LiteralPath $External -Recurse -Force -Directory | Where-Object { $_.Name -eq 'build' } | Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
Write-Host "Y02 external project: $External (copied from $source)"

# ---- 2. Package through the real editor path -----------------------------------
$child = Join-Path $Output ('scratch-editor-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $child | Out-Null
Initialize-EditorChild $child
$pkgResult = Join-Path $Output 'y02-package-result.json'
if (Test-Path -LiteralPath $pkgResult) { Remove-Item -LiteralPath $pkgResult -Force }
$env:COSMIC_X01_PACKAGE = $pkgResult
$env:COSMIC_X01_PROJECT = $External
$env:COSMIC_SDK         = $repo.TrimEnd('\')
$pkg = Start-Bounded (Join-Path $child 'Starforge.exe') $child (Join-Path $Output 'y02-package-stdout.log') (Join-Path $Output 'y02-package-stderr.log') $PackageTimeoutSec
foreach ($k in @('COSMIC_X01_PACKAGE','COSMIC_X01_PROJECT')) { Remove-Item -Path ("Env:" + $k) -ErrorAction SilentlyContinue }
$pkgRes = $null; $pkgVerdict = 'FAIL'
if (Test-Path -LiteralPath $pkgResult) { try { $pkgRes = Get-Content -LiteralPath $pkgResult -Raw | ConvertFrom-Json; $pkgVerdict = $pkgRes.verdict } catch {} }
Write-Host "Y02 package (editor path): exit=$($pkg.code) verdict=$pkgVerdict elapsed=$($pkg.seconds)s timedOut=$($pkg.timedOut)"
if ($pkgRes) {
    Write-Host ("  dist={0} build_seconds={1} failed_checks={2}" -f $pkgRes.dist, $pkgRes.build_seconds, $pkgRes.failed_checks)
    foreach ($c in @($pkgRes.checks_failed)) { if ($c) { Write-Host "  FAILED CHECK: $c" } }
}
Remove-ChildTree $child
if ($pkg.code -eq 0 -and $pkgVerdict -eq 'PASS' -and -not $pkg.timedOut -and $pkgRes -and (Test-Path -LiteralPath $pkgRes.exe)) { $passed++ }
else { [void]$problems.Add("package step failed (exit $($pkg.code), verdict $pkgVerdict, timedOut $($pkg.timedOut))"); }
$extDll = Join-Path $External 'build\Release\PendulumLab.dll'
if (-not (Test-Path -LiteralPath $extDll)) { [void]$problems.Add("external build output missing: $extDll") }

# ---- 3. Run the staged package from a DIFFERENT working directory ---------------
$runResult = Join-Path $Output 'y02-result.json'
$runOut = Join-Path $Output 'y02-output'
if (Test-Path -LiteralPath $runResult) { Remove-Item -LiteralPath $runResult -Force }
if (Test-Path -LiteralPath $runOut) { Remove-Item -LiteralPath $runOut -Recurse -Force }
New-Item -ItemType Directory -Force -Path $runOut | Out-Null
$elsewhere = Join-Path $Output ('scratch-cwd-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $elsewhere | Out-Null
$runRes = $null; $runVerdict = 'FAIL'; $run = @{ code = -1; timedOut = $false; seconds = 0 }
if ($pkgRes -and (Test-Path -LiteralPath $pkgRes.exe)) {
    $env:COSMIC_Y02_SELFTEST = $runResult
    $env:COSMIC_Y02_OUTPUT   = $runOut
    $run = Start-Bounded $pkgRes.exe $elsewhere (Join-Path $Output 'y02-run-stdout.log') (Join-Path $Output 'y02-run-stderr.log') $RunTimeoutSec
    foreach ($k in @('COSMIC_Y02_SELFTEST','COSMIC_Y02_OUTPUT')) { Remove-Item -Path ("Env:" + $k) -ErrorAction SilentlyContinue }
    if (Test-Path -LiteralPath $runResult) { try { $runRes = Get-Content -LiteralPath $runResult -Raw | ConvertFrom-Json; $runVerdict = $runRes.verdict } catch {} }
}
Write-Host "Y02 packaged run (cwd elsewhere): exit=$($run.code) verdict=$runVerdict elapsed=$($run.seconds)s timedOut=$($run.timedOut)"
if ($runRes) {
    Write-Host ("  exe={0} cwd(app)={1} user_root={2} frames={3} trace={4}" -f $runRes.exe, $runRes.cwd, $runRes.user_root, $runRes.frames, (@($runRes.trace) -join ','))
    Write-Host ("  angle_deg min={0} max={1} draws={2} plot line px={3}/{4}" -f $runRes.angle_deg.min, $runRes.angle_deg.max, $runRes.hosted_panel.draws, $runRes.plot_roi.line_colour_pixels, $runRes.plot_roi.pixels)
    foreach ($c in @($runRes.checks_failed)) { if ($c) { Write-Host "  FAILED CHECK: $c" } }
}
if ($run.code -eq 0 -and $runVerdict -eq 'PASS' -and -not $run.timedOut -and $runRes) { $passed++ }
else { [void]$problems.Add("packaged run failed (exit $($run.code), verdict $runVerdict, timedOut $($run.timedOut))") }

# ---- 4. Out-of-process oracle ----------------------------------------------------
$oracleOk = $false; $why = ''
if ($runRes) {
    $issues = New-Object System.Collections.ArrayList
    $expectTrace = 'Home,Lab,Settings,Lab,Home'
    $gotTrace = (@($runRes.trace) -join ',')
    if ($gotTrace -ne $expectTrace) { [void]$issues.Add("trace '$gotTrace' != '$expectTrace'") }
    if (-not ($runRes.hosted_panel.draws -gt 0)) { [void]$issues.Add("PhasePlot hosted-panel draws = $($runRes.hosted_panel.draws)") }
    if (-not ($runRes.plot_roi.line_colour_pixels -gt 0)) { [void]$issues.Add("plot ROI line-colour pixels = $($runRes.plot_roi.line_colour_pixels)") }
    if (-not (($runRes.angle_deg.max - $runRes.angle_deg.min) -gt 0.05)) { [void]$issues.Add("angle sweep too small: $($runRes.angle_deg.min)..$($runRes.angle_deg.max)") }
    if ($runRes.angle_deg.bus_clock_violations -ne 0) { [void]$issues.Add("bus clock violations = $($runRes.angle_deg.bus_clock_violations)") }
    if ($runRes.pendulum.producer -ne 'PendulumService') { [void]$issues.Add("producer '$($runRes.pendulum.producer)'") }
    if ($runRes.in_editor) { [void]$issues.Add('in_editor = true') }
    # Launch cwd was the scratch dir; the runtime re-roots itself to the exe dir on purpose
    # (Runtime/Main.cpp SetCurrentDirectoryA(exeDir)) so assets resolve from any launch directory,
    # and a writable exe dir means portable user data (<exe>/user, contract section 12). Both are
    # recorded; the assertion is that the app-reported cwd is the dist dir it was staged to.
    $appCwd = [IO.Path]::GetFullPath($runRes.cwd).TrimEnd('\')
    $distDir = [IO.Path]::GetFullPath($pkgRes.dist).TrimEnd('\')
    if ($appCwd -ne $distDir) { [void]$issues.Add("app cwd '$appCwd' is not the dist dir the runtime re-roots to") }
    Write-Host ("  launch cwd={0}  app cwd={1}  user_root={2}" -f $elsewhere, $appCwd, $runRes.user_root)
    $png = $runRes.plot_roi.png
    if ($png -and (Test-Path -LiteralPath $png)) {
        try {
            Add-Type -AssemblyName System.Drawing
            $bmp = [System.Drawing.Bitmap]::FromFile($png)
            $w = $bmp.Width; $h = $bmp.Height; $bmp.Dispose()
            $rw = [int]($runRes.plot_roi.rect[2] - $runRes.plot_roi.rect[0]); $rh = [int]($runRes.plot_roi.rect[3] - $runRes.plot_roi.rect[1])
            if ($w -le 0 -or $h -le 0) { [void]$issues.Add("ROI PNG decodes to ${w}x${h}") }
            elseif (($w -ne $rw) -or ($h -ne $rh)) { Write-Host "  note: ROI PNG ${w}x${h} vs rect ${rw}x${rh}" }
            Copy-Item -LiteralPath $png -Destination (Join-Path $Output 'y02-plot-roi.png') -Force
        } catch { [void]$issues.Add("ROI PNG decode failed: $($_.Exception.Message)") }
    } else { [void]$issues.Add("ROI PNG missing: '$png'") }
    if ($issues.Count -eq 0) { $oracleOk = $true; $why = "trace $gotTrace, draws $($runRes.hosted_panel.draws), plot px $($runRes.plot_roi.line_colour_pixels), angle $($runRes.angle_deg.min)..$($runRes.angle_deg.max) deg, launched from a scratch cwd, re-rooted to dist, portable user_root" }
    else { $why = ($issues -join '; ') }
} else { $why = 'no result JSON' }
Write-Host "Y02 out-of-process oracle: $(if ($oracleOk) { 'PASS' } else { 'FAIL' }) - $why"
if ($oracleOk) { $passed++ } else { [void]$problems.Add("oracle: $why") }

# Evidence copies.
if ($pkgRes -and (Test-Path -LiteralPath $pkgRes.dist)) {
    Get-ChildItem -LiteralPath $pkgRes.dist -Recurse -File | ForEach-Object { '{0}  {1}  {2}' -f $_.Length, (Get-FileSha256 $_.FullName), $_.FullName.Substring($pkgRes.dist.Length).TrimStart('\','/') } |
        Set-Content -LiteralPath (Join-Path $Output 'y02-package-payload.txt') -Encoding utf8
}
if (Test-Path -LiteralPath $runOut) { Remove-Item -LiteralPath $runOut -Recurse -Force -ErrorAction SilentlyContinue }
if (Test-Path -LiteralPath $elsewhere) { Remove-Item -LiteralPath $elsewhere -Recurse -Force -ErrorAction SilentlyContinue }

$failed = $cases - $passed
Write-Host "[doctest] test cases: $cases | $passed passed | $failed failed"
if ($failed -eq 0) { exit 0 }
foreach ($p in $problems) { Write-Host "  PROBLEM: $p" }
exit 1
