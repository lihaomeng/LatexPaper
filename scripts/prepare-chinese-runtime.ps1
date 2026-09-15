param([Parameter(Mandatory)][string]$Root, [switch]$AllowDownload)
$ErrorActionPreference = 'Stop'
$Root = [IO.Path]::GetFullPath($Root)
$bin = @('texmfs/install/miktex/bin/x64','miktex/bin/x64') | ForEach-Object { Join-Path $Root $_ } | Where-Object {
    Test-Path -LiteralPath (Join-Path $_ 'miktex.exe') -PathType Leaf
} | Select-Object -First 1
if (!$bin) { throw "MiKTeX utility not found below $Root" }
function Invoke-MiKTeX([string[]]$Arguments) {
    & (Join-Path $bin 'miktex.exe') @Arguments
    if ($LASTEXITCODE -ne 0) { throw "MiKTeX preparation failed: $($Arguments -join ' ')" }
}
if ($AllowDownload) {
    Write-Host 'Explicit provisioning: installing Chinese packages and dependencies.'
    foreach ($package in @('ctex','cjk','arphic','cjkpunct','zhmetrics','zhnumber','booktabs','geometry')) {
        $installed = & (Join-Path $bin 'miktex.exe') packages info --template '{isInstalled}' $package
        if ($LASTEXITCODE -ne 0) { throw "Cannot query MiKTeX package: $package" }
        if ((($installed -join '').Trim()) -eq 'true') { continue }
        Invoke-MiKTeX @('packages','install',$package)
    }
}
foreach ($file in @('ctexart.cls','CJKutf8.sty','CJKpunct.sty','zhnumber.sty','c70gbsn.fd','c70gkai.fd','booktabs.sty','geometry.sty')) {
    $found = & (Join-Path $bin 'kpsewhich.exe') $file
    if ($LASTEXITCODE -ne 0 -or !$found) { throw "Missing $file. Run prepare-chinese-runtime.ps1 -Root <source-runtime> -AllowDownload explicitly." }
}
$configuration = Join-Path $Root 'texmfs/config/tex/latex/lightoverleaf'
New-Item -ItemType Directory -Path $configuration -Force | Out-Null
$resources = Join-Path $PSScriptRoot '../resources/tex'
foreach ($file in @('ctexopts.cfg','ctex-fontset-lightoverleaf.def')) {
    Copy-Item -LiteralPath (Join-Path $resources $file) -Destination (Join-Path $configuration $file) -Force
}
Invoke-MiKTeX @('--disable-installer','fndb','refresh')
Invoke-MiKTeX @('--disable-installer','fontmaps','configure')
Write-Host 'Chinese runtime resources and font maps prepared (Arphic portable profile).'
$global:LASTEXITCODE = 0