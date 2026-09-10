@echo off
setlocal
cd /d "%~dp0"
set "PYTHONUTF8=1"
if exist "%LocalAppData%\Programs\Python\Python37\python.exe" (
  "%LocalAppData%\Programs\Python\Python37\python.exe" Scripts\connect_project.py
) else (
  py -3.7 Scripts\connect_project.py
)
if errorlevel 1 pause
