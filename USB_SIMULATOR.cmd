@echo off
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0DisplaySrc\Host\run_usb_simulator.ps1" %*
if errorlevel 1 pause
