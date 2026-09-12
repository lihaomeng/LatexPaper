#requires -Version 5.1
param(
    [ValidateSet('Lite', 'Full')][string]$Mode = 'Lite',
    [string]$PortableTexRoot = '',
    [switch]$SkipBuild,
    [string]$SevenZip = 'C:/Program Files/7-Zip/7z.exe'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))

function Run([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed: $LASTEXITCODE" }
}
function RequireFile([string]$Path) {
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Missing required file: $Path" }
}

Push-Location $repo
try {
    RequireFile $SevenZip
    $sevenZipDirectory = Split-Path -Parent $SevenZip
    $sevenZipDll = Join-Path $sevenZipDirectory '7z.dll'
    RequireFile $sevenZipDll
    if (!$SkipBuild) {
        Run 'cmake' @('--preset', 'desktop-release')
        Run 'cmake' @('--build', '--preset', 'desktop-release')
    }
    Run 'ctest' @('--preset', 'desktop-release', '--output-on-failure')
    $source = Join-Path $repo 'out/desktop-release/bin/Release'
    foreach ($file in @(
        'LightOverLeaf.exe', 'LightOverLeaf.dll', 'libcef.dll', 'Qt5Core.dll',
        'Qt5Gui.dll', 'Qt5Widgets.dll', 'icudtl.dat', 'resources.pak',
        'v8_context_snapshot.bin', 'platforms/qwindows.dll', 'web/index.html',
        'locales/en-US.pak', 'licenses/CEF.txt'
    )) { RequireFile (Join-Path $source $file) }
    if ($Mode -eq 'Full') {
        if ([string]::IsNullOrWhiteSpace($PortableTexRoot)) {
            throw 'Full package requires -PortableTexRoot. No TeX payload is downloaded automatically.'
        }
        $PortableTexRoot = [IO.Path]::GetFullPath($PortableTexRoot)
        RequireFile (Join-Path $PortableTexRoot 'bin/windows/xelatex.exe')
    }
    $runRoot = Join-Path $repo ('out/packages/' + $Mode.ToLowerInvariant() + '-' +
        (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
    $payload = Join-Path $runRoot 'payload'
    $outer = Join-Path $runRoot 'outer'
    New-Item -ItemType Directory -Path $payload, $outer -Force | Out-Null
    Copy-Item -Path (Join-Path $source '*') -Destination $payload -Recurse

    $licenses = Join-Path $payload 'licenses'
    New-Item -ItemType Directory -Path $licenses -Force | Out-Null
    foreach ($package in @('react', 'react-dom', 'scheduler', 'monaco-editor', 'pdfjs-dist')) {
        $license = Join-Path $repo "web/node_modules/$package/LICENSE"
        RequireFile $license
        Copy-Item -LiteralPath $license -Destination (Join-Path $licenses "$package.txt")
    }
    $sqliteLicense = Join-Path $repo '..\QTBest\thirdparty_install\vcpkg\installed\x64-windows\share\sqlite3\copyright'
    RequireFile $sqliteLicense
    Copy-Item -LiteralPath $sqliteLicense -Destination (Join-Path $licenses 'sqlite3.txt')
    Copy-Item -LiteralPath (Join-Path $sevenZipDirectory 'License.txt') -Destination (Join-Path $licenses '7-Zip.txt')
    if ($Mode -eq 'Full') {
        Copy-Item -LiteralPath $PortableTexRoot -Destination (Join-Path $payload 'texlive') -Recurse
    }
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'package-readme.txt') -Destination (Join-Path $payload 'README.txt')
    $manifest = @(Get-ChildItem -LiteralPath $payload -Recurse -File | ForEach-Object {
        [ordered]@{
            path = $_.FullName.Substring($payload.Length + 1)
            bytes = $_.Length
            sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
        }
    })
    $manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $payload 'manifest.json') -Encoding UTF8
    $innerArchive = Join-Path $outer 'payload.7z'
    Push-Location $payload
    try { Run $SevenZip @('a', '-t7z', '-mx3', '-mmt2', '-sse', $innerArchive, '.\*') }
    finally { Pop-Location }
    Run $SevenZip @('t', $innerArchive)
    Copy-Item -LiteralPath $SevenZip, $sevenZipDll -Destination $outer
    $runner = @'
@echo off
setlocal
set "LOL_RUNTIME=%TEMP%\LightOverLeaf-%RANDOM%-%RANDOM%"
mkdir "%LOL_RUNTIME%" >nul 2>nul
"%~dp07z.exe" x "%~dp0payload.7z" "-o%LOL_RUNTIME%" -y >nul
if errorlevel 1 goto :failed
if "%LIGHTOVERLEAF_PACKAGE_SMOKE%"=="1" (
  start "" /wait "%LOL_RUNTIME%\LightOverLeaf.exe" --smoke-test
) else (
  start "" /wait "%LOL_RUNTIME%\LightOverLeaf.exe"
)
set "LOL_EXIT=%ERRORLEVEL%"
rmdir /s /q "%LOL_RUNTIME%" >nul 2>nul
exit /b %LOL_EXIT%
:failed
set "LOL_EXIT=%ERRORLEVEL%"
rmdir /s /q "%LOL_RUNTIME%" >nul 2>nul
exit /b %LOL_EXIT%
'@
    Set-Content -LiteralPath (Join-Path $outer 'run.cmd') -Value $runner -Encoding ASCII
    $target = Join-Path $runRoot ("LightOverLeaf-0.1.0-$($Mode.ToLowerInvariant())-win64.exe")
    $sed = Join-Path $runRoot 'package.sed'
    $sedLines = @(
        '[Version]', 'Class=IEXPRESS', 'SEDVersion=3', '[Options]',
        'PackagePurpose=InstallApp', 'ShowInstallProgramWindow=0', 'HideExtractAnimation=1',
        'UseLongFileName=1', 'InsideCompressed=0', 'CAB_FixedSize=0', 'CAB_ResvCodeSigning=0',
        'RebootMode=N', 'InstallPrompt=', 'DisplayLicense=', 'FinishMessage=',
        "TargetName=$target", 'FriendlyName=LightOverLeaf', 'AppLaunched=run.cmd',
        'PostInstallCmd=<None>', 'AdminQuietInstCmd=', 'UserQuietInstCmd=',
        'SourceFiles=SourceFiles', '[SourceFiles]', "SourceFiles0=$outer\", '[SourceFiles0]',
        '%FILE0%=', '%FILE1%=', '%FILE2%=', '%FILE3%=', '[Strings]',
        'FILE0="payload.7z"', 'FILE1="7z.exe"', 'FILE2="7z.dll"', 'FILE3="run.cmd"'
    )
    $sedLines | Set-Content -LiteralPath $sed -Encoding Unicode
    $iexpress = Start-Process -FilePath "$env:WINDIR\System32\iexpress.exe" -ArgumentList @('/N', '/Q', $sed) -Wait -PassThru -WindowStyle Hidden
    if ($iexpress.ExitCode -ne 0) { throw "IExpress failed: $($iexpress.ExitCode)" }
    RequireFile $target
    $hash = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
    "$hash  $([IO.Path]::GetFileName($target))" | Set-Content -LiteralPath "$target.sha256" -Encoding ASCII
    [ordered]@{
        mode = $Mode
        executable = $target
        bytes = (Get-Item -LiteralPath $target).Length
        sha256 = $hash
        runtimeFiles = $manifest.Count
        portableTexIncluded = ($Mode -eq 'Full')
        packaging = 'IExpress launcher containing a directory-preserving 7z payload'
        signed = $false
    } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runRoot 'package-report.json') -Encoding UTF8
    Write-Host "Runnable single EXE ready: $target"
} finally {
    Pop-Location
}
