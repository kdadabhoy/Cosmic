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
echo [STAGE 1] Configuring CMake for Visual Studio...

:: FIXED: Using native architecture auto-detection syntax.
:: This naturally selects your latest installed MSVC toolchain (VS 2022 / 2025 / 2026) 
:: without failing on rigid generator string checks.
cmake .. -A x64

if %errorlevel% neq 0 (
    echo ERROR: CMake configuration failed!
    echo Ensure "Desktop development with C++" is installed in your Visual Studio instance.
    pause
    exit /b %errorlevel%
)

:: 5. Compile Everything (Engine + Launcher + Plugins)
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