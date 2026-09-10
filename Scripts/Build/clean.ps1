param([string]$DisplayDir, [switch]$Legacy)
$ErrorActionPreference = 'Stop'
$WorkspaceRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
if (!$DisplayDir) { $DisplayDir = Join-Path $WorkspaceRoot 'DisplaySrc' }
$ModuleRoot = (Resolve-Path -LiteralPath $DisplayDir).Path
$Targets = @(
    @{ Base=$WorkspaceRoot; Path=(Join-Path $WorkspaceRoot 'build') },
    @{ Base=$WorkspaceRoot; Path=(Join-Path $WorkspaceRoot '.build') },
    @{ Base=$ModuleRoot; Path=(Join-Path $ModuleRoot '.build') },
    @{ Base=$WorkspaceRoot; Path=(Join-Path $WorkspaceRoot 'Firmware/BluePillHMI/MDK-ARM/ExternalFlash') }
)
if ($Legacy) {
    foreach ($Name in @('Libraries','tools','vendor')) {
        $Targets += @{ Base=$WorkspaceRoot; Path=(Join-Path $WorkspaceRoot $Name) }
    }
}
foreach ($Target in $Targets) {
    $ResolvedTarget = [IO.Path]::GetFullPath($Target.Path)
    $AllowedPrefix = [IO.Path]::GetFullPath($Target.Base).TrimEnd('\') + '\'
    if (!$ResolvedTarget.StartsWith($AllowedPrefix, [StringComparison]::OrdinalIgnoreCase)) { throw "Unsafe cleanup path: $ResolvedTarget" }
    if (Test-Path -LiteralPath $ResolvedTarget) {
        Write-Output "Removing intermediate files: $ResolvedTarget"
        Remove-Item -LiteralPath $ResolvedTarget -Recurse -Force
    }
}
