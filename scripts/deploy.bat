@echo off
rem ============================================================
rem  HFADM deploy script (windeployqt)
rem  Usage: scripts\deploy.bat [build_dir] [out_dir]
rem  Defaults:
rem    build_dir = build\Desktop_Qt_6_11_1_MinGW_64_bit_Release
rem    out_dir   = dist\HFADM
rem ============================================================

set QT_BIN=C:\Qt\6.11.1\mingw_64\bin
set BUILD_DIR=%~1
if "%BUILD_DIR%"=="" set BUILD_DIR=build\Desktop_Qt_6_11_1_MinGW_64_bit_Release
set OUT_DIR=%~2
if "%OUT_DIR%"=="" set OUT_DIR=dist\HFADM

if not exist "%BUILD_DIR%\HFADM.exe" (
    echo [ERROR] HFADM.exe not found in "%BUILD_DIR%"
    echo Please build Release first, e.g.:
    echo   cmake -S . -B build\Desktop_Qt_6_11_1_MinGW_64_bit_Release -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:\Qt\6.11.1\mingw_64
    echo   cmake --build build\Desktop_Qt_6_11_1_MinGW_64_bit_Release
    exit /b 1
)

if exist "%OUT_DIR%" rmdir /s /q "%OUT_DIR%"
mkdir "%OUT_DIR%" || exit /b 1

copy /y "%BUILD_DIR%\HFADM.exe" "%OUT_DIR%\" >nul || exit /b 1

"%QT_BIN%\windeployqt.exe" --release --no-translations --no-system-d3d-compiler "%OUT_DIR%\HFADM.exe"
if errorlevel 1 (
    echo [ERROR] windeployqt failed
    exit /b 1
)

echo.
echo [OK] Deploy complete: %OUT_DIR%
echo Run "%OUT_DIR%\HFADM.exe" to verify.
