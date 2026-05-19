@echo off
SETLOCAL
CLS

:: 1. Get the name of the current folder automatically
for %%I in ("%~dp0.") do set "PROJECT_NAME=%%~nxI"

echo ======================================================
echo           Cosmic Project Local Build Engine
echo           Target: %PROJECT_NAME%
echo ======================================================

:: 2. Find Visual Studio Installation Path
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    set "VS_PATH=%%i"
)

if not exist "%VS_PATH%" (
    echo ERROR: Visual Studio not found!
    pause
    exit /b 1
)

:: 3. Initialize Developer Environment
echo [STAGE 0] Initializing MSVC Environment...
call "%VS_PATH%\Common7\Tools\VsDevCmd.bat" -arch=x64

:: 4. Locate the central build folder by climbing up 2 directories
cd /d "%~dp0"
cd ..\..\

if not exist build (
    echo [ERROR] Main engine build directory not found!
    echo Please run the full engine build script once first.
    pause
    exit /b 1
)

cd build

:: 5. Compile ONLY this specific project target
echo.
echo [STAGE 1] Compiling Client Module: %PROJECT_NAME%...
:: FIXED: Pass the raw %PROJECT_NAME% directly as the target without the Projects/ path prefix
cmake --build . --target %PROJECT_NAME% --config Debug --parallel

if %errorlevel% neq 0 (
    echo.
    echo ERROR: Build failed for local target '%PROJECT_NAME%'!
    pause
    exit /b %errorlevel%
)

echo.
echo SUCCESS: %PROJECT_NAME%.dll compiled successfully!
timeout /t 3
ENDLOCAL