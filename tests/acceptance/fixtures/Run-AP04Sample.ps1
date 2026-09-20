# Run-AP04Sample.ps1 — AP-04 (App Platform): the Y01 standalone build of PendulumLab.
#
# PendulumLab (Projects/PendulumLab) is an EXTERNAL Cosmic consumer: the root build skips
# it, and Y01 proves it configures and builds from a CLEAN SDK PATH — the K01 procedure:
#   1. COPY      — the committed source is copied to a folder OUTSIDE the SDK tree
#                  (-BuildRoot, default <repo>\..\_ap04-pendulum-build\PendulumLab-src; fresh per run)
#                  so nothing can resolve source-relative to the checkout;
#   2. CONFIGURE — cmake -S <copy> -B <BuildRoot>\build -A x64 -DCOSMIC_SDK_DIR=<repo> -DGAME_OUTPUT_DIR=<BuildRoot>\build
#                  (the exact commands the Starforge packager / BuildRunner drive; recorded in commands.txt);
#   3. BUILD     — cmake --build <BuildRoot>\build --config <Config> --parallel, 0 warnings outside imgui/implot;
#   4. ORACLE    — <BuildRoot>\build\<Config>\PendulumLab.dll exists and its export table names the
#                  three module exports (CosmicModule_Register, CreatePluginLayer, InitializePluginContexts);
#                  the copied tree still carries kind = "app", the flow and the four screens; result JSON.
# The in-exe physics/flow units (RK4 vs F-PENDULUM, damping, period, determinism, F02) run through
# Run-WO10Case.ps1 from the ap04-units manifest. Y02/Y03 (the packaged exe + self-test) are AP-Q1's.
param(
    [Parameter(Mandatory=$true)][string]$Bin,
    [Parameter(Mandatory=$true)][string]$Output,
    [string]$Config = 'Release',
    [string]$BuildRoot = '',
    [string]$CMake = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
    [int]$BuildTimeoutSec = 900)
