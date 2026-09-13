param(
    [ValidateSet('display','template')][string]$Environment = 'display',
    [string]$Python = '',
    [switch]$Upload,
    [string]$Port = ''
)
$ErrorActionPreference = 'Stop'
# Use the same Core, SDK and toolchain as the VS Code tasks. Sharing only the
# output directory while switching SDK paths invalidates the incremental build.
if (-not $Python) {
    $Python = Join-Path $env:USERPROFILE '.platformio/penv/Scripts/python.exe'
}
if (-not (Test-Path -LiteralPath $Python)) {
    throw 'PlatformIO Core is missing. Complete its installation in VS Code.'
}
$arguments = @('-m','platformio','run','-d',$PSScriptRoot,'-e',$Environment)
if ($Upload) {
    $arguments += @('-t','upload')
    if ($Port) { $arguments += @('--upload-port',$Port) }
}
& $Python @arguments
if ($LASTEXITCODE) { throw "ESP32 $Environment build failed" }