#requires -Version 5.1
param(
    [string]$MiKTeXRoot = 'D:/CodeMyself/QTBest/thirdparty_install/miktex',
    [switch]$SkipBuild,
    [ValidateRange(60, 1800)][int]$SmokeTimeoutSeconds = 600
)
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'package-onefile.ps1') -Mode Full `
    -MiKTeXRoot $MiKTeXRoot -SkipBuild:$SkipBuild `
    -SmokeTimeoutSeconds $SmokeTimeoutSeconds
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
