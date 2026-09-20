# Run-GuideWalkthrough.ps1 — drives docs/guide/pendulumlab-walkthrough.md inside the real
# Starforge editor (GuideWalkthroughSelfTest.cpp), then runs the EXPORTED PendulumLab2.exe from
# a directory that is not the package, clicks its Home > Start button, screenshots it and
# reads its log. Everything lands under -Output:
#   guide-result.json, guide-editor-console.txt, shots/*.png (raw, green ImGui locate rects),
#   exported/run.json, exported/*.png, exported/log-excerpt.txt, exported/files.txt
#
#   powershell -ExecutionPolicy Bypass -File tests\acceptance\fixtures\Run-GuideWalkthrough.ps1 `
#       -Bin build\Runtime\Release -Output <dir> [-ProjectRoot <dir>] [-SkipPackage]
#
# Prerequisites: a Release build of the tree (the editor packages against build\Runtime\Release),
# $env:COSMIC_SDK pointing at this checkout, and the reference sources in Projects/PendulumLab/src.
param(
    [Parameter(Mandatory=$true)][string]$Bin,
    [Parameter(Mandatory=$true)][string]$Output,
    [string]$ProjectRoot = '',
    [int]$TimeoutSec = 1800,
    [switch]$SkipPackage,
    [switch]$ExportedOnly    # re-run only the exported-exe half against an existing dist/ + result JSON
)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) '..\..\..'))
$Bin = [IO.Path]::GetFullPath($Bin)
$Output = [IO.Path]::GetFullPath($Output)
if (-not $ProjectRoot) { $ProjectRoot = Join-Path $Output 'projects' }
New-Item -ItemType Directory -Force -Path $Output, $ProjectRoot, (Join-Path $Output 'shots'), (Join-Path $Output 'exported') | Out-Null
$dist = Join-Path $repo 'dist\PendulumLab2'
$result = Join-Path $Output 'guide-result.json'
if (-not $ExportedOnly) {
if (Test-Path (Join-Path $ProjectRoot 'PendulumLab2')) { Remove-Item -Recurse -Force (Join-Path $ProjectRoot 'PendulumLab2') }
if (Test-Path $dist) { Remove-Item -Recurse -Force $dist }
if (Test-Path $result) { Remove-Item -Force $result }
# A fresh homescreen: the editor's project library is worktree-local (user:// = <bin>/starforge/).
$library = Join-Path $Bin 'starforge\projects.toml'
if (Test-Path $library) { Remove-Item -Force $library }
$env:COSMIC_SDK = $repo
$env:COSMIC_GUIDE_SELFTEST = $result
$env:COSMIC_GUIDE_ROOT = $ProjectRoot
$env:COSMIC_GUIDE_SHOTS = Join-Path $Output 'shots'
$env:COSMIC_GUIDE_REF = Join-Path $repo 'Projects\PendulumLab\src'
if ($SkipPackage) { $env:COSMIC_GUIDE_SKIP_PACKAGE = '1' } else { Remove-Item Env:\COSMIC_GUIDE_SKIP_PACKAGE -ErrorAction SilentlyContinue }

Write-Host "[guide] editor: $Bin\Starforge.exe  project root: $ProjectRoot"
$stdout = Join-Path $Output 'guide-editor-stdout.txt'
$p = Start-Process -FilePath (Join-Path $Bin 'Starforge.exe') -WorkingDirectory $Bin -PassThru -RedirectStandardOutput $stdout -RedirectStandardError (Join-Path $Output 'guide-editor-stderr.txt')
if (-not $p.WaitForExit($TimeoutSec * 1000)) { $p.Kill(); Write-Host "[guide] TIMEOUT after $TimeoutSec s"; exit 2 }
Write-Host "[guide] editor exit $($p.ExitCode)"
Remove-Item Env:\COSMIC_GUIDE_SELFTEST, Env:\COSMIC_GUIDE_ROOT, Env:\COSMIC_GUIDE_SHOTS, Env:\COSMIC_GUIDE_REF -ErrorAction SilentlyContinue
if (-not (Test-Path $result)) { Write-Host '[guide] no result JSON'; exit 3 }
$json = Get-Content $result -Raw | ConvertFrom-Json
Write-Host "[guide] editor verdict: $($json.verdict) ($($json.failed_checks) failed checks, $([int]$json.total_seconds) s)"
if ($SkipPackage) { exit $(if ($json.verdict -eq 'PASS') { 0 } else { 1 }) }
}
$json = Get-Content $result -Raw | ConvertFrom-Json

