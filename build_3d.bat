@echo off
SETLOCAL
CLS
echo ======================================================
echo      Cosmic Engine - FULL 3D build is not on this branch
echo ======================================================
echo.
echo This is the 2D-only stability trunk (main). The full 3D engine — terrain,
echo voxel, water, navigation, the 3D renderer passes, model/assimp import and
echo the 3D sample projects — lives on the 'engine-3d' branch and the frozen
echo 'cosmic-pre-2d-2026-09-16' tag.
echo.
echo A 3D configure (-DCOSMIC_2D_ONLY=OFF) is REJECTED at configure time on this
echo branch by design (WO-03), so this script no longer attempts one.
echo.
echo   To build 3D:      git switch engine-3d ^&^& build_all.bat
echo   To build 2D here: build_2d.bat   (or just build.bat / build_all.bat)
echo.
pause
ENDLOCAL
exit /b 1
