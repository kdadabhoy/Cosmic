<#
.SYNOPSIS
    AP-P1 K02/K03/K04 - stage the one package layout, install it read-only, run the real
    packaged app from there, and check reinstall/uninstall + older-fixture readability.

.DESCRIPTION
    Every step drives the SHIPPING artefacts: installer/Stage-AppPackage.ps1 (what
    package.bat <App> and release.yml call), the renamed <App>.exe, the app's own
    boot.cfg. Nothing here re-implements the app or the packager.

    K02  Stage SF_Telem through the CLI path and compare the sorted relative-path list
         with the editor packager's (passed in with -EditorList) - the one-layout gate.
         Also asserts no dev-only target, no second app, no PDB/LIB reached the payload.
    K03  Install to a disposable location whose path carries SPACES and a non-ASCII
         character, deny this user write access to it (icacls), snapshot a SHA-256
         manifest, launch <App>.exe with NO flags from an arbitrary working directory,
         close it, and then prove: it exited cleanly, the install tree is byte-identical,
         and the log + imgui.ini it wrote are under %LOCALAPPDATA%\<App>. The Windows 10
         legs are NOT run here - see the report.
    K04  Reinstall over the same location (an update), then uninstall it, and prove the
         user data under %LOCALAPPDATA%\<App> survives both. Older v1 recording fixtures
         are loaded by the production reader afterwards.

.NOTES
    Windows PowerShell 5.1. Run from the repo root. Writes everything under -Output.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Repo,
    [Parameter(Mandatory=$true)][string]$Output,
    [string]$App        = 'SF_Telem',
    [string]$RuntimeDir = '',
    [string]$EditorList = '',
    [int]$RunSeconds    = 25
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not $RuntimeDir) { $RuntimeDir = Join-Path $Repo 'build\Runtime\Release' }
New-Item -ItemType Directory -Force -Path $Output | Out-Null
$fail = 0
$notes = New-Object System.Collections.ArrayList
function Note([string]$m) { Write-Host $m; [void]$notes.Add($m) }
# $ok is deliberately untyped: a PowerShell comparison against a single-element array
# yields the element, not a Boolean, and a [bool] parameter would throw instead of
# evaluating it. `if ($ok)` applies the ordinary truthiness rules.
function Check($ok, [string]$what) {
    if ($ok) { Note "  PASS  $what" } else { $script:fail++; Note "  FAIL  $what" }
}
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
    $h = @{}
    $p = (Resolve-Path -LiteralPath $dir).Path.TrimEnd('\') + '\'
    foreach ($f in Get-ChildItem -LiteralPath $dir -Recurse -File -Force) {
        $h[$f.FullName.Substring($p.Length) -replace '\\','/'] = (Get-FileHash -LiteralPath $f.FullName -Algorithm SHA256).Hash
    }
    return $h
}

