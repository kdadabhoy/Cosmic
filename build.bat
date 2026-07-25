@echo off
SETLOCAL EnableDelayedExpansion
CLS
echo ======================================================
echo            Cosmic Engine - Incremental Build
echo ======================================================

:: Accept optional config argument: build.bat [Debug|Release]
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

:: 2. If no build directory exists, configure fresh with ENGINE_ONLY=OFF.
::    If the cache exists but was configured with ENGINE_ONLY=ON (e.g. after build_engine.bat),
::    re-configure so the flag matches what this script needs.
set NEEDS_CONFIGURE=0
if not exist build (
    mkdir build
    set NEEDS_CONFIGURE=1
) else (
    findstr /C:"COSMIC_BUILD_ENGINE_ONLY:BOOL=ON" build\CMakeCache.txt >nul 2>&1
    if not errorlevel 1 (
        echo [INFO] Cache has ENGINE_ONLY=ON — reconfiguring for full build...
        set NEEDS_CONFIGURE=1
    )
)

:: 3. Report which engine configuration the cache holds. COSMIC_2D_ONLY is read,
::    never written: the CMake cache is sticky, so a tree configured 2D (via
::    build_2d.bat, or the engine-2d worktree) stays 2D and this script is its fast
::    incremental build. Use build_2d.bat / build_3d.bat to change modes.
set ENGINE_MODE=full 3D engine
if exist build\CMakeCache.txt (
    findstr /C:"COSMIC_2D_ONLY:BOOL=ON" build\CMakeCache.txt >nul 2>&1
    if not errorlevel 1 set ENGINE_MODE=2D-only engine
)
echo [MODE] !ENGINE_MODE!

cd build

if "!NEEDS_CONFIGURE!"=="1" (
    if defined VS_PATH (
        cmake .. -A x64 -DCOSMIC_BUILD_ENGINE_ONLY=OFF
    ) else (
        cmake .. -DCOSMIC_BUILD_ENGINE_ONLY=OFF
    )
)

:: 4. Incremental build — CMake re-runs configure automatically if CMakeLists.txt changed.
cmake --build . --config %BUILD_CONFIG% --parallel

echo.
echo [DONE] Incremental Build Complete! (%BUILD_CONFIG%)
pause
ENDLOCAL
