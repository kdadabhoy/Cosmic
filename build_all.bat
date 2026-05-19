@echo off
SETLOCAL
CLS
echo ======================================================
echo           Cosmic Engine - Full Universal Build
echo           Target: Core Engine + All Project Modules
echo ======================================================

:: 1. Find Visual Studio Installation Path
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    set "VS_PATH=%%i"
)

if not exist "%VS_PATH%" (
    echo ERROR: Visual Studio not found!
    pause
    exit /b 1
)

:: 2. Initialize Developer Environment
echo [STAGE 0] Initializing MSVC Environment...
call "%VS_PATH%\Common7\Tools\VsDevCmd.bat" -arch=x64

:: 3. Setup Build Directory (Clean build to prevent Toolset Mismatch)
if exist build (
    echo [INFO] Cleaning old build cache...
    rmdir /s /q build
)
mkdir build
cd build

:: 4. Configure Project
echo [STAGE 1] Configuring CMake for Visual Studio 2026...

:: Using the version 18 generator for VS 2026
cmake .. -G "Visual Studio 18 2026" -A x64

:: Fallback if CMake version doesn't recognize the 2026 string yet
if %errorlevel% neq 0 (
    echo [INFO] Specific 2026 generator failed, trying auto-detection...
    cmake .. -A x64
)

if %errorlevel% neq 0 (
    echo ERROR: CMake configuration failed!
    echo Ensure "Desktop development with C++" is installed in Visual Studio 2026.
    pause
    exit /b %errorlevel%
)

:: 5. Compile Everything (Engine + Plugins)
echo.
echo [STAGE 2] Building Engine Host and All Client Projects (Multi-Threaded)...
:: No explicit target here compiles the entire solution tree natively!
cmake --build . --config Debug --parallel
if %errorlevel% neq 0 (
    echo ERROR: Global build failed!
    pause
    exit /b %errorlevel%
)

echo.
echo SUCCESS: Engine Core and Game Modules Compiled Successfully!
echo Path: %CD%
pause
ENDLOCAL