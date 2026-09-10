@echo off
setlocal
cd /d "%~dp0"
set "PYTHONUTF8=1"
py -3 Scripts\Build\build_firmware.py %*
if errorlevel 1 pause
