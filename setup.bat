@echo off
CLS
echo ======================================================
echo           Cosmic Engine - SDK Auto-Registrar
echo ======================================================
echo.

:: Get the absolute folder path where this batch file is currently running
set "CURRENT_DIR=%~dp0"

:: Strip trailing backslash for clean formatting
if "%CURRENT_DIR:~-1%"=="\" set "CURRENT_DIR=%CURRENT_DIR:~0,-1%"

echo Registering Cosmic Engine at: %CURRENT_DIR%

:: THE MAGIC COMMAND: Permanently sets the environment variable in Windows
setx COSMIC_SDK "%CURRENT_DIR%"

echo.
echo SUCCESS: COSMIC_SDK environment variable has been configured!
echo Please restart any open command prompts or Visual Studio instances.
pause