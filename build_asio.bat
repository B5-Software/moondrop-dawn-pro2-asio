@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

echo [1/2] Locating MinGW g++ compiler...
set "GXX="
where g++ >nul 2>&1
if !errorlevel! equ 0 (
    set "GXX=g++"
) else if exist "C:\Program Files\mingw64\bin\g++.exe" (
    set "GXX=C:\Program Files\mingw64\bin\g++.exe"
) else if exist "C:\msys64\mingw64\bin\g++.exe" (
    set "GXX=C:\msys64\mingw64\bin\g++.exe"
)

if "!GXX!"=="" (
    echo [ERROR] MinGW g++ not found. Please ensure MinGW-w64 is installed and added to PATH.
    exit /b 1
)

echo [2/2] Building dawn_pro2_asio.dll...
"!GXX!" -O2 -shared -static -Wall -o dawn_pro2_asio.dll.new dawn_asio.cpp dawn_asio.def -I. -lwinmm -ladvapi32 -luser32 -lole32 -luuid
if errorlevel 1 (
    echo [ERROR] Build failed.
    exit /b 1
)

:: Handle moving the binary
move /y dawn_pro2_asio.dll.new dawn_pro2_asio.dll >nul 2>&1
if errorlevel 1 (
    set "STAMP=%RANDOM%"
    ren dawn_pro2_asio.dll "dawn_pro2_asio_old_!STAMP!.dll" >nul 2>&1
    move /y dawn_pro2_asio.dll.new dawn_pro2_asio.dll >nul 2>&1
)

echo [SUCCESS] Build completed successfully: dawn_pro2_asio.dll
exit /b 0
