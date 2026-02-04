@echo off
CLS
echo ======================================================
echo           Cosmic Engine - Universal Build
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

:: 3. Setup Build Directory
if not exist build mkdir build
cd build

:: 4. Configure Project
echo [STAGE 1] Configuring CMake for Visual Studio 2026...

:: Try VS 2026 first, fallback to 2022 if configuration fails
cmake .. -G "Visual Studio 19 2026" -A x64
if %errorlevel% neq 0 (
    echo [INFO] VS 2026 not detected by CMake, trying VS 2022 fallback...
    cmake .. -G "Visual Studio 17 2022" -A x64
)

if %errorlevel% neq 0 (
    echo ERROR: CMake configuration failed!
    pause
    exit /b %errorlevel%
)

:: 5. Compile
echo.
echo [STAGE 2] Building Engine and Sandbox (Multi-Threaded)...
cmake --build . --config Debug --parallel
if %errorlevel% neq 0 (
    echo ERROR: Build failed!
    pause
    exit /b %errorlevel%
)

echo SUCCESS: Build Finished!
pause
