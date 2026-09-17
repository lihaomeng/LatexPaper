param(
    [ValidateSet('core-debug','core-release','desktop-debug','desktop-release','electron-dev','--dev')][string]$Preset = 'core-debug',
    [Alias('-dev')][switch]$Dev,
    [string]$MiKTeXRoot,
    [switch]$RefreshRuntime,
    [switch]$FreshConfigure
)
$ErrorActionPreference = 'Stop'
if ($Preset -eq '--dev') { $Dev = $true; $Preset = 'electron-dev' }
foreach ($argument in $args) {
    if ($argument -eq '--dev') { $Dev = $true }
    else { throw "Unknown build argument: $argument" }
}
if ($Dev -and -not $PSBoundParameters.ContainsKey('Preset')) { $Preset = 'electron-dev' }
if ($Preset -eq 'electron-dev') {
    & (Join-Path $PSScriptRoot 'internal/build-electron.ps1') -Dev:$Dev -MiKTeXRoot $MiKTeXRoot -RefreshRuntime:$RefreshRuntime -FreshConfigure:$FreshConfigure
    return
}
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
. (Join-Path $PSScriptRoot 'internal/cmakecontext.ps1')
Push-Location (Join-Path $repo 'backend/latexlocalservice')
try {
    $testing = if ($Dev) { 'OFF' } else { 'ON' }
    $devMode = if ($Dev) { 'ON' } else { 'OFF' }
    $configureArguments = @('--preset', $Preset)
    $configureArguments += Get-CMakeRefreshArguments -Repo $repo -Preset $Preset -Fresh:$FreshConfigure
    $configureArguments += @("-DBUILD_TESTING=$testing", "-DLIGHTOVERLEAF_DEV_BUILD=$devMode")
    & cmake @configureArguments
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & cmake --build --preset $Preset
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    if ($Preset.StartsWith('desktop-')) {
        $configuration = if ($Preset.EndsWith('-debug')) { 'Debug' } else { 'Release' }
        $runtimeDirectory = Join-Path $repo "out/$Preset/bin/$configuration"
        foreach ($file in @('LightOverLeaf.exe', 'LightOverLeaf.dll', 'libcef.dll', 'web/index.html')) {
            if (!(Test-Path -LiteralPath (Join-Path $runtimeDirectory $file) -PathType Leaf)) {
                throw "Build output is incomplete: $runtimeDirectory/$file"
            }
        }
        if (!$MiKTeXRoot) {
            $dependencyLine = Get-Content (Join-Path $repo "out/$Preset/CMakeCache.txt") | Where-Object { $_ -match '^LIGHTOVERLEAF_THIRDPARTY_ROOT:PATH=' } | Select-Object -First 1
            if (!$dependencyLine) { throw 'Cannot resolve LIGHTOVERLEAF_THIRDPARTY_ROOT; specify -MiKTeXRoot.' }
            $MiKTeXRoot = Join-Path ($dependencyLine -replace '^[^=]+=', '') 'miktex'
        }
        if ($Dev) {
            & (Join-Path $PSScriptRoot 'runtime/ensure-dev-runtime.ps1') -Source $MiKTeXRoot -Destination (Join-Path $runtimeDirectory 'runtime/miktex') -Refresh:$RefreshRuntime
        } else {
            & (Join-Path $PSScriptRoot 'runtime/deploy-miktex.ps1') -Source $MiKTeXRoot -Destination (Join-Path $runtimeDirectory 'runtime/miktex')
        }
        $application = Join-Path $runtimeDirectory 'LightOverLeaf.exe'
        Write-Host ''
        Write-Host 'BUILD SUCCEEDED - double-click this EXE to start:' -ForegroundColor Green
        Write-Host $application -ForegroundColor Cyan
        Write-Host 'Keep the EXE together with its DLLs, web, platforms and locales folders.'
    }
} finally { Pop-Location }

