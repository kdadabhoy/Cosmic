@echo off
SETLOCAL EnableDelayedExpansion
CLS
echo ======================================================
echo            Cosmic Engine - CORE ONLY BUILD
echo ======================================================

:: Accept optional config argument: build_engine.bat [Debug|Release]
set BUILD_CONFIG=%1
if "%BUILD_CONFIG%"=="" set BUILD_CONFIG=Debug

set "VS_PATH="
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
    for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (set "VS_PATH=%%i")
)

if defined VS_PATH (
    if exist "!VS_PATH!\Common7\Tools\VsDevCmd.bat" (
        echo [STAGE 0] Initializing MSVC Environment...
        call "!VS_PATH!\Common7\Tools\VsDevCmd.bat" -arch=x64
    )
) else (
    echo [INFO] Visual Studio not detected. Relying on system default CMake generator...
)

:: If no build directory exists, configure fresh with ENGINE_ONLY=ON.
:: If the cache exists but was configured with ENGINE_ONLY=OFF (e.g. after build.bat),
:: re-configure so the flag matches what this script needs.
set NEEDS_CONFIGURE=0
if not exist build (
    mkdir build
    set NEEDS_CONFIGURE=1
) else (
    findstr /C:"COSMIC_BUILD_ENGINE_ONLY:BOOL=OFF" build\CMakeCache.txt >nul 2>&1
    if not errorlevel 1 (
        echo [INFO] Cache has ENGINE_ONLY=OFF — reconfiguring for engine-only build...
        set NEEDS_CONFIGURE=1
    )
)

:: Report which engine configuration the cache holds — read only, never forced
:: (build_2d.bat / build_3d.bat are the mode setters).
set ENGINE_MODE=full 3D engine
if exist build\CMakeCache.txt (
    findstr /C:"COSMIC_2D_ONLY:BOOL=ON" build\CMakeCache.txt >nul 2>&1
    if not errorlevel 1 set ENGINE_MODE=2D-only engine
)
echo [MODE] !ENGINE_MODE!

cd build

if "!NEEDS_CONFIGURE!"=="1" (
    if defined VS_PATH (
        cmake .. -A x64 -DCOSMIC_BUILD_ENGINE_ONLY=ON
    ) else (
        cmake .. -DCOSMIC_BUILD_ENGINE_ONLY=ON
    )
)

echo [STAGE 1] Compiling Engine Host components...
cmake --build . --config %BUILD_CONFIG% --target Cosmic CosmicApp --parallel

echo.
echo SUCCESS: Engine Core Components Updated! (%BUILD_CONFIG%)
pause
ENDLOCAL
