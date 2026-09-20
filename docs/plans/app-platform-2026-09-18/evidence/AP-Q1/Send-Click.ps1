# Send a real mouse click (or key) to a top-level window at window-relative client coordinates,
# via SetCursorPos + mouse_event (GLFW/ImGui see ordinary WM_ input). Used only to drive the
# showcase captures; never used by an acceptance case.
param([Parameter(Mandatory=$true)][string]$Process, [int]$X = -1, [int]$Y = -1, [string]$Key = '', [switch]$Right, [switch]$Double, [string]$Text = '', [int]$DragToX = -1, [int]$DragToY = -1, [string]$TitleLike = '', [int]$OwnerPid = 0)
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class Inp {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, UIntPtr e);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte sc, uint f, UIntPtr e);
  [DllImport("dwmapi.dll")] public static extern int DwmGetWindowAttribute(IntPtr h, int a, out RECT r, int s);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern short VkKeyScan(char c);
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
$hwnd = [IntPtr]::Zero
if ($OwnerPid -gt 0) { $hs = [Inp]::FindPid($TitleLike, [uint32]$OwnerPid); if ($hs.Count -gt 0) { $hwnd = $hs[0] } else { throw "no window of pid $OwnerPid like '$TitleLike'" } }
elseif ($TitleLike) { $hs = [Inp]::Find($TitleLike); if ($hs.Count -gt 0) { $hwnd = $hs[0] } }
if ($hwnd -eq [IntPtr]::Zero) {
    $p = Get-Process -Name $Process -ErrorAction Stop | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
    if (-not $p) { throw "no window for $Process" }
    $hwnd = $p.MainWindowHandle
}
[Inp]::SetForegroundWindow($hwnd) | Out-Null
Start-Sleep -Milliseconds 250
$r = New-Object Inp+RECT
if ([Inp]::DwmGetWindowAttribute($hwnd, 9, [ref]$r, [Runtime.InteropServices.Marshal]::SizeOf($r)) -ne 0) { [Inp]::GetWindowRect($hwnd, [ref]$r) | Out-Null }
if ($X -ge 0 -and $Y -ge 0) {
    [Inp]::SetCursorPos($r.Left + $X, $r.Top + $Y) | Out-Null
    Start-Sleep -Milliseconds 120
    $down = if ($Right) { 0x0008 } else { 0x0002 }; $up = if ($Right) { 0x0010 } else { 0x0004 }
    if ($DragToX -ge 0 -and $DragToY -ge 0) {
        [Inp]::mouse_event($down, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 120
        $steps = 20
        for ($i = 1; $i -le $steps; $i++) { [Inp]::SetCursorPos($r.Left + $X + [int](($DragToX - $X) * $i / $steps), $r.Top + $Y + [int](($DragToY - $Y) * $i / $steps)) | Out-Null; Start-Sleep -Milliseconds 25 }
        Start-Sleep -Milliseconds 120; [Inp]::mouse_event($up, 0, 0, 0, [UIntPtr]::Zero)
        Write-Host "dragged to ($DragToX,$DragToY)"; return
    }
    [Inp]::mouse_event($down, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 60; [Inp]::mouse_event($up, 0, 0, 0, [UIntPtr]::Zero)
    if ($Double) { Start-Sleep -Milliseconds 80; [Inp]::mouse_event($down, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 60; [Inp]::mouse_event($up, 0, 0, 0, [UIntPtr]::Zero) }
}
$vkmap = @{ 'escape' = 0x1B; 'return' = 0x0D; 'tab' = 0x09; 'space' = 0x20; 'delete' = 0x2E; 'f5' = 0x74; 'ctrl' = 0x11 }
if ($Key) {
    $parts = $Key.ToLower().Split('+'); $mods = @(); $main = $parts[-1]
    foreach ($m in $parts[0..($parts.Length - 2)]) { $mods += $vkmap[$m] }
    $vk = if ($vkmap.ContainsKey($main)) { $vkmap[$main] } else { [byte]([Inp]::VkKeyScan([char]$main) -band 0xFF) }
    foreach ($m in $mods) { [Inp]::keybd_event([byte]$m, 0, 0, [UIntPtr]::Zero) }
    [Inp]::keybd_event([byte]$vk, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 50; [Inp]::keybd_event([byte]$vk, 0, 2, [UIntPtr]::Zero)
    foreach ($m in $mods) { [Inp]::keybd_event([byte]$m, 0, 2, [UIntPtr]::Zero) }
}
if ($Text) {
    foreach ($ch in $Text.ToCharArray()) {
        $scan = [Inp]::VkKeyScan($ch); $vk = [byte]($scan -band 0xFF); $shift = (($scan -shr 8) -band 1) -ne 0
        if ($shift) { [Inp]::keybd_event(0x10, 0, 0, [UIntPtr]::Zero) }
        [Inp]::keybd_event($vk, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 25; [Inp]::keybd_event($vk, 0, 2, [UIntPtr]::Zero)
        if ($shift) { [Inp]::keybd_event(0x10, 0, 2, [UIntPtr]::Zero) }
        Start-Sleep -Milliseconds 25
    }
}
Write-Host ("sent to {0} ({1},{2}) key='{3}' text='{4}' window=({5},{6})" -f $Process, $X, $Y, $Key, $Text, $r.Left, $r.Top)
