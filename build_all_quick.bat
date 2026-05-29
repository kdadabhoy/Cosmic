@echo off
SETLOCAL EnableDelayedExpansion
CLS
echo ======================================================
echo            Cosmic Engine - Quick Full Universal Build
echo ======================================================

@echo off
SETLOCAL EnableDelayedExpansion

:: 1. Quick MSVC Environment Setup (if available)
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
    for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        if exist "%%i\Common7\Tools\VsDevCmd.bat" call "%%i\Common7\Tools\VsDevCmd.bat" -arch=x64 >nul 2>&1
    )
)

:: 2. Create build directory ONLY if it doesn't exist (No rmdir!)
if not exist build mkdir build
cd build

:: 3. Fast Configure & Build
cmake .. -DCOSMIC_BUILD_ENGINE_ONLY=OFF
cmake --build . --config Debug --parallel

echo.
echo [DONE] Quick Build Complete!
pause
ENDLOCAL