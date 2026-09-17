#requires -Version 5.1
[CmdletBinding()]
param(
    [string]$Version = '26.5',
    [string]$SourceSha256 = 'a1fd2223ef04a0268b0718536f7b55a04b5c9d1f0f721d1c0b2749b3af95115d',
    [string]$DependencyRoot = '',
    [string]$InstallRoot = '',
    [string]$WorkRoot = '',
    [ValidateSet('none', 'essential', 'basic', 'complete')][string]$PackageSet = 'basic',
    [ValidateSet('Visual Studio 16 2019', 'Visual Studio 17 2022')][string]$Generator = 'Visual Studio 16 2019',
    [ValidateRange(1, 32)][int]$Parallel = 4,
    [switch]$EnableCairoDirectWrite,
    [switch]$SkipDownload,
    [switch]$SkipCompile,
    [switch]$ConfigureOnly,
    [switch]$SkipPackageProvision
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if ([string]::IsNullOrWhiteSpace($DependencyRoot)) {
    $DependencyRoot = if (![string]::IsNullOrWhiteSpace($env:LIGHTOVERLEAF_THIRDPARTY_ROOT)) {
        $env:LIGHTOVERLEAF_THIRDPARTY_ROOT
    } else {
        'D:/CodeMyself/QTBest/thirdparty_install'
    }
}
$DependencyRoot = [IO.Path]::GetFullPath($DependencyRoot)
if ([string]::IsNullOrWhiteSpace($InstallRoot)) { $InstallRoot = Join-Path $DependencyRoot 'miktex' }
if ([string]::IsNullOrWhiteSpace($WorkRoot)) { $WorkRoot = Join-Path $repo 'out/dependencies/miktex' }
$InstallRoot = [IO.Path]::GetFullPath($InstallRoot)
$WorkRoot = [IO.Path]::GetFullPath($WorkRoot)
$archive = Join-Path $WorkRoot "miktex-$Version.tar.xz"
$sourceRoot = Join-Path $WorkRoot "miktex-$Version"
$buildRoot = Join-Path $WorkRoot 'build'
$sourceInstallRoot = Join-Path $WorkRoot 'source-install'
$repositoryRoot = Join-Path $WorkRoot "repository-$PackageSet"
$buildToolRoot = Join-Path $WorkRoot 'tools/msys2'
$parserToolRoot = Join-Path $WorkRoot 'tools/winflexbison'
$temporaryRoot = Join-Path $repo 'out/tmp/miktex-build'
$sourceUrl = "https://miktex.org/download/ctan/systems/win32/miktex/source/miktex-$Version.tar.xz"
$vcpkg = Join-Path $DependencyRoot 'vcpkg/vcpkg.exe'
$toolchain = Join-Path $DependencyRoot 'vcpkg/scripts/buildsystems/vcpkg.cmake'

function Require-File([string]$Path, [string]$Hint = '') {
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) {
        $suffix = if ([string]::IsNullOrWhiteSpace($Hint)) { '' } else { " $Hint" }
        throw "Missing required file: $Path.$suffix"
    }
}
function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    Write-Host "> $Program $($Arguments -join ' ')"
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}
function Find-Tool([string]$Name, [string[]]$Candidates) {
    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    foreach ($candidate in $Candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { return [IO.Path]::GetFullPath($candidate) }
    }
    return ''
}

Require-File $vcpkg 'Install the declared vcpkg dependencies before configuring MiKTeX.'
Require-File $toolchain
New-Item -ItemType Directory -Path $WorkRoot, $temporaryRoot -Force | Out-Null
$env:TEMP = $temporaryRoot
$env:TMP = $temporaryRoot

$git = (Get-Command git -ErrorAction Stop).Source
$gitRoot = Split-Path -Parent (Split-Path -Parent $git)
$gitUnixBin = Join-Path $gitRoot 'usr/bin'
$perl = Find-Tool 'perl.exe' @((Join-Path $gitUnixBin 'perl.exe'))
$windowsSdkCandidates = @()
$windowsKitsRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10/bin'
if (Test-Path -LiteralPath $windowsKitsRoot -PathType Container) {
    $windowsSdkCandidates = @(Get-ChildItem -LiteralPath $windowsKitsRoot -Directory |
        Sort-Object Name -Descending | ForEach-Object { Join-Path $_.FullName 'x64/mc.exe' })
}
$messageCompiler = Find-Tool 'mc.exe' $windowsSdkCandidates
$xsltproc = Find-Tool 'xsltproc.exe' @(
    (Join-Path $DependencyRoot 'vcpkg/installed/x64-windows/tools/libxslt/xsltproc.exe'),
    (Join-Path $buildToolRoot 'usr/bin/xsltproc.exe'),
    (Join-Path $gitUnixBin 'xsltproc.exe'))
