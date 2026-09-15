#requires -Version 5.1
param(
    [string]$MiKTeXRoot = 'D:/CodeMyself/QTBest/thirdparty_install/miktex',
    [switch]$SkipBuild,
    [ValidateRange(30, 600)][int]$SmokeTimeoutSeconds = 120
)
Write-Warning 'Compatibility entry: use package.ps1 for the Full MiKTeX green directory.'
& (Join-Path $PSScriptRoot 'package.ps1') -MiKTeXRoot $MiKTeXRoot `
    -SkipBuild:$SkipBuild -SmokeTimeoutSeconds $SmokeTimeoutSeconds
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
