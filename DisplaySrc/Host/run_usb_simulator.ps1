param([string]$Python = '', [string]$Port = '')
$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if(-not $Python){$Python=Join-Path $root '.build/platformio/core/Scripts/python.exe'}
if(-not (Test-Path -LiteralPath $Python)){$Python=Join-Path $env:USERPROFILE '.platformio/penv/Scripts/python.exe'}
if(-not (Test-Path -LiteralPath $Python)){throw 'Run BUILD_ESP32.cmd first or pass -Python with Python 3.9+.'}
& $Python -c 'import serial,mido,PySide6'
if($LASTEXITCODE){
    & $Python -m pip install --disable-pip-version-check -r "$PSScriptRoot/requirements-usb.txt"
    if($LASTEXITCODE){throw 'Simulator dependencies installation failed'}
}
$arguments=@('-B',"$PSScriptRoot/usb_simulator.py")
if($Port){$arguments+=@('--port',$Port)}
& $Python @arguments
if($LASTEXITCODE){throw 'USB simulator failed'}
