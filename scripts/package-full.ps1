#requires -Version 5.1
param([Parameter(Mandatory=$true)][string]$PortableTexRoot,[switch]$SkipBuild)
& (Join-Path $PSScriptRoot 'package-onefile.ps1') -Mode Full -PortableTexRoot $PortableTexRoot -SkipBuild:$SkipBuild
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
