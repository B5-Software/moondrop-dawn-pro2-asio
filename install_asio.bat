@echo off
cd /d "%~dp0"
echo Registering MOONDROP DAWN PRO 2 ASIO Driver...
regsvr32 /s "%~dp0dawn_pro2_asio.dll"

:: Configure 64-bit and 32-bit registry entries
reg add "HKCU\Software\ASIO\MOONDROP DAWN PRO 2 ASIO" /v "CLSID" /t REG_SZ /d "{B9B1E0A1-35D8-011D-8000-000000000001}" /f >nul 2>&1
reg add "HKCU\Software\ASIO\MOONDROP DAWN PRO 2 ASIO" /v "Description" /t REG_SZ /d "MOONDROP DAWN PRO 2 ASIO" /f >nul 2>&1
reg add "HKLM\Software\ASIO\MOONDROP DAWN PRO 2 ASIO" /v "CLSID" /t REG_SZ /d "{B9B1E0A1-35D8-011D-8000-000000000001}" /f >nul 2>&1
reg add "HKLM\Software\ASIO\MOONDROP DAWN PRO 2 ASIO" /v "Description" /t REG_SZ /d "MOONDROP DAWN PRO 2 ASIO" /f >nul 2>&1
reg add "HKLM\Software\WOW6432Node\ASIO\MOONDROP DAWN PRO 2 ASIO" /v "CLSID" /t REG_SZ /d "{B9B1E0A1-35D8-011D-8000-000000000001}" /f >nul 2>&1
reg add "HKLM\Software\WOW6432Node\ASIO\MOONDROP DAWN PRO 2 ASIO" /v "Description" /t REG_SZ /d "MOONDROP DAWN PRO 2 ASIO" /f >nul 2>&1

echo.
echo ========================================================
echo MOONDROP DAWN PRO 2 ASIO Driver registered successfully!
echo ========================================================
pause
