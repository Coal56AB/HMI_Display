@echo off
setlocal
cd /d "%~dp0.."
set "PYTHONUTF8=1"
py -3 Scripts\build_simulator.py
if errorlevel 1 pause
