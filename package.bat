@echo off
SETLOCAL EnableDelayedExpansion
CLS
echo ======================================================
echo        Cosmic Engine - Distributable Packager
echo ======================================================

:: Builds a clean Release and stages a self-contained distributable folder at
:: dist\Cosmic\ containing only CosmicApp.exe, Cosmic.dll, project DLLs, the
:: bundled VC++ runtime DLLs, and assets/ — then zips it to dist\Cosmic.zip.

set "SDK_ROOT=%~dp0"
if "%SDK_ROOT:~-1%"=="\" set "SDK_ROOT=%SDK_ROOT:~0,-1%"
set "DIST_DIR=%SDK_ROOT%\dist\Cosmic"

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
    pause
    ENDLOCAL
    exit /b 1
)

echo [STAGE 2] Building Engine Host and All Client Projects (Release)...
cmake --build . --config Release --parallel
if errorlevel 1 (
    echo.
    echo [ERROR] Release build failed! Check log output above.
    cd "%SDK_ROOT%"
    pause
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
    pause
    ENDLOCAL
    exit /b 1
)

:: 3. Zip the staged folder (tar ships with Windows 10+)
echo [STAGE 4] Zipping to "%SDK_ROOT%\dist\Cosmic.zip"...
tar -a -c -f "%SDK_ROOT%\dist\Cosmic.zip" -C "%DIST_DIR%" .
if errorlevel 1 (
    echo [WARN] Zip step failed (tar unavailable?). The staged folder is still valid.
)

cd "%SDK_ROOT%"
echo.
echo SUCCESS: Distributable ready at dist\Cosmic\  (and dist\Cosmic.zip)
pause
ENDLOCAL
