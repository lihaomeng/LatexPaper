#requires -Version 5.1
param([switch]$SkipBuild, [string]$SevenZip = 'C:/Program Files/7-Zip/7z.exe')
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
Set-StrictMode -Version Latest
function Run([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed: $LASTEXITCODE" }
}
function RequireFile([string]$Path) {
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Missing: $Path" }
}
function Cache([string]$Key) {
    $lines = @(Get-Content (Join-Path $repo 'out/desktop-release/CMakeCache.txt') |
        Where-Object { $_ -match ('^' + [regex]::Escape($Key) + ':[^=]+=') })
    if ($lines.Count -ne 1) { throw "Missing cache key: $Key" }
    return $lines[0].Substring($lines[0].IndexOf('=') + 1)
}
Push-Location $repo
try {
    RequireFile $SevenZip
    $sfx = Join-Path (Split-Path $SevenZip) '7z.sfx'
    RequireFile $sfx
    if (!$SkipBuild) {
        Run 'cmake' @('--preset','desktop-release')
        Run 'cmake' @('--build','--preset','desktop-release')
    }
    Run 'ctest' @('--preset','desktop-release')
    $source = Join-Path $repo 'out/desktop-release/bin/Release'
    foreach ($file in @('LightOverLeaf.exe','LightOverLeaf.dll','libcef.dll','Qt5Core.dll','Qt5Gui.dll',
        'Qt5Widgets.dll','icudtl.dat','resources.pak','v8_context_snapshot.bin',
        'platforms/qwindows.dll','web/index.html','locales/en-US.pak','licenses/CEF.txt')) {
        RequireFile (Join-Path $source $file)
    }
    # Unique directory: never delete or overwrite earlier packages.
    $runRoot = Join-Path $repo ('out/packages/' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0,8))
    $stage = Join-Path $runRoot 'stage'
    $payload = Join-Path $stage 'LightOverLeaf'
    New-Item -ItemType Directory -Path $payload | Out-Null
    foreach ($item in Get-ChildItem -LiteralPath $source -File) {
        if ($item.Extension -in @('.dll','.pak','.dat','.bin') -or $item.Name -in @('LightOverLeaf.exe','vk_swiftshader_icd.json')) {
            Copy-Item -LiteralPath $item.FullName -Destination $payload
        }
    }
    foreach ($directory in @('platforms','locales','licenses','web')) {
        Copy-Item -LiteralPath (Join-Path $source $directory) -Destination $payload -Recurse
    }
    $share = Split-Path (Split-Path (Cache 'Qt5Core_DIR'))
    foreach ($port in @('qt5-base','brotli','bzip2','double-conversion','freetype','harfbuzz','libpng','pcre2','zlib','zstd')) {
        $license = Join-Path $share "$port/copyright"
        RequireFile $license
        Copy-Item -LiteralPath $license -Destination (Join-Path $payload "licenses/$port.txt")
    }
    foreach ($package in @('react','react-dom','scheduler','monaco-editor','pdfjs-dist')) {
        $license = Join-Path $repo "web/node_modules/$package/LICENSE"
        RequireFile $license
        Copy-Item -LiteralPath $license -Destination (Join-Path $payload "licenses/$package.txt")
    }
    Copy-Item -LiteralPath (Join-Path (Split-Path $SevenZip) 'License.txt') -Destination (Join-Path $payload 'licenses/7-Zip.txt')
    $vcRoot = (Cache 'CMAKE_LINKER') -replace '/Tools/MSVC/.*$', ''
    $versions = @(Get-ChildItem -LiteralPath (Join-Path $vcRoot 'Redist/MSVC') -Directory |
        Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' } | Sort-Object { [version]$_.Name } -Descending)
    $crtPath = ''
    foreach ($version in $versions) {
        $crt = @(Get-ChildItem -Path (Join-Path $version.FullName 'x64/Microsoft.VC*.CRT') -Directory -ErrorAction SilentlyContinue)
        if ($crt.Count -eq 1) { $crtPath = $crt[0].FullName; break }
    }
    RequireFile (Join-Path $crtPath 'msvcp140.dll')
    RequireFile (Join-Path $crtPath 'vcruntime140.dll')
    Get-ChildItem -LiteralPath $crtPath -Filter '*.dll' -File | Copy-Item -Destination $payload
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'package-readme.txt') -Destination (Join-Path $payload 'README.txt')
    $manifest = @(Get-ChildItem -LiteralPath $payload -Recurse -File | ForEach-Object {
        [ordered]@{ path = $_.FullName.Substring($payload.Length + 1); bytes = $_.Length;
            sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
    })
    $manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $payload 'manifest.json') -Encoding UTF8
    $archive = Join-Path $runRoot 'LightOverLeaf-0.1.0-dev-win64.exe'
    Push-Location $stage
    try { Run $SevenZip @('a','-t7z',"-sfx$sfx",'-mx3','-mmt2','-sse',$archive,'LightOverLeaf') }
    finally { Pop-Location }
    Run $SevenZip @('t',$archive)
    $verify = Join-Path $runRoot 'verify'
    Run $SevenZip @('x',$archive,"-o$verify",'-y')
    $verifiedPayload = Join-Path $verify 'LightOverLeaf'
    foreach ($entry in $manifest) {
        $file = Join-Path $verifiedPayload $entry.path
        RequireFile $file
        if ((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne $entry.sha256) { throw "Hash mismatch: $($entry.path)" }
    }
    $savedPath = $env:PATH
    $savedPluginPath = $env:QT_QPA_PLATFORM_PLUGIN_PATH
    $savedQtPath = $env:QT_PLUGIN_PATH
    $process = $null
    try {
        $env:PATH = "$env:SystemRoot/System32;$env:SystemRoot"
        $env:QT_QPA_PLATFORM_PLUGIN_PATH = Join-Path $verifiedPayload 'platforms'
        $env:QT_PLUGIN_PATH = $verifiedPayload
        $process = Start-Process -FilePath (Join-Path $verifiedPayload 'LightOverLeaf.exe') -ArgumentList '--smoke-test' -WorkingDirectory $verifiedPayload -WindowStyle Hidden -PassThru
        if (!$process.WaitForExit(45000)) {
            $process.Kill()
            throw 'Extracted application timed out. Inspect remaining application subprocesses.'
        }
        if ($process.ExitCode -ne 0) { throw "Extracted application failed: $($process.ExitCode)" }
    } finally {
        $env:PATH = $savedPath
        $env:QT_QPA_PLATFORM_PLUGIN_PATH = $savedPluginPath
        $env:QT_PLUGIN_PATH = $savedQtPath
        if ($process) { $process.Dispose() }
    }
    $hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
    "$hash  $([IO.Path]::GetFileName($archive))" | Set-Content -LiteralPath "$archive.sha256" -Encoding ASCII
    [ordered]@{ package = $archive; sha256 = $hash; skipBuild = [bool]$SkipBuild; runtimeFiles = $manifest.Count;
        archiveTest = 'passed'; extractionHashes = 'passed'; extractedSmoke = 'passed'; signed = $false;
        scope = 'development self-extracting archive; not M8 release acceptance' } |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runRoot 'package-report.json') -Encoding UTF8
    Write-Host "Package ready: $archive"
} finally { Pop-Location }
