# Run-UX03Launcher.ps1 — UX-03 (UX & Shipping) LH01 W: the REAL Cosmic Launcher, started
# the way a developer starts it (CosmicApp.exe, no arguments, CWD = the runtime dir — the
# project scan reads the CWD, LauncherLayer::ScanForProjects), lists the real projects and
# none of the test fixtures the test build puts beside it (KI-77).
#
# Oracle: the launcher's own found-list log line
#     LauncherLayer: scan found <N> project(s): [<A>, <B>, ...]
# read from the process's new logs\Cosmic_<stamp>.log (and its redirected stdout). The list
# must contain Starforge and SF_Telem and no name ending in "Fixture" (nor WO07NoExport).
# The per-fixture skip lines ("skipped test fixture '<name>'") are counted and reported.
# Only the process this script started is closed (CloseMainWindow, then Kill of that PID).
param(
    [Parameter(Mandatory=$true)][string]$Bin,
    [Parameter(Mandatory=$true)][string]$Output,
    [int]$TimeoutSec = 90
)
$ErrorActionPreference = 'Stop'
$acceptance = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$repo = [IO.Path]::GetFullPath((Join-Path $acceptance '..\..')).TrimEnd('\') + '\'
foreach ($location in @($Bin, $Output)) {
    if (-not [IO.Path]::GetFullPath($location).StartsWith($repo, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'UX-03 fixtures and artifacts must remain inside Cosmic'
    }
}
New-Item -ItemType Directory -Force -Path $Output | Out-Null
$Bin = [IO.Path]::GetFullPath($Bin)

$problems = @()
$exe = Join-Path $Bin 'CosmicApp.exe'
if (-not (Test-Path -LiteralPath $exe)) { $problems += "CosmicApp.exe missing: $exe" }
if (Test-Path -LiteralPath (Join-Path $Bin 'boot.cfg')) { $problems += 'boot.cfg next to the exe: CosmicApp would boot a project, not the Launcher' }

$logDir = Join-Path $Bin 'logs'
$before = @{}
if (Test-Path -LiteralPath $logDir) {
    foreach ($f in Get-ChildItem -LiteralPath $logDir -Filter 'Cosmic_*.log') { $before[$f.Name] = $true }
}
$stdout = Join-Path $Output 'ux03-launcher-stdout.log'
$stderr = Join-Path $Output 'ux03-launcher-stderr.log'
$pattern = 'LauncherLayer: scan found (\d+) project\(s\): \[([^\]]*)\]'

$line = $null; $found = @(); $skipped = @(); $logUsed = $null
$exitCode = $null; $closedBy = 'n/a'; $elapsed = 0
if ($problems.Count -eq 0) {
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $proc = Start-Process -FilePath $exe -WorkingDirectory $Bin -PassThru -NoNewWindow `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    $null = $proc.Handle
    Write-Host ("launched CosmicApp.exe pid={0} cwd={1}" -f $proc.Id, $Bin)
    try {
        while ($sw.Elapsed.TotalSeconds -lt $TimeoutSec -and -not $line) {
            Start-Sleep -Milliseconds 250
            $candidates = @()
            if (Test-Path -LiteralPath $logDir) {
                $candidates += @(Get-ChildItem -LiteralPath $logDir -Filter 'Cosmic_*.log' | Where-Object { -not $before.ContainsKey($_.Name) } | ForEach-Object { $_.FullName })
            }
            $candidates += $stdout
            foreach ($c in $candidates) {
                if (-not (Test-Path -LiteralPath $c)) { continue }
                $text = $null
                try {
                    $fs = [IO.File]::Open($c, 'Open', 'Read', 'ReadWrite')
                    $sr = New-Object IO.StreamReader($fs)
                    $text = $sr.ReadToEnd(); $sr.Close()
                } catch { continue }
                $m = [regex]::Match($text, $pattern)
                if ($m.Success) {
                    $line = $m.Value; $logUsed = $c
                    $found = @($m.Groups[2].Value.Split(',') | ForEach-Object { $_.Trim() } | Where-Object { $_ })
                    $skipped = @([regex]::Matches($text, "skipped test fixture '([^']+)'") | ForEach-Object { $_.Groups[1].Value } | Select-Object -Unique)
                    break
                }
            }
            if ($proc.HasExited) { break }
        }
        if (-not $line) {
            if ($proc.HasExited) { $problems += ("CosmicApp exited (code {0}) before the found-list line" -f $proc.ExitCode) }
            else { $problems += "no found-list line within $TimeoutSec s" }
        }
    } finally {
        if (-not $proc.HasExited) {
            $closed = $false
            try { $closed = $proc.CloseMainWindow() } catch { }
            if ($closed -and $proc.WaitForExit(20000)) { $closedBy = 'CloseMainWindow' }
            else { try { $proc.Kill() } catch { }; $null = $proc.WaitForExit(15000); $closedBy = 'Kill' }
        } else { $closedBy = 'exited on its own' }
        $exitCode = $proc.ExitCode
        $elapsed = [int]$sw.Elapsed.TotalSeconds
    }
}

if ($line) {
    if ($found -notcontains 'Starforge') { $problems += 'Starforge not listed' }
    if ($found -notcontains 'SF_Telem')  { $problems += 'SF_Telem not listed' }
    foreach ($n in $found) {
        if ($n -like '*Fixture') { $problems += "test fixture listed: $n" }
        if ($n -eq 'WO07NoExport') { $problems += 'WO07NoExport listed' }
    }
}
$ok = ($problems.Count -eq 0)

$res = [ordered]@{
    id = 'LH01-W'; verdict = $(if ($ok) { 'PASS' } else { 'FAIL' });
    bin = $Bin; exe = $exe; cwd = $Bin; args = @();
    found_line = $line; found = @($found); skipped_fixtures = @($skipped);
    log = $logUsed; process_exit = $exitCode; closed_by = $closedBy; elapsed_s = $elapsed;
    problems = @($problems)
}
$res | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $Output 'ux03-launcher-result.json') -Encoding utf8

Write-Host ("LH01-W found-list line: {0}" -f $(if ($line) { $line } else { '(none)' }))
Write-Host ("  listed ({0}): {1}" -f $found.Count, ($found -join ', '))
Write-Host ("  skipped fixtures ({0}): {1}" -f $skipped.Count, ($skipped -join ', '))
Write-Host ("  log={0} closed_by={1} exit={2} elapsed={3}s" -f $logUsed, $closedBy, $exitCode, $elapsed)
foreach ($p in $problems) { Write-Host "  FAIL: $p" }
if ($ok) {
    Write-Host '[doctest] test cases: 1 | 1 passed | 0 failed'
    exit 0
}
Write-Host '[doctest] test cases: 1 | 0 passed | 1 failed'
exit 1