if ([string]::IsNullOrWhiteSpace($perl)) {
    throw 'perl.exe is required. Git for Windows normally supplies it in usr/bin.'
}
if ([string]::IsNullOrWhiteSpace($messageCompiler)) {
    throw 'mc.exe is required. Install the Windows 10/11 SDK x64 tools.'
}
if ([string]::IsNullOrWhiteSpace($xsltproc)) {
    if ($SkipDownload) {
        throw 'xsltproc.exe is required and the pinned MSYS2 build-tool packages are absent.'
    }
    New-Item -ItemType Directory -Path $buildToolRoot -Force | Out-Null
    $toolPackages = @(
        @{
            Name = 'libxml2-2.15.3-1-x86_64.pkg.tar.zst'
            Url = 'https://mirror.msys2.org/msys/x86_64/libxml2-2.15.3-1-x86_64.pkg.tar.zst'
            Sha256 = '9d7158e6c41c93806435b2c170785da8067e675c010083805ba1de3260535b9a'
        },
        @{
            Name = 'libxslt-1.1.45-1-x86_64.pkg.tar.zst'
            Url = 'https://mirror.msys2.org/msys/x86_64/libxslt-1.1.45-1-x86_64.pkg.tar.zst'
            Sha256 = '6006ac779da13082b2cb18a6070780f3530649c9ad9143b4739cb440a6c42e45'
        }
    )
    foreach ($package in $toolPackages) {
        $packagePath = Join-Path $WorkRoot $package.Name
        if (!(Test-Path -LiteralPath $packagePath -PathType Leaf)) {
            Invoke-WebRequest -Uri $package.Url -OutFile $packagePath -UseBasicParsing
        }
        $packageHash = (Get-FileHash -LiteralPath $packagePath -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($packageHash -ne $package.Sha256) {
            throw "Build-tool SHA256 mismatch for $($package.Name)."
        }
        Invoke-Checked 'tar.exe' @('-xf', $packagePath, '-C', $buildToolRoot)
    }
    $xsltproc = Find-Tool 'xsltproc.exe' @((Join-Path $buildToolRoot 'usr/bin/xsltproc.exe'))
    if ([string]::IsNullOrWhiteSpace($xsltproc)) {
        throw 'Pinned MSYS2 packages did not produce xsltproc.exe.'
    }
}
$bison = Find-Tool 'bison.exe' @((Join-Path $parserToolRoot 'bison.exe'))
$flex = Find-Tool 'flex.exe' @((Join-Path $parserToolRoot 'flex.exe'))
if ([string]::IsNullOrWhiteSpace($bison) -or [string]::IsNullOrWhiteSpace($flex)) {
    if ($SkipDownload) {
        throw 'bison.exe and flex.exe are required and the pinned WinFlexBison tool is absent.'
    }
    $parserArchive = Join-Path $WorkRoot 'win_flex_bison-2.5.24.zip'
    $parserUrl = 'https://github.com/lexxmark/winflexbison/releases/download/v2.5.24/win_flex_bison-2.5.24.zip'
    $parserHash = '39c6086ce211d5415500acc5ed2d8939861ca1696aee48909c7f6daf5122b505'
    if (!(Test-Path -LiteralPath $parserArchive -PathType Leaf)) {
        Invoke-WebRequest -Uri $parserUrl -OutFile $parserArchive -UseBasicParsing
    }
    $actualParserHash = (Get-FileHash -LiteralPath $parserArchive -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualParserHash -ne $parserHash) { throw 'WinFlexBison SHA256 mismatch.' }
    New-Item -ItemType Directory -Path $parserToolRoot -Force | Out-Null
    Expand-Archive -LiteralPath $parserArchive -DestinationPath $parserToolRoot -Force
    Copy-Item -LiteralPath (Join-Path $parserToolRoot 'win_bison.exe') `
        -Destination (Join-Path $parserToolRoot 'bison.exe') -Force
    Copy-Item -LiteralPath (Join-Path $parserToolRoot 'win_flex.exe') `
        -Destination (Join-Path $parserToolRoot 'flex.exe') -Force
    $bison = Find-Tool 'bison.exe' @((Join-Path $parserToolRoot 'bison.exe'))
    $flex = Find-Tool 'flex.exe' @((Join-Path $parserToolRoot 'flex.exe'))
}
if ([string]::IsNullOrWhiteSpace($bison) -or [string]::IsNullOrWhiteSpace($flex)) {
    throw 'Pinned WinFlexBison archive did not produce bison.exe and flex.exe.'
}
$toolDirectories = @($gitUnixBin, (Split-Path -Parent $perl),
    (Split-Path -Parent $xsltproc), (Split-Path -Parent $messageCompiler),
    (Split-Path -Parent $bison), (Split-Path -Parent $flex)) |
    Where-Object { ![string]::IsNullOrWhiteSpace($_) } | Select-Object -Unique
$env:PATH = ($toolDirectories -join ';') + ';' + $env:PATH

if (!(Test-Path -LiteralPath $archive -PathType Leaf)) {
    if ($SkipDownload) { throw "MiKTeX source archive is absent: $archive" }
    Write-Host "Downloading official MiKTeX source $Version..."
    Invoke-WebRequest -Uri $sourceUrl -OutFile $archive -UseBasicParsing
}
$actualHash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
if ($actualHash -ne $SourceSha256.ToLowerInvariant()) {
    throw "MiKTeX source SHA256 mismatch. Expected $SourceSha256, got $actualHash"
}
if (!(Test-Path -LiteralPath (Join-Path $sourceRoot 'CMakeLists.txt') -PathType Leaf)) {
    Invoke-Checked 'tar.exe' @('-xf', $archive, '-C', $WorkRoot)
}
Require-File (Join-Path $sourceRoot 'CMakeLists.txt')

function Apply-SourcePatch([string]$PatchPath, [string]$SentinelFile, [string]$SentinelText) {
    Require-File $PatchPath
    Require-File $SentinelFile
    if (Select-String -LiteralPath $SentinelFile -SimpleMatch $SentinelText -Quiet) { return }
    $patchTool = Find-Tool 'patch.exe' @((Join-Path $gitUnixBin 'patch.exe'))
    if ([string]::IsNullOrWhiteSpace($patchTool)) { throw 'patch.exe is required to apply the pinned MiKTeX compatibility patch.' }
    Push-Location $sourceRoot
    try { Invoke-Checked $patchTool @('-p1', '--forward', '--batch', '-i', $PatchPath) }
    finally { Pop-Location }
}

Apply-SourcePatch `
    (Join-Path $repo 'support/patches/miktex/26.5-windows-shlwapi-declarations.patch') `
    (Join-Path $sourceRoot 'Libraries/MiKTeX/Core/win/winRegistry.cpp') `
    '#include <Shlwapi.h>'
Apply-SourcePatch `
    (Join-Path $repo 'support/patches/miktex/26.5-cairo-configurable-dwrite.patch') `
    (Join-Path $sourceRoot 'Libraries/3rd/cairo/CMakeLists.txt') `
    'option(MIKTEX_CAIRO_DWRITE'

$configureArguments = @(
    '-S', $sourceRoot,
    '-B', $buildRoot,
    '-G', $Generator,
    '-A', 'x64',
    "-DCMAKE_INSTALL_PREFIX=$($sourceInstallRoot.Replace('\', '/'))",
    "-DCMAKE_TOOLCHAIN_FILE=$($toolchain.Replace('\', '/'))",
    "-DBISON_EXECUTABLE=$($bison.Replace('\', '/'))",
    "-DFLEX_EXECUTABLE=$($flex.Replace('\', '/'))",
    '-DVCPKG_TARGET_TRIPLET=x64-windows',
    '-DMIKTEX_SELF_CONTAINED=ON',
    '-DMIKTEX_MPM_AUTO_INSTALL=OFF',
    '-DWITH_UI_QT=OFF',
    '-DWITH_UI_MFC=OFF',
    '-DWITH_COM=OFF',
    '-DWITH_ASYMPTOTE=OFF',
    '-DUSE_SYSTEM_OPENGL=ON',
    '-DWITH_STANDALONE_SETUP=ON',
    '-DWITH_MIKTEX_DOC=OFF',
    '-DWITH_MIKTEX_API_DOC=OFF',
    '-DWITH_MPC=OFF',
    '-DWITH_BOOTSTRAPPING=ON',
    '-DWITH_PACKAGE_DB_SIGNING=OFF',
    "-DMIKTEX_CAIRO_DWRITE=$(if ($EnableCairoDirectWrite) { 'ON' } else { 'OFF' })",
    '-DWITH_SYNCTEX=ON'
)
if ($SkipCompile -and $ConfigureOnly) {
    throw '-SkipCompile and -ConfigureOnly cannot be used together.'
}
if (!$SkipCompile) {
    Invoke-Checked 'cmake.exe' $configureArguments
    if ($ConfigureOnly) {
        Write-Host "MiKTeX configure passed: $buildRoot"
        exit 0
    }

    Invoke-Checked 'cmake.exe' @('--build', $buildRoot, '--config', 'Release', '--parallel', "$Parallel")
    Invoke-Checked 'cmake.exe' @('--install', $buildRoot, '--config', 'Release')
} else {
    Write-Host "Reusing source-built MiKTeX staging tree: $sourceInstallRoot"
}

$sourceBuiltBin = Join-Path $sourceInstallRoot 'texmf/miktex/bin/x64'
$sandboxBin = Join-Path $buildRoot 'sandbox/miktex/bin/x64'
Require-File (Join-Path $sourceBuiltBin 'miktexsetup.exe')
# The source install excludes vcpkg runtime DLLs; CMake keeps those beside the
# built tools in its sandbox. Stage the complete DLL closure before setup runs.
Get-ChildItem -LiteralPath $sandboxBin -Filter '*.dll' -File | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination $sourceBuiltBin -Force
}

