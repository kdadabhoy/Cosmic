@echo off
SETLOCAL
CLS
echo ======================================================
echo           Cosmic Engine - CORE ONLY BUILD
echo ======================================================

for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (set "VS_PATH=%%i")
if not exist "%VS_PATH%" (echo ERROR: Visual Studio not found! && pause && exit /b 1)

echo [STAGE 0] Initializing MSVC Environment...
call "%VS_PATH%\Common7\Tools\VsDevCmd.bat" -arch=x64

if not exist build mkdir build
cd build

echo [STAGE 1] Configuring CMake for Engine Core Only...
cmake .. -A x64 -DCOSMIC_BUILD_ENGINE_ONLY=ON

echo [STAGE 2] Compiling Engine Host components...
cmake --build . --config Debug --target Cosmic CosmicApp --parallel

echo.
echo SUCCESS: Engine Core Components Updated!
pause
ENDLOCAL