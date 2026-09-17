#requires -Version 5.1
param(
    [string]$MiKTeXRoot,
    [switch]$SkipBuild,
    [switch]$LegacyCef,
    [ValidateRange(30, 600)][int]$SmokeTimeoutSeconds = 120
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($LegacyCef) {
    $legacyArguments = @{ SkipBuild = $SkipBuild; SmokeTimeoutSeconds = $SmokeTimeoutSeconds }
    if ($MiKTeXRoot) { $legacyArguments.MiKTeXRoot = $MiKTeXRoot }
    & (Join-Path $PSScriptRoot 'legacy/package-cef.ps1') @legacyArguments
    return
}
if ($SkipBuild -and $MiKTeXRoot) { throw 'Use MiKTeXRoot during build; SkipBuild packages the already deployed runtime.' }
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
function Require-File([string]$Path) {
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Missing required file: $Path" }
}
if (!$SkipBuild) {
    & (Join-Path $PSScriptRoot 'build.ps1') --dev -MiKTeXRoot $MiKTeXRoot
}
$source = Join-Path $repo 'out/electron-dev/bin/Release'
foreach ($file in @('LightOverLeaf.exe', 'LightOverLeafBackend.exe', 'sqlite3.dll',
    'resources/app/package.json', 'resources/app/main.js', 'resources/app/preload.js',
    'resources/app/web/index.html', 'icudtl.dat', 'resources.pak',
    'LICENSE', 'LICENSES.chromium.html', 'runtime/miktex/lightoverleaf-miktex-runtime.json')) {
    Require-File (Join-Path $source $file)
}
$cache = Join-Path $repo 'out/electron-dev/CMakeCache.txt'
Require-File $cache
$dependencyLine = Get-Content -LiteralPath $cache |
    Where-Object { $_ -match '^LIGHTOVERLEAF_THIRDPARTY_ROOT:PATH=' } | Select-Object -First 1
if (!$dependencyLine) { throw 'Cannot resolve third-party license root.' }
$sqliteLicense = Join-Path ($dependencyLine -replace '^[^=]+=', '') 'vcpkg/installed/x64-windows/share/sqlite3/copyright'
Require-File $sqliteLicense
$licenseSources = @{}
foreach ($package in @('react', 'react-dom', 'scheduler', 'monaco-editor', 'pdfjs-dist')) {
    $license = Join-Path $repo "frontend/web/node_modules/$package/LICENSE"
    Require-File $license
    $licenseSources[$package] = $license
}
$runRoot = Join-Path $repo ('out/distributions/electron-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
$distribution = Join-Path $runRoot 'LightOverLeaf-Full-win64'
New-Item -ItemType Directory -Path $distribution -Force | Out-Null
Get-ChildItem -LiteralPath $source -Force | Copy-Item -Destination $distribution -Recurse
$licenses = Join-Path $distribution 'licenses'
New-Item -ItemType Directory -Path $licenses -Force | Out-Null
foreach ($package in $licenseSources.Keys) {
    Copy-Item -LiteralPath $licenseSources[$package] -Destination (Join-Path $licenses "$package.txt")
}
Copy-Item -LiteralPath $sqliteLicense -Destination (Join-Path $licenses 'sqlite3.txt')
Copy-Item -LiteralPath (Join-Path $source 'LICENSE') -Destination (Join-Path $licenses 'Electron.txt')
Copy-Item -LiteralPath (Join-Path $source 'LICENSES.chromium.html') -Destination $licenses
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '../support/resources/packaging/README.txt') -Destination (Join-Path $distribution 'README.txt')
$entries = @(Get-ChildItem -LiteralPath $distribution -Recurse -File | Sort-Object FullName | ForEach-Object {
    [pscustomobject][ordered]@{
        path = $_.FullName.Substring($distribution.Length + 1).Replace('\', '/')
        bytes = $_.Length
        sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
    }
})
[ordered]@{
    schemaVersion = 1; product = 'LightOverLeaf'; shell = 'Electron'; edition = 'Full'
    buildPreset = 'electron-dev'; delivery = 'expanded-green-directory'; runtimeExtraction = $false
    entrypoint = 'LightOverLeaf.exe'; fileCount = $entries.Count; files = $entries
} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $distribution 'runtime-manifest.json') -Encoding UTF8
[ordered]@{
    directory = $distribution; shell = 'Electron'; signed = $false
    validation = 'required-files-and-sha256'; runtimeSmoke = 'not-run'; tests = 'not-run'
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runRoot 'package-report.json') -Encoding UTF8
Write-Host "Electron development distribution ready: $distribution"
Write-Host 'Runtime startup and offline clean-machine validation remain manual acceptance steps.'
