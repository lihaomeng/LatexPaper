param([ValidateSet('core-debug','core-release','desktop-debug','desktop-release','electron-dev')][string]$Preset = 'electron-dev')
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
. (Join-Path $PSScriptRoot 'cmakecontext.ps1')
Push-Location (Join-Path $repo 'backend/latexlocalservice')
try {
    $configureArguments = @('--preset', $Preset)
    $configureArguments += Get-CMakeRefreshArguments -Repo $repo -Preset $Preset
    & cmake @configureArguments
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} finally { Pop-Location }

