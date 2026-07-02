@echo off
SETLOCAL EnableDelayedExpansion
CLS
echo ======================================================
echo     Cosmic Engine - Single-App Installer Builder
echo ======================================================

:: Builds a Release single-app distributable via package.bat, then compiles an
:: Inno Setup installer that puts ONE shortcut on the desktop which boots the
:: app directly ("CosmicApp.exe --project <AppName>").
::
:: Usage:   package_installer.bat <AppName>
:: Example: package_installer.bat SF_Telem
::
:: Requires Inno Setup 6 (free): https://jrsoftware.org/isinfo.php

if "%~1"=="" (
    echo Usage: package_installer.bat ^<AppName^>
    echo Example: package_installer.bat SF_Telem
    ENDLOCAL
    exit /b 1
)
set "APP_NAME=%~1"

set "SDK_ROOT=%~dp0"
if "%SDK_ROOT:~-1%"=="\" set "SDK_ROOT=%SDK_ROOT:~0,-1%"

:: --- Read the engine version from the single source of truth (Version.h) ---
set "APP_VERSION=0.0.0"
for /f "tokens=3" %%v in ('findstr /C:"#define COSMIC_VERSION_STRING" "%SDK_ROOT%\Cosmic\src\core\Version.h"') do set "APP_VERSION=%%~v"
echo [INFO] Engine version: %APP_VERSION%

:: --- Locate the Inno Setup compiler ---
set "ISCC="
if exist "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not defined ISCC if exist "%ProgramFiles%\Inno Setup 6\ISCC.exe" set "ISCC=%ProgramFiles%\Inno Setup 6\ISCC.exe"
if not defined ISCC (
    for /f "delims=" %%p in ('where ISCC 2^>nul') do if not defined ISCC set "ISCC=%%p"
)
if not defined ISCC (
    echo [ERROR] Inno Setup 6 not found. Install it from https://jrsoftware.org/isinfo.php
    echo         ^(the staged dist folder will still be built below if you continue manually^).
    ENDLOCAL
    exit /b 1
)
echo [INFO] Inno Setup: !ISCC!

:: --- Stage 1: clean Release build + single-app dist folder ---
set "COSMIC_NOPAUSE=1"
call "%SDK_ROOT%\package.bat" %APP_NAME%
if errorlevel 1 (
    echo [ERROR] Packaging failed — see output above.
    ENDLOCAL
    exit /b 1
)

:: --- Stage 2: compile the installer ---
echo [STAGE 5] Compiling installer...
"!ISCC!" /DAppName=%APP_NAME% /DAppVersion=%APP_VERSION% "%SDK_ROOT%\installer\CosmicSetup.iss"
if errorlevel 1 (
    echo [ERROR] Inno Setup compilation failed — see output above.
    ENDLOCAL
    exit /b 1
)

echo.
echo SUCCESS: Installer ready at dist\%APP_NAME%-Setup-%APP_VERSION%.exe
echo          Run it on any Windows 10/11 x64 machine — no SDK, no env vars needed.
ENDLOCAL
