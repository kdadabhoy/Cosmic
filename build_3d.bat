@echo off
SETLOCAL EnableDelayedExpansion
CLS
echo ======================================================
echo      Cosmic Engine - Incremental Build (FULL 3D)
echo ======================================================
echo [MODE] full 3D engine

:: Accept optional config argument: build_3d.bat [Debug|Release]
set BUILD_CONFIG=%1
if "%BUILD_CONFIG%"=="" set BUILD_CONFIG=Debug

:: 1. MSVC Environment Setup (if available)
set "VS_PATH="
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
    for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (set "VS_PATH=%%i")
)

if defined VS_PATH (
    if exist "!VS_PATH!\Common7\Tools\VsDevCmd.bat" (
        call "!VS_PATH!\Common7\Tools\VsDevCmd.bat" -arch=x64 >nul 2>&1
    )
)

:: 2. This script is the 3D mode SETTER — the way back from build_2d.bat. Only
::    reconfigures when the cache is absent or currently 2D-only.
set NEEDS_CONFIGURE=0
if not exist build (
    mkdir build
    set NEEDS_CONFIGURE=1
) else (
    if not exist build\CMakeCache.txt (
        echo [INFO] No CMake cache — configuring for the full 3D engine...
        set NEEDS_CONFIGURE=1
    ) else (
        findstr /C:"COSMIC_2D_ONLY:BOOL=ON" build\CMakeCache.txt >nul 2>&1
        if not errorlevel 1 (
            echo [INFO] Cache has COSMIC_2D_ONLY=ON — reconfiguring for the full 3D engine...
            set NEEDS_CONFIGURE=1
        )
    )
)

cd build

if "!NEEDS_CONFIGURE!"=="1" (
    if defined VS_PATH (
        cmake .. -A x64 -DCOSMIC_BUILD_ENGINE_ONLY=OFF -DCOSMIC_2D_ONLY=OFF
    ) else (
        cmake .. -DCOSMIC_BUILD_ENGINE_ONLY=OFF -DCOSMIC_2D_ONLY=OFF
    )
)

:: 3. Incremental build — CMake re-runs configure automatically if CMakeLists.txt changed.
cmake --build . --config %BUILD_CONFIG% --parallel

echo.
echo [DONE] Incremental Build Complete! (full 3D, %BUILD_CONFIG%)
pause
ENDLOCAL
