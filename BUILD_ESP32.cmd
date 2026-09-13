@echo off
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Firmware\ESP32\build.ps1" %*
if errorlevel 1 (echo Build failed. & pause & exit /b 1)
pause
