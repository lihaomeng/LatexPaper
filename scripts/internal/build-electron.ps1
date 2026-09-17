param(
    [Alias('-dev')][switch]$Dev,
    [string]$MiKTeXRoot,
    [switch]$RefreshRuntime,
    [switch]$FreshConfigure
)
$ErrorActionPreference = 'Stop'
if (!$Dev) { throw 'Use scripts/build.ps1 --dev for the Electron development build.' }
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
. (Join-Path $PSScriptRoot 'cmakecontext.ps1')
Push-Location (Join-Path $repo 'backend/latexlocalservice')
try {
    $configureArguments = @('--preset', 'electron-dev')
    $configureArguments += Get-CMakeRefreshArguments -Repo $repo -Preset 'electron-dev' -Fresh:$FreshConfigure
    $configureArguments += @('-DBUILD_TESTING=OFF', '-DLIGHTOVERLEAF_DEV_BUILD=ON', '-DLIGHTOVERLEAF_BUILD_DESKTOP=OFF', '-DLIGHTOVERLEAF_BUILD_ELECTRON=ON')
    & cmake @configureArguments
    if ($LASTEXITCODE -ne 0) { throw 'Backend configure failed.' }
    & cmake --build --preset electron-dev
    if ($LASTEXITCODE -ne 0) { throw 'Backend build failed.' }
    & (Join-Path $PSScriptRoot 'build-frontend.ps1') -Mode dev
    Push-Location (Join-Path $repo 'frontend/electron')
    try {
        $fingerprint = ((Get-FileHash package.json).Hash, (Get-FileHash package-lock.json).Hash,
            (& node --version)) -join ':'
        $stamp = Join-Path (Get-Location) 'node_modules/.lightoverleaf-dependencies'
        if (!(Test-Path $stamp) -or !(Test-Path 'node_modules/electron/dist/electron.exe') -or
            [IO.File]::ReadAllText($stamp) -ne $fingerprint) {
            & npm.cmd ci --no-audit --no-fund
            if ($LASTEXITCODE -ne 0) { throw 'Electron dependency installation failed.' }
            [IO.File]::WriteAllText($stamp, $fingerprint)
        }
        # Electron 44 installs its runtime explicitly; npm ci only installs the package.
        & node node_modules/electron/install.js
        if ($LASTEXITCODE -ne 0) { throw 'Electron runtime download failed.' }
        & npm.cmd run build
        if ($LASTEXITCODE -ne 0) { throw 'Electron TypeScript build failed.' }
    } finally { Pop-Location }
    $runtime = Join-Path $repo 'out/electron-dev/bin/Release'
    $electron = Join-Path $repo 'frontend/electron/node_modules/electron/dist'
    Get-ChildItem -LiteralPath $electron | Where-Object Name -ne 'electron.exe' |
        Copy-Item -Destination $runtime -Recurse -Force
    Copy-Item -LiteralPath (Join-Path $electron 'electron.exe') -Destination (Join-Path $runtime 'LightOverLeaf.exe') -Force
    $application = Join-Path $runtime 'resources/app'
    New-Item -ItemType Directory -Force $application | Out-Null
    Copy-Item -Path (Join-Path $repo 'out/electron-dev/shell/*') -Destination $application -Force
    [IO.File]::WriteAllText((Join-Path $application 'package.json'),
        '{"name":"lightoverleaf","version":"0.1.0","main":"main.js"}', [Text.UTF8Encoding]::new($false))
    $webTarget = Join-Path $application 'web'
    New-Item -ItemType Directory -Force $webTarget | Out-Null
    Copy-Item -Path (Join-Path $repo 'frontend/web/dist/*') -Destination $webTarget -Recurse -Force
    if (!$MiKTeXRoot) {
        $dependencyLine = Get-Content (Join-Path $repo 'out/electron-dev/CMakeCache.txt') |
            Where-Object { $_ -match '^LIGHTOVERLEAF_THIRDPARTY_ROOT:PATH=' } | Select-Object -First 1
        if (!$dependencyLine) { throw 'Cannot resolve native dependency root.' }
        $MiKTeXRoot = Join-Path ($dependencyLine -replace '^[^=]+=', '') 'miktex'
    }
    & (Join-Path $PSScriptRoot '../runtime/ensure-dev-runtime.ps1') -Source $MiKTeXRoot -Destination (Join-Path $runtime 'runtime/miktex') -Refresh:$RefreshRuntime
    foreach ($file in @('LightOverLeaf.exe', 'LightOverLeafBackend.exe', 'sqlite3.dll',
        'resources/app/main.js', 'resources/app/preload.js', 'resources/app/web/index.html')) {
        if (!(Test-Path -LiteralPath (Join-Path $runtime $file) -PathType Leaf)) { throw "Missing build output: $file" }
    }
    Write-Host 'BUILD SUCCEEDED - Electron + C++ backend:' -ForegroundColor Green
    Write-Host (Join-Path $runtime 'LightOverLeaf.exe') -ForegroundColor Cyan
} finally { Pop-Location }
