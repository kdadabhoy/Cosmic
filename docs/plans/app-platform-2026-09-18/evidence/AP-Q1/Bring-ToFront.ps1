# Keep every top-level window of the named processes in the foreground for -Seconds (the WO-07 P01
# pixel probe and the L05 editor click loop need an unoccluded, focused window; the chain runs from
# a hidden shell under other desktop windows). Foregrounding only; no input is sent.
param([string[]]$Processes = @('CosmicTests','Starforge','SF_Telem'), [int]$Seconds = 900)
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class Fg { [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h); [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow(); }
"@
$end = (Get-Date).AddSeconds($Seconds)
while ((Get-Date) -lt $end) {
    foreach ($n in $Processes) {
        foreach ($p in (Get-Process -Name $n -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 })) {
            if ([Fg]::GetForegroundWindow() -ne $p.MainWindowHandle) { [Fg]::SetForegroundWindow($p.MainWindowHandle) | Out-Null }
        }
    }
    Start-Sleep -Milliseconds 500
}
