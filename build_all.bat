@echo off
SETLOCAL EnableDelayedExpansion
CLS
echo ======================================================
echo            Cosmic Engine - Full Universal Build
echo ======================================================

:: Accept optional config argument: build_all.bat [Debug|Release]
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

:: This is a clean build. This is the 2D-only stability trunk (WO-03): the tree
:: builds the 2D engine, always. The mode is passed explicitly and the root
:: CMakeLists rejects OFF, so a clean build here can only ever be 2D.
echo [MODE] 2D-only engine

if exist build rmdir /s /q build
mkdir build
cd build

echo [STAGE 1] Configuring Global Solution Tree (2D-only engine)...
if defined VS_PATH (
    cmake .. -A x64 -DCOSMIC_BUILD_ENGINE_ONLY=OFF -DCOSMIC_2D_ONLY=ON
) else (
    cmake .. -DCOSMIC_BUILD_ENGINE_ONLY=OFF -DCOSMIC_2D_ONLY=ON
)

echo [STAGE 2] Building Engine Host and All Client Projects...
cmake --build . --config %BUILD_CONFIG% --parallel

echo.
echo SUCCESS: Full System Context Built! (%BUILD_CONFIG%)
pause
ENDLOCAL