# ---- the exported exe, from another directory ------------------------------------------------
$exe = Join-Path $dist 'PendulumLab2.exe'
$run = @{ exe = $exe; exists = (Test-Path $exe) }
if (-not (Test-Path $exe)) { $run | ConvertTo-Json | Set-Content (Join-Path $Output 'exported\run.json'); Write-Host '[guide] exported exe missing'; exit 4 }
Get-ChildItem -Recurse -File $dist | ForEach-Object { $_.FullName.Substring($dist.Length + 1) } | Sort-Object | Set-Content (Join-Path $Output 'exported\files.txt')
$run.files = (Get-Content (Join-Path $Output 'exported\files.txt')).Count
$run.sha256_exe = (Get-FileHash -Algorithm SHA256 $exe).Hash
$run.sha256_dll = (Get-FileHash -Algorithm SHA256 (Join-Path $dist 'PendulumLab2.dll')).Hash

Add-Type -AssemblyName System.Drawing
Add-Type -Namespace Win32 -Name Native -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
[DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
[DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
[DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
[DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint data, UIntPtr extra);
[DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
[DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
[DllImport("user32.dll")] public static extern bool ScreenToClient(IntPtr h, ref POINT p);
[DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
[DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
[DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
public struct RECT { public int Left, Top, Right, Bottom; }
public struct POINT { public int X, Y; }
'@
function Shot([IntPtr]$h, [string]$path) {
    $r = New-Object Win32.Native+RECT; [Win32.Native]::GetClientRect($h, [ref]$r) | Out-Null
    $o = New-Object Win32.Native+POINT; $o.X = 0; $o.Y = 0; [Win32.Native]::ClientToScreen($h, [ref]$o) | Out-Null
    $w = $r.Right - $r.Left; $hh = $r.Bottom - $r.Top
    if ($w -le 0 -or $hh -le 0) { return $null }
    # PrintWindow with PW_RENDERFULLCONTENT (DWM composition): the app's own pixels even when
    # another window overlaps it (a second Starforge instance did, during this chapter's runs).
    $bmp = New-Object System.Drawing.Bitmap $w, $hh
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $g.GetHdc()
    [Win32.Native]::PrintWindow($h, $hdc, 2) | Out-Null
    $g.ReleaseHdc($hdc)
    $g.Dispose(); $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
    return @{ x = $o.X; y = $o.Y; w = $w; h = $hh }
}
# A click on the app's canvas: the OS cursor supplies the position (the engine polls it), the
# button edges are posted to the app's own window so they cannot land anywhere else.
function ClickClient([IntPtr]$h, [hashtable]$cl, [double]$fx, [double]$fy) {
    $x = [int]($cl.x + $cl.w * $fx); $y = [int]($cl.y + $cl.h * $fy)
    $lp = [IntPtr](($y - $cl.y) * 65536 + ($x - $cl.x))
    [Win32.Native]::SetCursorPos($x - 4, $y - 4) | Out-Null; Start-Sleep -Milliseconds 150
    [Win32.Native]::SetCursorPos($x, $y) | Out-Null
    [Win32.Native]::PostMessage($h, 0x0200, [IntPtr]0, $lp) | Out-Null; Start-Sleep -Milliseconds 300
    [Win32.Native]::PostMessage($h, 0x0201, [IntPtr]1, $lp) | Out-Null; Start-Sleep -Milliseconds 150
    [Win32.Native]::PostMessage($h, 0x0202, [IntPtr]0, $lp) | Out-Null
    return @{ x = $x; y = $y }
}

# Physical pixels everywhere (window rects, the capture, SetCursorPos): otherwise a scaled display
# virtualises this process's coordinates while the app's are physical and the click lands elsewhere.
[Win32.Native]::SetProcessDPIAware() | Out-Null
$otherDir = Join-Path $Output 'exported\cwd-elsewhere'
New-Item -ItemType Directory -Force -Path $otherDir | Out-Null
# Portable mode: the staged user/ folder next to the exe is the app's user:// root (AP-P1 §12).
$logDir = Join-Path $dist 'user\logs'
$logsBefore = @(Get-ChildItem -Path $logDir -Filter '*.log' -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName })
Write-Host "[guide] running $exe from $otherDir"
$app = Start-Process -FilePath $exe -WorkingDirectory $otherDir -PassThru
Start-Sleep -Seconds 6
$app.Refresh()
$h = $app.MainWindowHandle
$run.window_found = ($h -ne [IntPtr]::Zero)
if ($h -ne [IntPtr]::Zero) {
    try { (New-Object -ComObject WScript.Shell).AppActivate($app.Id) | Out-Null } catch {}
    [Win32.Native]::SetForegroundWindow($h) | Out-Null; Start-Sleep -Milliseconds 800
    $cl = Shot $h (Join-Path $Output 'exported\19-exported-home.png')
    $run.client = $cl
    # Home: the Start button is the FIRST button-tinted run (UiImage tint 0.16/0.19/0.25 over the
    # canvas) down the window's centre column of the screenshot just taken: click the middle of it,
    # in the same coordinate space the capture used.
    $bmp = New-Object System.Drawing.Bitmap (Join-Path $Output 'exported\19-exported-home.png')
    $cx = [int]($bmp.Width / 2) + [int]($bmp.Width * 0.045); $runStart = -1; $runEnd = -1   # right of the label text, inside the button
    for ($y = 0; $y -lt $bmp.Height; $y++) {
        $c = $bmp.GetPixel($cx, $y)
        $isBtn = ([Math]::Abs($c.R - 39) -le 12 -and [Math]::Abs($c.G - 46) -le 12 -and [Math]::Abs($c.B - 61) -le 12)
        if ($isBtn -and $runStart -lt 0) { $runStart = $y }
        if (-not $isBtn -and $runStart -ge 0) { if (($y - $runStart) -ge 20) { $runEnd = $y; break } else { $runStart = -1 } }
    }
    $bmp.Dispose()
    if ($runEnd -gt 0) {
        $fy = (($runStart + $runEnd) / 2.0) / $cl.h
        $run.start_button_rows = @($runStart, $runEnd)
        $run.click = ClickClient $h $cl 0.5 $fy
    } else {
        $run.start_button_rows = 'not found'
        $run.click = ClickClient $h $cl 0.5 0.70   # the anchor arithmetic (canvas 0.68 below ~54 px of chrome)
    }
    Start-Sleep -Seconds 4
    $null = Shot $h (Join-Path $Output 'exported\20-exported-lab.png')
    Start-Sleep -Seconds 2
    $null = Shot $h (Join-Path $Output 'exported\21-exported-lab-later.png')
    # Escape -> Home
    [Win32.Native]::PostMessage($h, 0x0100, [IntPtr]0x1B, [IntPtr]0x00010001) | Out-Null; Start-Sleep -Milliseconds 80
    [Win32.Native]::PostMessage($h, 0x0101, [IntPtr]0x1B, [IntPtr]0xC0010001) | Out-Null
    Start-Sleep -Seconds 2
    $null = Shot $h (Join-Path $Output 'exported\22-exported-home-again.png')
}
if (-not $app.HasExited) { $app.CloseMainWindow() | Out-Null; if (-not $app.WaitForExit(8000)) { $app.Kill(); $run.killed = $true } }
$run.exit_code = $app.ExitCode
$logsAfter = @(Get-ChildItem -Path $logDir -Filter '*.log' -ErrorAction SilentlyContinue | Sort-Object LastWriteTime | ForEach-Object { $_.FullName })
$run.logs = @($logsAfter | Where-Object { $logsBefore -notcontains $_ })
$excerpt = @()
foreach ($l in $run.logs) { $excerpt += "==== $l"; $excerpt += (Get-Content $l | Select-String -Pattern 'Flow|flow|Pendulum|Service|boot|Project|Loaded|Screen|state|error|Error|warn' | Select-Object -First 60 | ForEach-Object { $_.Line }) }
$excerpt | Set-Content (Join-Path $Output 'exported\log-excerpt.txt')
$run.wrote_into_cwd = @(Get-ChildItem -Recurse -File $otherDir -ErrorAction SilentlyContinue).Count
$run | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $Output 'exported\run.json')
Write-Host "[guide] exported exe exit $($app.ExitCode); window found: $($run.window_found); files in package: $($run.files)"
exit $(if ($json.verdict -eq 'PASS' -and $app.ExitCode -eq 0) { 0 } else { 1 })
