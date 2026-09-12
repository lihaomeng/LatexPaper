param([ValidateSet('core-debug','core-release','desktop-debug','desktop-release')][string]$Preset = 'core-debug')
$ErrorActionPreference = 'Stop'
Push-Location (Join-Path $PSScriptRoot '..')
try {
    & cmake --build --preset $Preset
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} finally { Pop-Location }