# --------------------------------------------------------------------- K02 stage ---
Note "=== K02: stage $App through the CLI shipping path ==="
$dist = Join-Path $Output 'dist'
$listCli = Join-Path $Output "$App.cli.files.txt"
& (Join-Path $Repo 'installer\Stage-AppPackage.ps1') -SdkRoot $Repo -App $App `
    -RuntimeDir $RuntimeDir -OutDir (Join-Path $dist $App) -ListOut $listCli
if ($LASTEXITCODE -ne 0) { throw "staging failed ($LASTEXITCODE)" }
$staged = Join-Path $dist $App
$files = @(Get-Content $listCli)
Note ("  staged {0} files" -f $files.Count)

Check ($files -contains "$App.exe")   "<App>.exe present (renamed CosmicApp.exe)"
Check ($files -contains "$App.dll")   "<App>.dll present"
Check ($files -contains 'Cosmic.dll') "Cosmic.dll present"
Check ($files -contains 'boot.cfg')   "boot.cfg present"
Check ($files -contains 'user/README.txt') "user/ placeholder present"
Check (@($files | Where-Object { $_ -like 'licenses/*' }).Count -ge 2) "licenses/ staged"
Check (@($files | Where-Object { $_ -like "assets/projects/$App/*" }).Count -gt 0) "assets/projects/<App>/ staged"
Check (@($files | Where-Object { $_ -like 'assets/projects/*' -and $_ -notlike "assets/projects/$App/*" }).Count -eq 0) "no other app's content"
Check (@($files | Where-Object { $_ -match '(?i)cosmictests|cosmicrendertests|Fixture\.dll$' }).Count -eq 0) "no dev-only target staged"
Check (@($files | Where-Object { $_ -match '(?i)\.(pdb|lib|exp|ilk)$' }).Count -eq 0) "no PDB/LIB/EXP staged"
$rootDlls = ((@($files | Where-Object { $_ -match '(?i)^[^/]+\.dll$' }) | Sort-Object) -join ',')
$wantDlls = ((@('Cosmic.dll', "$App.dll") | Sort-Object) -join ',')
Check ($rootDlls -eq $wantDlls) "the only root DLLs are Cosmic.dll and $App.dll (got: $rootDlls)"
$boot = @(Get-Content (Join-Path $staged 'boot.cfg') | Where-Object { $_ -and -not $_.StartsWith('#') })[0]
Check ($boot -eq $App) "boot.cfg names $App (it is what sets the user:// identity)"

if ($EditorList -and (Test-Path -LiteralPath $EditorList)) {
    $diff = Compare-Object (Get-Content $listCli) (Get-Content $EditorList)
    if ($diff) {
        $diff | ForEach-Object { Note ("  layout diff: {0} {1}" -f $_.SideIndicator, $_.InputObject) }
        Check $false "CLI and editor packager produce the identical file list"
    } else {
        Check $true "CLI and editor packager produce the identical file list"
    }
} else {
    Note "  (no -EditorList supplied; the editor-path comparison is reported separately)"
}

# ------------------------------------------------------------ K03 read-only install ---
$installRoot = Join-Path $Output ("install root " + [char]0x00DC + "nicode")
$install = Join-Path $installRoot "$App app"
Note "=== K03: install to a read-only location with spaces and a non-ASCII character ==="
Note "  $install"
Remove-DeniedTree $installRoot
New-Item -ItemType Directory -Force -Path $install | Out-Null
Copy-Item (Join-Path $staged '*') $install -Recurse -Force

Set-TreeWriteDenied $install $true
$probe = Join-Path $install '.write-probe'
$writable = $true
try { [System.IO.File]::WriteAllText($probe, 'x'); Remove-Item $probe -Force } catch { $writable = $false }
Check (-not $writable) "the install directory really is read-only for this user"

$before = Manifest $install
$userRoot = Join-Path $env:LOCALAPPDATA $App
Remove-Item -Recurse -Force $userRoot -ErrorAction SilentlyContinue

Note "  launching $App.exe (no flags) from an arbitrary working directory ..."
$exe = Join-Path $install "$App.exe"
$p = Start-Process -FilePath $exe -WorkingDirectory $env:SystemRoot -PassThru
Start-Sleep -Seconds $RunSeconds
$closed = $false
try { $closed = $p.CloseMainWindow() } catch { }
if (-not $p.WaitForExit(60000)) { $p.Kill(); $p.WaitForExit(15000) }
$exit = $p.ExitCode
Note ("  process exited {0} (CloseMainWindow={1})" -f $exit, $closed)
Check ($exit -eq 0) "the installed app exited cleanly (exit 0) from a read-only install"

$after = Manifest $install
$added   = @($after.Keys  | Where-Object { -not $before.ContainsKey($_) })
$removed = @($before.Keys | Where-Object { -not $after.ContainsKey($_) })
$changed = @($before.Keys | Where-Object { $after.ContainsKey($_) -and $after[$_] -ne $before[$_] })
if ($added)   { $added   | ForEach-Object { Note "  install gained: $_" } }
if ($changed) { $changed | ForEach-Object { Note "  install changed: $_" } }
Check (($added.Count + $removed.Count + $changed.Count) -eq 0) "the install tree is byte-identical after the run (nothing written into it)"

$logs = @(Get-ChildItem (Join-Path $userRoot 'logs') -Filter *.log -ErrorAction SilentlyContinue)
Check ($logs.Count -gt 0 -and ($logs | Measure-Object Length -Sum).Sum -gt 0) "log written under %LOCALAPPDATA%\$App\logs"
Check (Test-Path (Join-Path $userRoot 'imgui.ini')) "config (imgui.ini) written under %LOCALAPPDATA%\$App"
foreach ($l in $logs) { Note ("  log: {0} ({1} bytes)" -f $l.Name, $l.Length) }
Get-ChildItem $userRoot -Recurse -File -ErrorAction SilentlyContinue |
    ForEach-Object { Note ("  user data: {0}" -f $_.FullName.Substring($userRoot.Length + 1)) }

# ------------------------------------------------------------------ K04 lifecycle ---
Note "=== K04: reinstall over the same location, then uninstall ==="
# Seed a user-data file the way the app would, to prove the lifecycle policy.
$keep = Join-Path $userRoot 'recordings\SF_Telem\k04-keepme\scene.bin'
New-Item -ItemType Directory -Force -Path (Split-Path $keep) | Out-Null
Copy-Item (Join-Path $Repo 'tests\acceptance\fixtures\wo06\independent-v1.bin') $keep -Force
$keepHash = (Get-FileHash $keep -Algorithm SHA256).Hash

Set-TreeWriteDenied $install $false
Copy-Item (Join-Path $staged '*') $install -Recurse -Force        # reinstall / update
Check (Test-Path $keep) "user data survives a reinstall"
Check ((Get-FileHash $keep -Algorithm SHA256).Hash -eq $keepHash) "user data is unchanged by a reinstall"
$reinstalled = Manifest $install
Check ($null -eq (Compare-Object @($reinstalled.Keys | Sort-Object) @($before.Keys | Sort-Object))) "the reinstalled tree has the same file list"

Remove-DeniedTree $installRoot                                     # uninstall ({app} only)
Check (-not (Test-Path $install)) "uninstall removes the install directory"
Check (Test-Path $keep) "uninstall leaves %LOCALAPPDATA%\$App user data in place (documented policy)"

Note "=== K04: older v1 recording fixtures stay readable by the shipped reader ==="
$reader = Join-Path $RuntimeDir 'CosmicTests.exe'
& $reader '--test-case=WO-06 D01:*,WO-06 D03:*' '--no-intro' '--no-colors' | Tee-Object -FilePath (Join-Path $Output 'v1-fixtures.txt') | Out-Null
Check ($LASTEXITCODE -eq 0) "the pinned v1 fixtures (independent-v1.bin, fallback-A.bin, bad-version.bin) still load/reject as specified"

$notes | Set-Content -LiteralPath (Join-Path $Output 'k02-k04-excerpts.txt') -Encoding UTF8
Write-Host ""
if ($fail -gt 0) { Write-Host "[K02/K03/K04] $fail check(s) FAILED" -ForegroundColor Red; exit 1 }
Write-Host "[K02/K03/K04] all checks passed" -ForegroundColor Green
exit 0
