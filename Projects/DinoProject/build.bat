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

:: 2. Initialize Developer Environment
echo [STAGE 0] Initializing MSVC Environment...
call "%VS_PATH%\Common7\Tools\VsDevCmd.bat" -arch=x64

:: 3. Setup Isolated Build Directory *inside* DinoProject
:: This keeps the root directory perfectly clean!
if not exist build mkdir build
cd build

echo [STAGE 1] Configuring CMake directly on DinoProject...
:: The ".." points to Projects/DinoProject/ since we are inside its build folder
cmake .. -A x64

echo [STAGE 2] Building DinoProject DLL Module...
cmake --build . --config Debug --parallel

echo.
echo SUCCESS: DinoProject Game Module Updated!
pause
ENDLOCAL