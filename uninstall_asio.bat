@echo off
cd /d "%~dp0"
echo Unregistering MOONDROP DAWN PRO 2 ASIO Driver...
regsvr32 /u /s "%~dp0dawn_pro2_asio.dll"

reg delete "HKCU\Software\ASIO\MOONDROP DAWN PRO 2 ASIO" /f >nul 2>&1
reg delete "HKLM\Software\ASIO\MOONDROP DAWN PRO 2 ASIO" /f >nul 2>&1
reg delete "HKLM\Software\WOW6432Node\ASIO\MOONDROP DAWN PRO 2 ASIO" /f >nul 2>&1

echo Done!
pause
