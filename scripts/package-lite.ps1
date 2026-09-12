#requires -Version 5.1
param([switch]$SkipBuild)
& (Join-Path $PSScriptRoot 'package-onefile.ps1') -Mode Lite -SkipBuild:$SkipBuild
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
