@echo off
setlocal
cd /d "%~dp0.."
set "PYTHONUTF8=1"
if exist "Release\Simulator\PCH_Simulator\PCH_Simulator.exe" (
  start "" "Release\Simulator\PCH_Simulator\PCH_Simulator.exe"
  exit /b
)
if not exist "Release\Native\hmi_renderer.dll" (
  py -3 Scripts\build_native_renderer.py
  if errorlevel 1 exit /b 1
)
py -3 Simulator\desktop_simulator.py
if errorlevel 1 pause
