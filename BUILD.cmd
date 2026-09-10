@echo off
setlocal
set "PATH=%LOCALAPPDATA%\Programs\Python\Launcher;%PATH%"
cd /d "%~dp0"
set "PYTHONUTF8=1"
py -3.7 Scripts\Build\build_firmware.py %*
if errorlevel 1 pause
