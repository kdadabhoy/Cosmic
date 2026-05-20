@echo off
SETLOCAL
CLS
echo ======================================================
echo           Cosmic Engine - Full Universal Build
echo ======================================================

for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (set "VS_PATH=%%i")
if not exist "%VS_PATH%" (echo ERROR: Visual Studio not found! && pause && exit /b 1)

echo [STAGE 0] Initializing MSVC Environment...
call "%VS_PATH%\Common7\Tools\VsDevCmd.bat" -arch=x64

if exist build rmdir /s /q build
mkdir build
cd build

echo [STAGE 1] Configuring Global Solution Tree...
cmake .. -A x64 -DCOSMIC_BUILD_ENGINE_ONLY=OFF

echo [STAGE 2] Building Engine Host and All Client Projects...
cmake --build . --config Debug --parallel

echo.
echo SUCCESS: Full System Context Built Natively!
pause
ENDLOCAL