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
::   package.bat              -> full SDK dist (every project) at dist\Cosmic\
::   package.bat <AppName>    -> single-app dist at dist\<AppName>\ containing
::                               ONLY that project's DLL and assets (e.g.
::                               "package.bat SF_Telem"). Pair with
::                               package_installer.bat for a setup exe.
::
:: Set COSMIC_NOPAUSE=1 to suppress the final pause (used when chained from
:: package_installer.bat).

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

:: 1. Clean configure + build (Release implies the distribution build)
if exist build rmdir /s /q build
mkdir build
cd build

echo [STAGE 1] Configuring (Release)...
if defined VS_PATH (
    cmake .. -A x64 -DCOSMIC_BUILD_ENGINE_ONLY=OFF
) else (
    cmake .. -DCOSMIC_BUILD_ENGINE_ONLY=OFF
)
if errorlevel 1 (
    echo.
    echo [ERROR] CMake configure failed! Check log output above.
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

:: 2. Stage a clean distributable folder via install rules
echo [STAGE 3] Staging distributable to "%DIST_DIR%"...
if exist "%SDK_ROOT%\dist" rmdir /s /q "%SDK_ROOT%\dist"
cmake --install . --config Release --prefix "%DIST_DIR%"
if errorlevel 1 (
    echo.
    echo [ERROR] Install/staging failed! Check log output above.
    cd "%SDK_ROOT%"
    if not defined COSMIC_NOPAUSE pause
    ENDLOCAL
    exit /b 1
)

:: 2b. Single-app mode: prune every project except the requested one so the
::     distributable carries exactly one app (the desktop shortcut boots it via
::     "CosmicApp.exe --project <AppName>").
if defined APP_NAME (
    echo [STAGE 3b] Pruning distributable to app "%APP_NAME%"...
    if not exist "%DIST_DIR%\projects\%APP_NAME%.dll" (
        echo [ERROR] Project DLL "%APP_NAME%.dll" was not produced by the build!
        cd "%SDK_ROOT%"
        if not defined COSMIC_NOPAUSE pause
        ENDLOCAL
        exit /b 1
    )
    for %%f in ("%DIST_DIR%\projects\*.dll") do (
        if /I not "%%~nf"=="%APP_NAME%" del "%%f"
    )
    for /d %%d in ("%DIST_DIR%\assets\projects\*") do (
        if /I not "%%~nxd"=="%APP_NAME%" rmdir /s /q "%%d"
    )
)

:: 3. Zip the staged folder. Use PowerShell's Compress-Archive — reliable on
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
