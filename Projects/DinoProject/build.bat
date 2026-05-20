@echo off
SETLOCAL EnableDelayedExpansion
CLS
echo ======================================================
echo            Cosmic Engine - Project Module
echo ======================================================

:: 1. Smart MSVC Environment Detection (Optional Option)
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

:: 2. Smart SDK Detection (Looks at System Variable first, falls back if local)
if "%COSMIC_SDK%"=="" (
    echo [WARN] COSMIC_SDK environment variable not found. Testing local relative fallback...
    set "ENGINE_DETECTED_PATH=%~dp0..\.."
    pushd "%ENGINE_DETECTED_PATH%"
    set "COSMIC_SDK=%CD%"
    popd
)

echo [INFO] Environment Context Pathing Resolved To: %COSMIC_SDK%

if exist build rmdir /s /q build
mkdir build
cd build

echo [STAGE 1] Configuring CMake directly on Project...
cmake .. -DCOSMIC_SDK_DIR="%COSMIC_SDK%"

echo [STAGE 2] Building Game Module DLL...
cmake --build . --config Debug --parallel

:: Check if the compiler return code was something other than 0
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [ERROR] CMake Build Failed! Check compilation logs above.
    pause
    exit /b %ERRORLEVEL%
)

echo.
echo SUCCESS: Game Module Updated From Variable State context!
pause
ENDLOCAL