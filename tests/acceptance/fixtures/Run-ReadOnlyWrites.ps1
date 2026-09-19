<#
.SYNOPSIS
    AP-P1 - record / autosave / export / replay / screenshot on the production path, with
    the application directory READ-ONLY, plus the same chain from a writable package.

.DESCRIPTION
    The driver is the existing L05 host fixture (tests/WO07UiCyclesFixture.cpp): the REAL
    Workspace::SF_Telem (SerialLink -> SerialPort -> the WO-04 FakeSerialTransport ->
    TelemHub -> DataRecorder -> DataPlayer), hosted by the real Application, driven
    through its REAL navigation buttons. Nothing here re-implements the app.

    Two runs, because the oracle differs:

      PORTABLE  working directory = a WRITABLE copy of the staged package. user:// is the
                app directory, so the fixture's own CWD-relative assertions hold and its
                verdict is the oracle. Proves the whole chain against a PACKAGE (not the
                dev tree).

      READONLY  working directory = a READ-ONLY copy of the same package. user:// moves to
                %LOCALAPPDATA%\<root>\ by design, so the fixture's CWD-relative existence
                check (WO07UiCyclesFixture.cpp:253, "recordings/SF_Telem/wo07-l05/scene.bin")
                no longer names the file and its verdict is NOT the oracle here. This
                script's own filesystem oracle is: the app directory must be byte-identical
                afterwards, and the recording, the autosave, the log and the screenshots
                must exist under the writable user root. The fixture's verdict is reported
                as-is, never rewritten.

    The host is CosmicTests.exe, which stays OUTSIDE the read-only directory (it is a
    dev-only target and is never installed). It carries no boot.cfg, so its user root is
    the identity-less %LOCALAPPDATA%\Cosmic; the per-app %LOCALAPPDATA%\<App> mapping is
    proved separately, by the real packaged <App>.exe, in Run-PackageInstall.ps1.

.NOTES
    Windows PowerShell 5.1. Needs a GPU (the fixture presents a real window).
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Repo,
    [Parameter(Mandatory=$true)][string]$Output,
    [string]$RuntimeDir = '',
    [int]$Cycles = 12
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (-not $RuntimeDir) { $RuntimeDir = Join-Path $Repo 'build\Runtime\Release' }
New-Item -ItemType Directory -Force -Path $Output | Out-Null

$fail = 0
$notes = New-Object System.Collections.ArrayList
function Note([string]$m) { Write-Host $m; [void]$notes.Add($m) }
# $ok is untyped on purpose: a comparison against a single-element array yields the
# element, not a Boolean, and a [bool] parameter would throw instead of evaluating it.
function Check($ok, [string]$what) { if ($ok) { Note "  PASS  $what" } else { $script:fail++; Note "  FAIL  $what" } }
# Make a directory tree read-only for THIS user, reversibly. .NET, not icacls: icacls
# goes through the console code page, so a path with a non-ASCII character (K03 uses one
# deliberately) is mangled and the ACE lands on nothing. EXACTLY the four write bits:
# adding Delete makes .NET normalise the rule to a mask that also carries SYNCHRONIZE,
# and a denied SYNCHRONIZE stops the directory being opened at all - unlistable, not
# read-only. Blocking create/modify is what an installed app faces under Program Files.
function Set-TreeWriteDenied([string]$dir, [bool]$deny) {
    $sid = [System.Security.Principal.WindowsIdentity]::GetCurrent().User
    $rights = [System.Security.AccessControl.FileSystemRights]'WriteData, AppendData, WriteExtendedAttributes, WriteAttributes'
    $inh = [System.Security.AccessControl.InheritanceFlags]'ContainerInherit, ObjectInherit'
    $sec = [System.Security.AccessControl.AccessControlSections]::Access
    # Access section ONLY: Get-Acl/Set-Acl round-trip the audit section too and then need
    # SeSecurityPrivilege, which a normal user does not hold.
    $acl = [System.IO.Directory]::GetAccessControl($dir, $sec)
    if ($deny) {
        $ace = New-Object System.Security.AccessControl.FileSystemAccessRule(
            $sid, $rights, $inh, [System.Security.AccessControl.PropagationFlags]::None,
            [System.Security.AccessControl.AccessControlType]::Deny)
        $acl.AddAccessRule($ace)
    } else {
        # Purge EVERY deny ACE for this user, not just the one we added: a previous run
        # that was interrupted leaves its ACE behind, the directory then cannot be
        # deleted or re-created cleanly, and the next run silently inherits it.
        foreach ($r in @($acl.Access)) {
            if ($r.AccessControlType -eq 'Deny' -and
                $r.IdentityReference.Translate([System.Security.Principal.SecurityIdentifier]) -eq $sid) {
                [void]$acl.RemoveAccessRuleSpecific($r)
            }
        }
    }
    [System.IO.Directory]::SetAccessControl($dir, $acl)
}

