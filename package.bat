@echo off
SETLOCAL EnableDelayedExpansion
CLS
echo ======================================================
echo        Cosmic Engine - Distributable Packager
echo ======================================================

:: Builds a clean Release and stages a self-contained distributable folder, then
:: zips it.
::
:: Usage:
::   package.bat <AppName>    -> THE shipping path. Stages dist\<AppName>\ in the
::                               ONE package layout (AP-P1), identical to what the
::                               Starforge editor's File > Package produces and to
::                               what .github/workflows/release.yml uploads:
::
::                                 <App>.exe        (renamed CosmicApp.exe)
::                                 <App>.dll        (only this app's DLL)
::                                 Cosmic.dll
::                                 boot.cfg         (names <App>; sets the user:// identity)
::                                 assets\          (engine assets minus projects\)
::                                 assets\projects\<App>\
::                                 licenses\        (installer\licenses\MANIFEST.txt)
::                                 user\            (portable writable-root placeholder)
::
::                               Staging is done by installer\Stage-AppPackage.ps1,
::                               the same script release.yml calls, so the two CLI
::                               paths cannot drift. Pair with package_installer.bat
::                               for a setup exe.
::
::   package.bat              -> DEVELOPER SDK bundle (every project) at dist\Cosmic\,
::                               staged through `cmake --install`.
::
:: NOTE (AP-P1): `cmake --install` still works and is still how the no-argument SDK
:: bundle above is produced, but it is NOT a shipping path any more. It emits the old
:: multi-project layout (CosmicApp.exe at the root, project DLLs in projects\, every
:: app's assets) which no installer, shortcut or boot.cfg targets. Ship only
:: `package.bat <AppName>`, the editor's Package command, or release.yml.
::
:: Set COSMIC_NOPAUSE=1 to suppress the final pause (used when chained from
:: package_installer.bat).
:: Set COSMIC_STAGE_ONLY=1 to skip the clean configure+build and stage from the
:: existing build\Runtime\Release outputs (CI and acceptance runs that already built).

set "SDK_ROOT=%~dp0"
if "%SDK_ROOT:~-1%"=="\" set "SDK_ROOT=%SDK_ROOT:~0,-1%"

set "APP_NAME=%~1"
if defined APP_NAME (
    set "DIST_NAME=%APP_NAME%"
) else (
    set "DIST_NAME=Cosmic"
)
set "DIST_DIR=%SDK_ROOT%\dist\!DIST_NAME!"

:: Try to find MSVC environment but don't hard fail if it's missing
set "VS_PATH="
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
    for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (set "VS_PATH=%%i")
)

if defined VS_PATH (
    if exist "!VS_PATH!\Common7\Tools\VsDevCmd.bat" (
        echo [STAGE 0] Initializing MSVC Environment...
        call "!VS_PATH!\Common7\Tools\VsDevCmd.bat" -arch=x64
    )
) else (
    echo [INFO] Visual Studio not detected. Relying on system default CMake generator...
)

if defined COSMIC_STAGE_ONLY (
    echo [STAGE 1-2] Skipped ^(COSMIC_STAGE_ONLY^) - staging the existing Release build.
    goto :stage
)

:: 1. Clean configure + build (Release implies the distribution build)
if exist build rmdir /s /q build
mkdir build
cd build

:: -DCOSMIC_2D_ONLY=ON — this trunk packages the 2D-only engine (WO-03). The root
:: CMakeLists also rejects OFF, so a stale build\ cache can't smuggle a 3D binary
:: into the distributable; the explicit flag makes the intent visible here too.
:: -DCOSMIC_BUILD_TESTS=OFF — CosmicTests/CosmicRenderTests are developer-only
:: targets and are never built into, let alone staged from, a shipping tree.
echo [STAGE 1] Configuring (Release, 2D-only engine)...
if defined VS_PATH (
    cmake .. -A x64 -DCOSMIC_BUILD_ENGINE_ONLY=OFF -DCOSMIC_2D_ONLY=ON -DCOSMIC_BUILD_TESTS=OFF
) else (
    cmake .. -DCOSMIC_BUILD_ENGINE_ONLY=OFF -DCOSMIC_2D_ONLY=ON -DCOSMIC_BUILD_TESTS=OFF
)
if errorlevel 1 (
    echo.
    echo [ERROR] CMake configure failed! Check log output above.
    cd "%SDK_ROOT%"
    if not defined COSMIC_NOPAUSE pause
    ENDLOCAL
    exit /b 1
)

:: Guard: prove the configured tree really is 2D before it becomes a distributable
:: (the same assertion ci.yml and release.yml make).
findstr /R /C:"^COSMIC_2D_ONLY:BOOL=ON$" CMakeCache.txt >nul
if errorlevel 1 (
    echo.
    echo [ERROR] build\CMakeCache.txt is not COSMIC_2D_ONLY=ON - refusing to stage a non-2D distributable.
    cd "%SDK_ROOT%"
    if not defined COSMIC_NOPAUSE pause
    ENDLOCAL
    exit /b 1
)

echo [STAGE 2] Building Engine Host and All Client Projects (Release)...
cmake --build . --config Release --parallel
if errorlevel 1 (
    echo.
    echo [ERROR] Release build failed! Check log output above.
    cd "%SDK_ROOT%"
    if not defined COSMIC_NOPAUSE pause
    ENDLOCAL
    exit /b 1
)

:stage
cd "%SDK_ROOT%"
if exist "%SDK_ROOT%\dist" rmdir /s /q "%SDK_ROOT%\dist"

if not defined APP_NAME goto :sdkbundle

:: 3a. Single-app mode: THE shipping layout, staged by the same script release.yml
::     calls. The staged tree carries exactly one app; the shortcut boots it as
::     "<App>.exe" with no --project flag (boot.cfg also sets the per-app user://
::     identity, which --project would bypass).
echo [STAGE 3] Staging app "%APP_NAME%" to "%DIST_DIR%"...
powershell -NoProfile -ExecutionPolicy Bypass -File "%SDK_ROOT%\installer\Stage-AppPackage.ps1" -SdkRoot "%SDK_ROOT%" -App "%APP_NAME%" -RuntimeDir "%SDK_ROOT%\build\Runtime\Release" -OutDir "%DIST_DIR%" -ListOut "%SDK_ROOT%\dist\%APP_NAME%.files.txt"
if errorlevel 1 (
    echo.
    echo [ERROR] Staging failed! Check log output above.
    if not defined COSMIC_NOPAUSE pause
    ENDLOCAL
    exit /b 1
)
goto :zip

:sdkbundle
:: 3b. No app name: the DEVELOPER SDK bundle via the install rules. Not a shipping
::     layout (see the note in this file's header).
echo [STAGE 3] Staging developer SDK bundle to "%DIST_DIR%" (cmake --install; NOT a shipping layout)...
cmake --install build --config Release --prefix "%DIST_DIR%"
if errorlevel 1 (
    echo.
    echo [ERROR] Install/staging failed! Check log output above.
    if not defined COSMIC_NOPAUSE pause
    ENDLOCAL
    exit /b 1
)

:zip
:: 4. Zip the staged folder. Use PowerShell's Compress-Archive — reliable on
::    Windows 10/11. (Plain `tar` is often the GNU build from Git-for-Windows,
::    which cannot write .zip archives.)
echo [STAGE 4] Zipping to "%SDK_ROOT%\dist\!DIST_NAME!.zip"...
powershell -NoProfile -ExecutionPolicy Bypass -Command "Compress-Archive -Path '%DIST_DIR%\*' -DestinationPath '%SDK_ROOT%\dist\!DIST_NAME!.zip' -Force"
if errorlevel 1 (
    echo [WARN] Zip step failed. The staged folder at "%DIST_DIR%" is still valid.
)

cd "%SDK_ROOT%"
echo.
echo SUCCESS: Distributable ready at dist\!DIST_NAME!\  (and dist\!DIST_NAME!.zip)
if not defined COSMIC_NOPAUSE pause
ENDLOCAL
