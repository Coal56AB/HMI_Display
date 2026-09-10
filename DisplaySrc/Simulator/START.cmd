@echo off
setlocal
set "PATH=%LOCALAPPDATA%\Programs\Python\Launcher;%PATH%"
cd /d "%~dp0"
if defined MUSICBOX_PYTHON (
  "%MUSICBOX_PYTHON%" main.py %*
) else (
  py -3.7 main.py %*
)
