param([ValidateSet('core-debug','core-release','desktop-debug','desktop-release','electron-dev')][string]$Preset = 'electron-dev')
$ErrorActionPreference = 'Stop'
Push-Location (Join-Path $PSScriptRoot '../..')
try {
    & cmake --preset $Preset
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} finally { Pop-Location }