$ErrorActionPreference = 'Stop'
$acceptance = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$repo = [IO.Path]::GetFullPath((Join-Path $acceptance '..\..')).TrimEnd('\')
if (-not $BuildRoot) { $BuildRoot = [IO.Path]::GetFullPath((Join-Path $repo '..\_ap04-pendulum-build')) }
if ([IO.Path]::GetFullPath($BuildRoot).StartsWith($repo + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw "Y01 requires a build root OUTSIDE the SDK tree (got $BuildRoot)"
}
if (-not (Test-Path -LiteralPath $CMake)) { $found = Get-Command cmake -ErrorAction SilentlyContinue; if ($found) { $CMake = $found.Source } else { throw "cmake not found at $CMake" } }
$source = Join-Path $repo 'Projects\PendulumLab'
if (-not (Test-Path -LiteralPath (Join-Path $source 'project.cproj'))) { throw "sample source missing: $source" }
$sdkLib = Join-Path $repo "build\Runtime\$Config\Cosmic.lib"
if (-not (Test-Path -LiteralPath $sdkLib)) { throw "SDK $Config import library missing: $sdkLib (build the SDK first)" }

New-Item -ItemType Directory -Force -Path $Output | Out-Null
$srcCopy = Join-Path $BuildRoot 'PendulumLab-src'
$bld     = Join-Path $BuildRoot 'build'
if (Test-Path -LiteralPath $BuildRoot) { Remove-Item -LiteralPath $BuildRoot -Recurse -Force }
New-Item -ItemType Directory -Force -Path $srcCopy | Out-Null
Copy-Item -Path (Join-Path $source '*') -Destination $srcCopy -Recurse -Force
Get-ChildItem -LiteralPath $srcCopy -Directory -Filter build -ErrorAction SilentlyContinue | Remove-Item -Recurse -Force

$commands = @(
    "`"$CMake`" -S `"$srcCopy`" -B `"$bld`" -A x64 -DCOSMIC_SDK_DIR=`"$repo`" -DGAME_OUTPUT_DIR=`"$bld`"",
    "`"$CMake`" --build `"$bld`" --config $Config --parallel"
)
Set-Content -Path (Join-Path $Output 'commands.txt') -Value ($commands -join "`r`n") -Encoding utf8

$sw = [Diagnostics.Stopwatch]::StartNew()
$env:COSMIC_SDK = $repo
& $CMake -S $srcCopy -B $bld -A x64 "-DCOSMIC_SDK_DIR=$repo" "-DGAME_OUTPUT_DIR=$bld" 2>&1 | Out-File -FilePath (Join-Path $Output 'configure.log') -Encoding utf8
$configureExit = $LASTEXITCODE
$buildExit = -1; $warnings = -1
if ($configureExit -eq 0) {
    & $CMake --build $bld --config $Config --parallel 2>&1 | Out-File -FilePath (Join-Path $Output 'build.log') -Encoding utf8
    $buildExit = $LASTEXITCODE
    $warnings = @(Select-String -Path (Join-Path $Output 'build.log') -Pattern 'warning [A-Z]+\d+' | Where-Object { $_.Line -notmatch '\\imgui\\|\\implot\\' }).Count
}
$sw.Stop()

$dll = Join-Path $bld "$Config\PendulumLab.dll"
$dllExists = Test-Path -LiteralPath $dll
$exports = @()
if ($dllExists) {
    $bytes = [IO.File]::ReadAllBytes($dll)
    $ascii = [Text.Encoding]::ASCII.GetString($bytes)
    foreach ($name in 'CosmicModule_Register', 'CreatePluginLayer', 'InitializePluginContexts') {
        if ($ascii.Contains($name)) { $exports += $name }
    }
}
$manifest = Get-Content -LiteralPath (Join-Path $srcCopy 'project.cproj') -Raw
$kindOk = $manifest -match 'kind\s*=\s*"app"'
$flowOk = Test-Path -LiteralPath (Join-Path $srcCopy 'flows\Main.cflow')
$screens = @('Home', 'Lab', 'Settings', 'Stopped') | Where-Object { Test-Path -LiteralPath (Join-Path $srcCopy "scenes\$_.cscene") }
$sdkLeak = $false
if (Test-Path -LiteralPath (Join-Path $bld 'CMakeCache.txt')) {
    $cache = Get-Content -LiteralPath (Join-Path $bld 'CMakeCache.txt') -Raw
    $sdkLeak = -not ($cache -match [regex]::Escape('COSMIC_SDK_DIR'))
}

$pass = ($configureExit -eq 0) -and ($buildExit -eq 0) -and $dllExists -and ($exports.Count -eq 3) -and $kindOk -and $flowOk -and ($screens.Count -eq 4) -and ($warnings -eq 0) -and (-not $sdkLeak)
$dllHash = if ($dllExists) { (Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash } else { '' }
$result = [ordered]@{
    work_order = 'AP-04'; case = 'Y01 PendulumLab standalone build'; config = $Config
    verdict = $(if ($pass) { 'PASS' } else { 'FAIL' })
    sdk_dir = $repo; source_copy = $srcCopy; build_dir = $bld
    configure_exit = $configureExit; build_exit = $buildExit; warnings_outside_deps = $warnings
    dll = $dll; dll_exists = $dllExists; dll_sha256 = $dllHash; exports_found = $exports
    manifest_kind_app = $kindOk; flow_present = $flowOk; screens_present = $screens
    seconds = [math]::Round($sw.Elapsed.TotalSeconds, 1)
    commands = $commands
}
$result | ConvertTo-Json -Depth 4 | Set-Content -Path (Join-Path $Output 'result.json') -Encoding utf8
Write-Host ("Y01_RESULT={0} config={1} configure={2} build={3} dll={4} exports={5} warnings={6} seconds={7}" -f $result.verdict, $Config, $configureExit, $buildExit, $dllExists, $exports.Count, $warnings, $result.seconds)
if (-not $pass) { exit 1 }
exit 0
