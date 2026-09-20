<#
.SYNOPSIS
    Stage ONE Cosmic app into the single shipping layout (AP-P1).

.DESCRIPTION
    The CLI counterpart of the editor packager (Projects/Starforge/src/Packager.cpp
    :: Packager::Stage), which is the reference implementation. package.bat and
    .github/workflows/release.yml both call THIS script, so every non-editor
    packaging path stages a byte-identical tree and AP-P1's file-list comparison
    has one CLI side, not two.

    The single layout (no other layout ships):

        <App>.exe                 renamed copy of CosmicApp.exe
        <App>.dll                 the app's plugin DLL
        Cosmic.dll                the engine
        boot.cfg                  names <App>; also sets the per-app user:// identity
        assets/**                 engine assets, MINUS assets/projects/**
        assets/projects/<App>/**  ONLY this app's content (no src/build/.git/...)
        licenses/**               installer/licenses/MANIFEST.txt, staged verbatim
        user/README.txt           portable-mode user-data placeholder

    NOT staged, ever: CosmicTests.exe / CosmicRenderTests.exe or any other dev-only
    target, test fixtures, PDBs, .lib/.exp import libraries, another app's DLL or
    content. The staged tree is the payload an installer or a zip ships as-is.

.PARAMETER SdkRoot
    The Cosmic SDK root (the repository root).

.PARAMETER App
    The app/project name: <App>.dll must exist under -RuntimeDir.

.PARAMETER RuntimeDir
    The build output dir holding CosmicApp.exe, Cosmic.dll, <App>.dll and assets/
    (normally <SdkRoot>\build\Runtime\Release).

.PARAMETER OutDir
    Destination directory. Removed and recreated.

.PARAMETER ProjectContentDir
    The app content to ship under assets/projects/<App>/. Defaults to
    <RuntimeDir>\assets\projects\<App> - exactly what the editor packager uses for
    an in-tree project. For an EXTERNAL project, pass its root, like the editor does.

.PARAMETER AppDllPath
    The app's plugin DLL when it is not in -RuntimeDir. An external consumer builds
    into its own tree (<project>\build\<CONFIG>\<App>.dll); the editor packager takes
    that path as PackageInputs::ProjectDllPath, and this is the CLI equivalent.

.PARAMETER ListOut
    Optional path for the sorted relative-path file list of the staged tree (the
    artifact AP-P1's three-path comparison diffs).

.NOTES
    Windows PowerShell 5.1 compatible, ASCII-only source.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$SdkRoot,
    [Parameter(Mandatory=$true)][string]$App,
    [Parameter(Mandatory=$true)][string]$RuntimeDir,
    [Parameter(Mandatory=$true)][string]$OutDir,
    [string]$ProjectContentDir = '',
    [string]$AppDllPath = '',
    [string]$ListOut = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Fail([string]$msg) { Write-Host "[stage] ERROR: $msg" -ForegroundColor Red; exit 1 }

$SdkRoot    = (Resolve-Path -LiteralPath $SdkRoot).Path
if (-not (Test-Path -LiteralPath $RuntimeDir)) { Fail "runtime dir not found: $RuntimeDir" }
$RuntimeDir = (Resolve-Path -LiteralPath $RuntimeDir).Path

$exeSrc    = Join-Path $RuntimeDir 'CosmicApp.exe'
$engineDll = Join-Path $RuntimeDir 'Cosmic.dll'
$appDll    = if ($AppDllPath) { $AppDllPath } else { Join-Path $RuntimeDir ("$App.dll") }
foreach ($required in @($exeSrc, $engineDll, $appDll)) {
    if (-not (Test-Path -LiteralPath $required)) {
        Fail "'$required' not found - build $App in this configuration first."
    }
}

if (-not $ProjectContentDir) { $ProjectContentDir = Join-Path $RuntimeDir "assets\projects\$App" }

# --- fresh output ---------------------------------------------------------------
# Resolved to an absolute path right after creation: release.yml passes RELATIVE paths
# ("build/Runtime/Release", "dist/$app"), and the file list below slices absolute
# FullNames, which only works against an absolute prefix.
if (Test-Path -LiteralPath $OutDir) { Remove-Item -LiteralPath $OutDir -Recurse -Force }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$OutDir = (Resolve-Path -LiteralPath $OutDir).Path

# --- renamed exe + engine + app DLL ----------------------------------------------
Copy-Item -LiteralPath $exeSrc    -Destination (Join-Path $OutDir "$App.exe") -Force
Copy-Item -LiteralPath $engineDll -Destination (Join-Path $OutDir 'Cosmic.dll') -Force
Copy-Item -LiteralPath $appDll    -Destination (Join-Path $OutDir "$App.dll") -Force

# --- assets: engine assets minus projects/, then ONLY this app's content ---------
$assetsSrc = Join-Path $RuntimeDir 'assets'
$assetsDst = Join-Path $OutDir 'assets'
New-Item -ItemType Directory -Force -Path (Join-Path $assetsDst 'projects') | Out-Null
if (Test-Path -LiteralPath $assetsSrc) {
    Get-ChildItem -LiteralPath $assetsSrc -Force | Where-Object { $_.Name -ne 'projects' } |
        ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $assetsDst -Recurse -Force }
}

# Content skip list - identical to Packager.cpp's SkipContentEntry().
$skip = @('build', '.git', '.starforge', 'src', 'CMakeLists.txt', '.vs', '.gitignore')
if (Test-Path -LiteralPath $ProjectContentDir) {
    $contentDst = Join-Path $assetsDst "projects\$App"
    New-Item -ItemType Directory -Force -Path $contentDst | Out-Null
    Get-ChildItem -LiteralPath $ProjectContentDir -Force | Where-Object { $skip -notcontains $_.Name } |
        ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $contentDst -Recurse -Force }
} else {
    Write-Host "[stage] WARNING: no content dir '$ProjectContentDir' - staging code only." -ForegroundColor Yellow
}

# --- boot.cfg (byte-identical to Packager.cpp's; ofstream text mode => CRLF) -----
$boot = "# Cosmic packaged app - launched with no --project flag; also sets`r`n" +
        "# the per-app user:// data identity (S6).`r`n$App`r`n"
[System.IO.File]::WriteAllText((Join-Path $OutDir 'boot.cfg'), $boot, (New-Object System.Text.UTF8Encoding($false)))

# --- licenses (installer/licenses/MANIFEST.txt is the one source of truth) -------
$manifest = Join-Path $SdkRoot 'installer\licenses\MANIFEST.txt'
if (-not (Test-Path -LiteralPath $manifest)) { Fail "license manifest missing: $manifest" }
$licDst = Join-Path $OutDir 'licenses'
New-Item -ItemType Directory -Force -Path $licDst | Out-Null
foreach ($line in (Get-Content -LiteralPath $manifest)) {
    $t = $line.Trim()
    if (-not $t -or $t.StartsWith('#')) { continue }
    $parts = $t.Split('|')
    if ($parts.Count -ne 2) { Fail "bad license manifest line: $t" }
    $src = Join-Path $SdkRoot $parts[0].Trim()
    $dst = Join-Path $licDst  $parts[1].Trim()
    if (-not (Test-Path -LiteralPath $src)) { Fail "license source missing: $src" }
    Copy-Item -LiteralPath $src -Destination $dst -Force
}

# --- user/ placeholder ------------------------------------------------------------
$userDir = Join-Path $OutDir 'user'
New-Item -ItemType Directory -Force -Path $userDir | Out-Null
$userNote = @"
This folder is the app's PORTABLE user-data root.

Run from a writable folder (an unzipped copy), everything the app writes - logs,
recordings, exports, screenshots, imgui.ini, settings - lands here, under user://.
Installed to a read-only location (Program Files), the same user:// paths resolve
to %LOCALAPPDATA%\<AppName> instead and this folder stays empty. The app never
writes anywhere else inside its install directory.

Deleting this folder discards that data; the app recreates it on the next run.
"@
[System.IO.File]::WriteAllText((Join-Path $userDir 'README.txt'), ($userNote -replace "`r`n", "`n"), (New-Object System.Text.UTF8Encoding($false)))

# --- honest gate: no dev-only target may ship -------------------------------------
$banned = Get-ChildItem -LiteralPath $OutDir -Recurse -File -Force |
    Where-Object { $_.Name -match '^(CosmicTests|CosmicRenderTests)\.exe$' -or $_.Extension -in @('.pdb', '.lib', '.exp', '.ilk') }
if ($banned) {
    $banned | ForEach-Object { Write-Host "[stage]   dev-only artifact: $($_.FullName)" -ForegroundColor Red }
    Fail 'dev-only artifacts reached the staged package.'
}

# --- file list --------------------------------------------------------------------
$prefix = $OutDir.TrimEnd('\') + '\'
$list = Get-ChildItem -LiteralPath $OutDir -Recurse -File -Force |
    ForEach-Object { $_.FullName.Substring($prefix.Length) -replace '\\', '/' } | Sort-Object
if ($ListOut) {
    $dir = Split-Path -Parent $ListOut
    if ($dir -and -not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
    [System.IO.File]::WriteAllText($ListOut, (($list -join "`n") + "`n"), (New-Object System.Text.UTF8Encoding($false)))
}

Write-Host ("[stage] {0}: {1} files -> {2}" -f $App, $list.Count, $OutDir)
exit 0
