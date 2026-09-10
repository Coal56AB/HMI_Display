@echo off
setlocal
cd /d "%~dp0"
if defined MUSICBOX_PYTHON (
  "%MUSICBOX_PYTHON%" main.py %*
) else (
  py -3 main.py %*
)
