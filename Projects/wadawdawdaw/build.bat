@echo off
SETLOCAL EnableDelayedExpansion
CLS

echo ======================================================
echo            Cosmic Engine - wadawdawdaw
echo ======================================================

:: 1. Smart MSVC Environment Detection
set "VS_PATH="
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
    for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (set "VS_PATH=%%i")
)

if defined VS_PATH (
    if exist "!VS_PATH!\Common7\Tools\VsDevCmd.bat" (
        echo [STAGE 0] Initializing MSVC Environment...
        call "!VS_PATH!\Common7\Tools\VsDevCmd.bat" -arch=x64
    )
)

:: 2. SDK Path Resolution
if "%COSMIC_SDK%"=="" (
    echo [WARN] COSMIC_SDK environment variable not found. Using C:/dev/Cosmic...
    set "COSMIC_SDK=C:/dev/Cosmic"
)

echo [INFO] SDK Root: %COSMIC_SDK%

if not exist build mkdir build
cd build

if not exist CMakeCache.txt (
    echo [STAGE 1] Configuring CMake...
    cmake .. -DCOSMIC_SDK_DIR="%COSMIC_SDK%"
)

echo [STAGE 2] Building wadawdawdaw.dll...
cmake --build . --config Debug --parallel

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [ERROR] Build failed! Check log output above.
    cd ..
    pause
    exit /b %ERRORLEVEL%
)

echo.
echo SUCCESS: wadawdawdaw.dll built and assets synced!
cd ..
ENDLOCAL