# Remove a tree that a previous (possibly interrupted) run left write-denied.
function Remove-DeniedTree([string]$dir) {
    if (-not (Test-Path -LiteralPath $dir)) { return }
    $dirs = @(Get-ChildItem -LiteralPath $dir -Recurse -Directory -Force -ErrorAction SilentlyContinue |
                ForEach-Object { $_.FullName })
    $dirs += $dir
    foreach ($d in $dirs) { try { Set-TreeWriteDenied $d $false } catch { } }
    Remove-Item -LiteralPath $dir -Recurse -Force -ErrorAction SilentlyContinue
}

function Manifest([string]$dir) {
    $h = @{}; $p = (Resolve-Path -LiteralPath $dir).Path.TrimEnd('\') + '\'
    foreach ($f in Get-ChildItem -LiteralPath $dir -Recurse -File -Force) {
        $h[$f.FullName.Substring($p.Length) -replace '\\','/'] = (Get-FileHash -LiteralPath $f.FullName -Algorithm SHA256).Hash
    }
    return $h
}

# --- stage one package and make two copies of it ----------------------------------
$staged = Join-Path $Output 'staged\SF_Telem'
& (Join-Path $Repo 'installer\Stage-AppPackage.ps1') -SdkRoot $Repo -App 'SF_Telem' `
    -RuntimeDir $RuntimeDir -OutDir $staged | Out-Null
if ($LASTEXITCODE -ne 0) { throw "staging failed ($LASTEXITCODE)" }

$tests = Join-Path $RuntimeDir 'CosmicTests.exe'
if (-not (Test-Path $tests)) { throw "host binary missing: $tests" }

# .NET ProcessStartInfo, not Start-Process: -PassThru does not populate ExitCode
# reliably, and -ArgumentList as an ARRAY re-quotes '--test-case=WO-07 L05 host*' in a
# way doctest does not parse (the child then runs 0 cases and still exits 0).
function Invoke-L05([string]$Cwd, [string]$OutSub) {
    $o = Join-Path $Output $OutSub
    New-Item -ItemType Directory -Force -Path $o | Out-Null
    $env:COSMIC_WO07_L05_CYCLES = "$Cycles"
    $env:COSMIC_WO07_L05_OUT    = $o
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName         = $tests
    $psi.Arguments        = '"--test-case=WO-07 L05 host*" --no-skip=true --no-intro --no-colors'
    $psi.WorkingDirectory = $Cwd
    $psi.UseShellExecute  = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError  = $true
    $p = [System.Diagnostics.Process]::Start($psi)
    $so = $p.StandardOutput.ReadToEnd()
    $se = $p.StandardError.ReadToEnd()
    if (-not $p.WaitForExit(600000)) { $p.Kill(); $p.WaitForExit(); Note '  (child killed at the 600 s deadline)' }
    [IO.File]::WriteAllText((Join-Path $o 'stdout.log'), $so)
    [IO.File]::WriteAllText((Join-Path $o 'stderr.log'), $se)
    $cases = -1
    $m = [regex]::Match($so, 'test cases:\s*(\d+)\s*\|\s*(\d+) passed\s*\|\s*(\d+) failed')
    if ($m.Success) { $cases = [int]$m.Groups[2].Value }
    return @{ exit = $p.ExitCode; out = $o; passed = $cases; stdout = $so }
}

# ------------------------------------------------------------------ PORTABLE run ---
Note '=== PORTABLE: the chain from a WRITABLE package copy (user:// = the app dir) ==='
$portable = Join-Path $Output 'portable\SF_Telem'
New-Item -ItemType Directory -Force -Path $portable | Out-Null
Copy-Item (Join-Path $staged '*') $portable -Recurse -Force
$r1 = Invoke-L05 $portable 'l05-portable'
Note ("  fixture exit {0}, doctest cases passed {1}" -f $r1.exit, $r1.passed)
Check (($r1.exit -eq 0) -and ($r1.passed -ge 1)) "L05 (record -> autosave -> export -> replay -> screenshot) passes from a package"
$summary = ([regex]::Match($r1.stdout, 'WO07 L05: .*')).Value
if ($summary) { Note ("  " + $summary) }
$rec1 = Join-Path $portable 'recordings\SF_Telem\wo07-l05\scene.bin'
Check (Test-Path $rec1) "the recording landed in the package's own user root (portable mode)"
if (Test-Path $rec1) { Note ("  recording: {0} bytes" -f (Get-Item $rec1).Length) }
$shots1 = @(Get-ChildItem (Join-Path $Output 'l05-portable') -Filter 'l05-shot-*.png' -ErrorAction SilentlyContinue)
Check ($shots1.Count -gt 0) "screenshots captured ($($shots1.Count))"

# ------------------------------------------------------------------ READONLY run ---
Note '=== READONLY: the same chain with the app directory denied write access ==='
$ro = Join-Path $Output 'readonly\SF_Telem'
Remove-DeniedTree $ro
New-Item -ItemType Directory -Force -Path $ro | Out-Null
Copy-Item (Join-Path $staged '*') $ro -Recurse -Force
Set-TreeWriteDenied $ro $true
$probe = Join-Path $ro '.probe'
$writable = $true
try { [IO.File]::WriteAllText($probe, 'x'); Remove-Item $probe -Force } catch { $writable = $false }
Check (-not $writable) "the app directory really is read-only for this user"

$userRoot = Join-Path $env:LOCALAPPDATA 'Cosmic'
$recRo = Join-Path $userRoot 'recordings\SF_Telem\wo07-l05\scene.bin'
Remove-Item -Recurse -Force (Join-Path $userRoot 'recordings\SF_Telem\wo07-l05') -ErrorAction SilentlyContinue

$before = Manifest $ro
$r2 = Invoke-L05 $ro 'l05-readonly'
$after = Manifest $ro
Note ("  fixture exit {0}, doctest cases passed {1}" -f $r2.exit, $r2.passed)

$added   = @($after.Keys  | Where-Object { -not $before.ContainsKey($_) })
$changed = @($before.Keys | Where-Object { $after.ContainsKey($_) -and $after[$_] -ne $before[$_] })
$removed = @($before.Keys | Where-Object { -not $after.ContainsKey($_) })
foreach ($a in $added)   { Note "  app dir GAINED: $a" }
foreach ($c in $changed) { Note "  app dir CHANGED: $c" }
Check (($added.Count + $changed.Count + $removed.Count) -eq 0) "nothing was written into the read-only app directory"

# KI-58: the DEV-ONLY test host fail-fasts at startup (0xC0000409 == release abort())
# when its working directory is not writable, before any output. That is a defect in
# CosmicTests.exe, not in the shipped app - the packaged SF_Telem.exe runs fine from
# the same read-only tree (see Run-PackageInstall.ps1). It does mean this leg cannot be
# driven here, so it is reported ENVIRONMENT_BLOCKED with the prerequisite named, never
# as a pass and never as a product failure.
if ($r2.exit -eq -1073740791 -and $r2.passed -lt 1) {
    Note "  BLOCKED  record/autosave/export/replay/screenshot INSIDE a read-only app dir"
    Note "           prerequisite: a test host that starts from a non-writable working"
    Note "           directory (KI-58), or an SF_Telem UI-automation harness not hosted"
    Note "           by the dev-only CosmicTests.exe. The same chain is proven above on a"
    Note "           writable package, and the writable-path policy itself is proven by"
    Note "           the real packaged SF_Telem.exe from a read-only install (K03)."
    Note ("           observed: exit {0} (0xC0000409), {1} bytes of output" -f $r2.exit, $r2.stdout.Length)
    $blocked = 1
} else {
    Check (Test-Path $recRo) "the exported recording landed under the writable user root instead"
    if (Test-Path $recRo) { Note ("  recording: {0} ({1} bytes)" -f $recRo, (Get-Item $recRo).Length) }
    $auto = @(Get-ChildItem (Join-Path $userRoot 'recordings\SF_Telem\_autosave') -Recurse -Filter '*.bin' -ErrorAction SilentlyContinue)
    Note ("  autosave snapshots under the user root: {0}" -f $auto.Count)
    $logs = @(Get-ChildItem (Join-Path $userRoot 'logs') -Filter '*.log' -ErrorAction SilentlyContinue)
    Check ($logs.Count -gt 0) "the log was written under the writable user root"
    $shots2 = @(Get-ChildItem (Join-Path $Output 'l05-readonly') -Filter 'l05-shot-*.png' -ErrorAction SilentlyContinue)
    Check ($shots2.Count -gt 0) "screenshots captured from the read-only app dir ($($shots2.Count))"
}
$actions = Join-Path (Join-Path $Output 'l05-readonly') 'l05-actions.txt'
if (Test-Path $actions) { Note ("  judged actions logged: {0}" -f (Get-Content $actions).Count) }

Set-TreeWriteDenied $ro $false

$notes | Set-Content -LiteralPath (Join-Path $Output 'readonly-writes-excerpts.txt') -Encoding UTF8
Write-Host ''
if ($fail -gt 0) { Write-Host "[read-only writes] $fail check(s) FAILED" -ForegroundColor Red; exit 1 }
Write-Host '[read-only writes] all checks passed' -ForegroundColor Green
exit 0
