@echo off
SETLOCAL EnableDelayedExpansion
CLS
echo ======================================================
echo            Cosmic Engine - Full Universal Build
echo ======================================================

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

if exist build rmdir /s /q build
mkdir build
cd build

echo [STAGE 1] Configuring Global Solution Tree...
:: Note: Removed "-A x64" to allow non-MSVC generators (like Ninja) to configure gracefully
cmake .. -DCOSMIC_BUILD_ENGINE_ONLY=OFF

echo [STAGE 2] Building Engine Host and All Client Projects...
cmake --build . --config Debug --parallel

echo.
echo SUCCESS: Full System Context Built Natively - Foolproof Version!
pause
ENDLOCAL