@echo off
SETLOCAL EnableDelayedExpansion
CLS
echo ======================================================
echo            Cosmic Engine - CORE ONLY BUILD
echo ======================================================

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

if not exist build mkdir build
cd build

echo [STAGE 1] Configuring CMake for Engine Core Only...
cmake .. -DCOSMIC_BUILD_ENGINE_ONLY=ON

echo [STAGE 2] Compiling Engine Host components...
cmake --build . --config Debug --target Cosmic CosmicApp --parallel

echo.
echo SUCCESS: Engine Core Components Updated!
pause
ENDLOCAL