$miktexBin = if (!$SkipPackageProvision -and $PackageSet -ne 'none') {
    Join-Path $InstallRoot 'texmfs/install/miktex/bin/x64'
} else {
    Join-Path $InstallRoot 'miktex/bin/x64'
}
$downloader = Join-Path $sourceBuiltBin 'miktexsetup.exe'
$setup = Join-Path $sandboxBin 'miktexsetup_standalone.exe'
if (!$SkipPackageProvision -and $PackageSet -ne 'none') {
    Require-File $setup 'Build with WITH_STANDALONE_SETUP=ON to enable the install task.'
    New-Item -ItemType Directory -Path $repositoryRoot -Force | Out-Null
    Invoke-Checked $downloader @('--verbose', "--local-package-repository=$repositoryRoot",
        "--package-set=$PackageSet", 'download')
    Invoke-Checked $setup @('--verbose', "--local-package-repository=$repositoryRoot",
        "--package-set=$PackageSet", "--portable=$InstallRoot", '--use-registry=no',
        '--modify-path=no', 'install')
}

# CMake installs developer artifacts below <prefix>/texmf. The standalone
# installer creates <portable>/texmfs/install/miktex/bin/x64. Overlay the
# source-built executables and DLLs there so the packaged runtime executes
# our pinned source build while retaining the installed package aliases.
Require-File (Join-Path $sourceBuiltBin 'miktex-xetex.exe')
New-Item -ItemType Directory -Path $miktexBin -Force | Out-Null
Copy-Item -Path (Join-Path $sourceBuiltBin '*') -Destination $miktexBin -Recurse -Force

