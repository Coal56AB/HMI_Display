@echo off
setlocal
cd /d "%~dp0"
set "PYTHONUTF8=1"
py -3.7 Scripts\Flash\flash_assets.py %*
if errorlevel 1 pause
