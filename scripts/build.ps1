param(
    [ValidateSet('core-debug','core-release','desktop-debug','desktop-release','--dev')][string]$Preset = 'core-debug',
    [Alias('-dev')][switch]$Dev,
    [string]$MiKTeXRoot
)
$ErrorActionPreference = 'Stop'
if ($Preset -eq '--dev') { $Dev = $true; $Preset = 'desktop-release' }
foreach ($argument in $args) {
    if ($argument -eq '--dev') { $Dev = $true }
    else { throw "Unknown build argument: $argument" }
}
Push-Location (Join-Path $PSScriptRoot '..')
try {
    if ($Dev -and -not $PSBoundParameters.ContainsKey('Preset')) { $Preset = 'desktop-release' }
    $testing = if ($Dev) { 'OFF' } else { 'ON' }
    $devMode = if ($Dev) { 'ON' } else { 'OFF' }
    & cmake --preset $Preset "-DBUILD_TESTING=$testing" "-DLIGHTOVERLEAF_DEV_BUILD=$devMode"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & cmake --build --preset $Preset
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    if ($Preset.StartsWith('desktop-')) {
        $configuration = if ($Preset.EndsWith('-debug')) { 'Debug' } else { 'Release' }
        $runtimeDirectory = Join-Path (Get-Location).Path "out/$Preset/bin/$configuration"
        foreach ($file in @('LightOverLeaf.exe', 'LightOverLeaf.dll', 'libcef.dll', 'web/index.html')) {
            if (!(Test-Path -LiteralPath (Join-Path $runtimeDirectory $file) -PathType Leaf)) {
                throw "Build output is incomplete: $runtimeDirectory/$file"
            }
        }
        if (!$MiKTeXRoot) {
            $dependencyLine = Get-Content "out/$Preset/CMakeCache.txt" | Where-Object { $_ -match '^LIGHTOVERLEAF_THIRDPARTY_ROOT:PATH=' } | Select-Object -First 1
            if (!$dependencyLine) { throw 'Cannot resolve LIGHTOVERLEAF_THIRDPARTY_ROOT; specify -MiKTeXRoot.' }
            $MiKTeXRoot = Join-Path ($dependencyLine -replace '^[^=]+=', '') 'miktex'
        }
        & (Join-Path $PSScriptRoot 'deploy-miktex.ps1') -Source $MiKTeXRoot -Destination (Join-Path $runtimeDirectory 'runtime/miktex')
        $application = Join-Path $runtimeDirectory 'LightOverLeaf.exe'
        Write-Host ''
        Write-Host 'BUILD SUCCEEDED - double-click this EXE to start:' -ForegroundColor Green
        Write-Host $application -ForegroundColor Cyan
        Write-Host 'Keep the EXE together with its DLLs, web, platforms and locales folders.'
    }
} finally { Pop-Location }