$requiredRuntimeFiles = if ($SkipPackageProvision -or $PackageSet -eq 'none') {
    @('miktex-xetex.exe', 'miktex-pdftex.exe', 'miktex-luatex.exe', 'miktex-synctex.exe')
} else {
    @('xelatex.exe', 'pdflatex.exe', 'lualatex.exe', 'synctex.exe')
}
foreach ($file in $requiredRuntimeFiles) { Require-File (Join-Path $miktexBin $file) }
$initexmf = Join-Path $miktexBin 'initexmf.exe'
if (Test-Path -LiteralPath $initexmf -PathType Leaf) {
    Invoke-Checked $initexmf @('--set-config-value=[MPM]AutoInstall=0')
}

if (!$SkipPackageProvision -and $PackageSet -ne 'none') {
    & (Join-Path $PSScriptRoot '../../scripts/runtime/prepare-chinese-runtime.ps1') -Root $InstallRoot -AllowDownload
}

$provenance = [ordered]@{
    product = 'MiKTeX'
    version = $Version
    sourceUrl = $sourceUrl
    sourceSha256 = $actualHash
    builtFromSource = $true
    packageSet = if ($SkipPackageProvision) { 'not-provisioned' } else { $PackageSet }
    autoInstall = $false
    generator = $Generator
    architecture = 'x64'
    cairoDirectWrite = [bool]$EnableCairoDirectWrite
    compatibilityPatches = @(
        '26.5-windows-shlwapi-declarations.patch',
        '26.5-cairo-configurable-dwrite.patch'
    )
    sourceInstallRoot = $sourceInstallRoot
    installRoot = $InstallRoot
    completedAt = (Get-Date).ToString('o')
}
$provenance | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $InstallRoot 'lightoverleaf-miktex-runtime.json') -Encoding UTF8
Write-Host "Source-built MiKTeX runtime ready: $InstallRoot"
