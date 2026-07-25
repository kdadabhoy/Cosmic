@echo off
SETLOCAL EnableDelayedExpansion
CLS
echo ======================================================
echo      Cosmic Engine - Full Universal Build (2D ONLY)
echo ======================================================
echo [MODE] 2D-only engine

:: Accept optional config argument: build_all_2d.bat [Debug|Release]
set BUILD_CONFIG=%1
if "%BUILD_CONFIG%"=="" set BUILD_CONFIG=Debug

:: Try to find MSVC environment but don't hard fail if it's missing
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

if exist build rmdir /s /q build
mkdir build
cd build

:: Clean configure in 2D mode. The three 3D game projects (Frontier, Engine3DDemo,
:: ForgeIsle) drop out of the project scanner, and assimp + recastnavigation are
:: never configured — see docs/plans/28-phase29-engine-split-plan.md §4.
echo [STAGE 1] Configuring Global Solution Tree (2D-only engine)...
if defined VS_PATH (
    cmake .. -A x64 -DCOSMIC_BUILD_ENGINE_ONLY=OFF -DCOSMIC_2D_ONLY=ON
) else (
    cmake .. -DCOSMIC_BUILD_ENGINE_ONLY=OFF -DCOSMIC_2D_ONLY=ON
)

echo [STAGE 2] Building Engine Host and All 2D Client Projects...
cmake --build . --config %BUILD_CONFIG% --parallel

echo.
echo SUCCESS: Full 2D System Context Built! (%BUILD_CONFIG%)
pause
ENDLOCAL
