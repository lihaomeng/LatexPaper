#requires -Version 5.1
param(
    [switch]$SkipBuild,
    [ValidateRange(60, 1800)][int]$SmokeTimeoutSeconds = 600
)
& (Join-Path $PSScriptRoot 'package-onefile.ps1') -Mode Lite -SkipBuild:$SkipBuild `
    -SmokeTimeoutSeconds $SmokeTimeoutSeconds
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
