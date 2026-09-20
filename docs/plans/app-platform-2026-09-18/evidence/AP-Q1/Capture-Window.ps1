# Capture one top-level window (by process name, optional title substring) or the whole primary
# screen to a lossless PNG; if it exceeds 1 MB it is downscaled in 10 % steps until it fits.
param(
    [Parameter(Mandatory=$true)][string]$Out,
    [string]$Process = '',
    [string]$TitleLike = '',
    [switch]$Screen,
    [int]$MaxBytes = 1048576,
    [int]$OwnerPid = 0
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class Win32Cap {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
  [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr hWnd);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
  [DllImport("dwmapi.dll")] public static extern int DwmGetWindowAttribute(IntPtr hwnd, int attr, out RECT rect, int size);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  public static System.Collections.Generic.List<IntPtr> FindPid(string like, uint pid) {
    var found = new System.Collections.Generic.List<IntPtr>();
    EnumWindows((h, l) => { if (!IsWindowVisible(h)) return true; uint p; GetWindowThreadProcessId(h, out p); if (p != pid) return true; var sb = new System.Text.StringBuilder(512); GetWindowText(h, sb, 512); if (sb.ToString().IndexOf(like, StringComparison.OrdinalIgnoreCase) >= 0) found.Add(h); return true; }, IntPtr.Zero);
    return found; }
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, System.Text.StringBuilder s, int n);
  public static System.Collections.Generic.List<IntPtr> Find(string like) {
    var found = new System.Collections.Generic.List<IntPtr>();
    EnumWindows((h, l) => { if (!IsWindowVisible(h)) return true; var sb = new System.Text.StringBuilder(512); GetWindowText(h, sb, 512); if (sb.ToString().IndexOf(like, StringComparison.OrdinalIgnoreCase) >= 0) found.Add(h); return true; }, IntPtr.Zero);
    return found; }
}
"@
$rect = $null
if (-not $Screen) {
    $h = [IntPtr]::Zero
    if ($OwnerPid -gt 0) { $hs = [Win32Cap]::FindPid($TitleLike, [uint32]$OwnerPid); if ($hs.Count -gt 0) { $h = $hs[0] } else { throw "no window of pid $OwnerPid like '$TitleLike'" } }
    elseif ($TitleLike) { $hs = [Win32Cap]::Find($TitleLike); if ($hs.Count -gt 0) { $h = $hs[0] } }
    if ($h -eq [IntPtr]::Zero) {
        $procs = Get-Process -Name $Process -ErrorAction Stop | Where-Object { $_.MainWindowHandle -ne 0 }
        if ($TitleLike) { $procs = $procs | Where-Object { $_.MainWindowTitle -like "*$TitleLike*" } }
        $p = $procs | Select-Object -First 1
        if (-not $p) { throw "no window for process '$Process' title like '$TitleLike'" }
        $h = $p.MainWindowHandle
    }
    if ([Win32Cap]::IsIconic($h)) { [Win32Cap]::ShowWindow($h, 9) | Out-Null }
    [Win32Cap]::SetForegroundWindow($h) | Out-Null
    Start-Sleep -Milliseconds 400
    $r = New-Object Win32Cap+RECT
    # DWMWA_EXTENDED_FRAME_BOUNDS = 9 (excludes the invisible resize border)
    if ([Win32Cap]::DwmGetWindowAttribute($h, 9, [ref]$r, [Runtime.InteropServices.Marshal]::SizeOf($r)) -ne 0) { [Win32Cap]::GetWindowRect($h, [ref]$r) | Out-Null }
    $rect = New-Object Drawing.Rectangle($r.Left, $r.Top, ($r.Right - $r.Left), ($r.Bottom - $r.Top))
} else {
    $rect = [Windows.Forms.Screen]::PrimaryScreen.Bounds
}
$bmp = New-Object Drawing.Bitmap($rect.Width, $rect.Height)
$g = [Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($rect.Location, [Drawing.Point]::Empty, $rect.Size)
$g.Dispose()
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Out) | Out-Null
$bmp.Save($Out, [Drawing.Imaging.ImageFormat]::Png)
$scale = 1.0
while ((Get-Item -LiteralPath $Out).Length -gt $MaxBytes -and $scale -gt 0.3) {
    $scale -= 0.1
    $w = [int]($bmp.Width * $scale); $hh = [int]($bmp.Height * $scale)
    $small = New-Object Drawing.Bitmap($w, $hh)
    $sg = [Drawing.Graphics]::FromImage($small); $sg.InterpolationMode = 'HighQualityBicubic'
    $sg.DrawImage($bmp, 0, 0, $w, $hh); $sg.Dispose()
    $small.Save($Out, [Drawing.Imaging.ImageFormat]::Png); $small.Dispose()
}
$bmp.Dispose()
$fi = Get-Item -LiteralPath $Out
Write-Host ("{0}  {1}x{2} window, {3} bytes, scale {4:0.0}" -f $fi.Name, $rect.Width, $rect.Height, $fi.Length, $scale)
