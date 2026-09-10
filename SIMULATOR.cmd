@echo off
if not exist "%~dp0DisplaySrc\Simulator\START.cmd" (
  echo No simulator in DisplaySrc. Connect a display module first.
  exit /b 1
)
call "%~dp0DisplaySrc\Simulator\START.cmd" %*
