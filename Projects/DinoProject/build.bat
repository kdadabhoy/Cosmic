@echo off
SETLOCAL
CLS
echo ======================================================
echo           Cosmic Engine - Project Module: Dino
echo ======================================================

:: 1. Find Visual Studio Installation Path
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    set "VS_PATH=%%i"
)
if not exist "%VS_PATH%" (echo ERROR: Visual Studio not found! && pause && exit /b 1)

:: 2. Dynamic Absolute SDK Trace Resolution Validation
:: If the project remains nested inside the SDK layout structure, calculate it:
set "ENGINE_DETECTED_PATH=%~dp0..\.."

pushd "%ENGINE_DETECTED_PATH%"
set "COSMIC_SDK=%CD%"
popd

echo [INFO] Environment Context Pathing Resolved To: %COSMIC_SDK%

:: 3. Initialize Developer Environment
echo [STAGE 0] Initializing MSVC Environment...
call "%VS_PATH%\Common7\Tools\VsDevCmd.bat" -arch=x64

if exist build rmdir /s /q build
mkdir build
cd build

echo [STAGE 1] Configuring CMake directly on DinoProject...
cmake .. -A x64 -DCOSMIC_SDK_DIR="%COSMIC_SDK%"

echo [STAGE 2] Building DinoProject DLL Module...
cmake --build . --config Debug --parallel

echo.
echo SUCCESS: DinoProject Game Module Updated From Variable State context!
pause
ENDLOCAL