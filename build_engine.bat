@echo off
SETLOCAL
CLS
echo ======================================================
echo           Cosmic Engine - Core Host Only Build
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

:: 3. Complete Deep Clean Setup
if exist build (
    echo [INFO] Cleaning old core build cache...
    rmdir /s /q build
)
mkdir build
cd build

:: 4. Configure Project Targets
echo [STAGE 1] Configuring CMake for Visual Studio 2026...
cmake .. -G "Visual Studio 18 2026" -A x64

if %errorlevel% neq 0 (
    echo [INFO] Specific 2026 generator failed, trying auto-detection...
    cmake .. -A x64
)

if %errorlevel% neq 0 (
    echo ERROR: CMake configuration failed!
    pause
    exit /b %errorlevel%
)

:: 5. Compile ONLY the Engine Core Target
echo.
echo [STAGE 2] Building Engine Host Core Only (Skipping Projects)...
:: CHANGED: Added '--target Cosmic' to bypass any project sub-modules
cmake --build . --target Cosmic --config Debug --parallel
if %errorlevel% neq 0 (
    echo ERROR: Core engine build failed!
    pause
    exit /b %errorlevel%
)

echo.
echo SUCCESS: Engine Host Core Compiled Successfully!
echo Path: %CD%
pause
ENDLOCAL