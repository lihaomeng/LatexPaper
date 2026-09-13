#requires -Version 5.1
param(
    [ValidateSet('Lite', 'Full')][string]$Mode = 'Lite',
    [string]$PortableTexRoot = '',
    [string]$MiKTeXRoot = '',
    [switch]$SkipBuild,
    [ValidateRange(60, 1800)][int]$SmokeTimeoutSeconds = 600,
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
function StopProcessTree([Diagnostics.Process]$Process) {
    if ($null -eq $Process -or $Process.HasExited) { return }
    try {
        $taskkill = Join-Path $env:WINDIR 'System32/taskkill.exe'
        $result = Start-Process -FilePath $taskkill `
            -ArgumentList @('/PID', "$($Process.Id)", '/T', '/F') `
            -Wait -PassThru -WindowStyle Hidden
        if ($result.ExitCode -ne 0 -and !$Process.HasExited) { $Process.Kill() }
    } catch {
        if (!$Process.HasExited) { $Process.Kill() }
    }
}
function RequireGuiSubsystem([string]$Path) {
    $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    $reader = [IO.BinaryReader]::new($stream)
    try {
        if ($stream.Length -lt 256 -or $reader.ReadUInt16() -ne 0x5A4D) { throw "Invalid PE image: $Path" }
        $stream.Position = 0x3C
        $peOffset = $reader.ReadInt32()
        if ($peOffset -lt 64 -or $peOffset + 94 -gt $stream.Length) { throw "Invalid PE header: $Path" }
        $stream.Position = $peOffset
        if ($reader.ReadUInt32() -ne 0x00004550) { throw "Invalid PE signature: $Path" }
        $stream.Position = $peOffset + 24 + 68
        if ($reader.ReadUInt16() -ne 2) { throw "Console subsystem is forbidden for packaged launchers: $Path" }
    } finally {
        $reader.Dispose()
        $stream.Dispose()
    }
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
        'LightOverLeafRuntimeLauncher.exe',
        'Qt5Gui.dll', 'Qt5Widgets.dll', 'icudtl.dat', 'resources.pak',
        'v8_context_snapshot.bin', 'platforms/qwindows.dll', 'web/index.html',
        'locales/en-US.pak', 'licenses/CEF.txt'
    )) { RequireFile (Join-Path $source $file) }
    if ($Mode -eq 'Full') {
        $hasPortableTex = ![string]::IsNullOrWhiteSpace($PortableTexRoot)
        $hasMiKTeX = ![string]::IsNullOrWhiteSpace($MiKTeXRoot)
        if ($hasPortableTex -eq $hasMiKTeX) {
            throw 'Full package requires exactly one TeX payload: -PortableTexRoot or -MiKTeXRoot.'
        }
        if ($hasPortableTex) {
            $PortableTexRoot = [IO.Path]::GetFullPath($PortableTexRoot)
            RequireFile (Join-Path $PortableTexRoot 'bin/windows/xelatex.exe')
            $texRuntimeKind = 'TeXLive'
        } else {
            $MiKTeXRoot = [IO.Path]::GetFullPath($MiKTeXRoot)
            $miKTeXBin = Join-Path $MiKTeXRoot 'texmfs/install/miktex/bin/x64'
            if (!(Test-Path -LiteralPath (Join-Path $miKTeXBin 'xelatex.exe') -PathType Leaf)) {
                $miKTeXBin = Join-Path $MiKTeXRoot 'miktex/bin/x64'
            }
            RequireFile (Join-Path $miKTeXBin 'xelatex.exe')
            RequireFile (Join-Path $miKTeXBin 'pdflatex.exe')
            RequireFile (Join-Path $miKTeXBin 'synctex.exe')
            $texRuntimeKind = 'MiKTeX'
        }
    } else {
        $texRuntimeKind = 'None'
    }
    $runRoot = Join-Path $repo ('out/packages/' + $Mode.ToLowerInvariant() + '-' +
        (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
    $payload = Join-Path $runRoot 'payload'
    $outer = Join-Path $runRoot 'outer'
    New-Item -ItemType Directory -Path $payload, $outer -Force | Out-Null
    Copy-Item -Path (Join-Path $source '*') -Destination $payload -Recurse
    $launcher = Join-Path $source 'LightOverLeafRuntimeLauncher.exe'
    Copy-Item -LiteralPath $launcher -Destination $outer
    Remove-Item -LiteralPath (Join-Path $payload 'LightOverLeafRuntimeLauncher.exe') -Force
    RequireGuiSubsystem $launcher

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
        if ($texRuntimeKind -eq 'TeXLive') {
            Copy-Item -LiteralPath $PortableTexRoot -Destination (Join-Path $payload 'texlive') -Recurse
        } else {
            $runtimeRoot = Join-Path $payload 'runtime'
            New-Item -ItemType Directory -Path $runtimeRoot -Force | Out-Null
            Copy-Item -LiteralPath $MiKTeXRoot -Destination (Join-Path $runtimeRoot 'miktex') -Recurse
        }
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
    $target = Join-Path $runRoot ("LightOverLeaf-0.1.0-$($Mode.ToLowerInvariant())-win64.exe")
    $sed = Join-Path $runRoot 'package.sed'
    $sedLines = @(
        '[Version]', 'Class=IEXPRESS', 'SEDVersion=3', '[Options]',
        'PackagePurpose=InstallApp', 'ShowInstallProgramWindow=0', 'HideExtractAnimation=1',
        'UseLongFileName=1', 'InsideCompressed=0', 'CAB_FixedSize=0', 'CAB_ResvCodeSigning=0',
        'RebootMode=N', 'InstallPrompt=', 'DisplayLicense=', 'FinishMessage=',
        "TargetName=$target", 'FriendlyName=LightOverLeaf', 'AppLaunched=LightOverLeafRuntimeLauncher.exe',
        'PostInstallCmd=<None>', 'AdminQuietInstCmd=', 'UserQuietInstCmd=',
        'SourceFiles=SourceFiles', '[SourceFiles]', "SourceFiles0=$outer\", '[SourceFiles0]',
        '%FILE0%=', '%FILE1%=', '%FILE2%=', '%FILE3%=', '[Strings]',
        'FILE0="payload.7z"', 'FILE1="7z.exe"', 'FILE2="7z.dll"', 'FILE3="LightOverLeafRuntimeLauncher.exe"'
    )
    $sedLines | Set-Content -LiteralPath $sed -Encoding Unicode
    $iexpress = Start-Process -FilePath "$env:WINDIR\System32\iexpress.exe" -ArgumentList @('/N', '/Q', $sed) -Wait -PassThru -WindowStyle Hidden
    if ($iexpress.ExitCode -ne 0) { throw "IExpress failed: $($iexpress.ExitCode)" }
    RequireFile $target
    RequireGuiSubsystem $target
    $savedSmoke = $env:LIGHTOVERLEAF_PACKAGE_SMOKE
    $smokeProcess = $null
    $smokeTimer = [Diagnostics.Stopwatch]::StartNew()
    try {
        $env:LIGHTOVERLEAF_PACKAGE_SMOKE = '1'
        $smokeProcess = Start-Process -FilePath $target -WorkingDirectory $runRoot -WindowStyle Hidden -PassThru
        if (!$smokeProcess.WaitForExit($SmokeTimeoutSeconds * 1000)) {
            StopProcessTree $smokeProcess
            throw "Packaged application smoke timed out after $SmokeTimeoutSeconds seconds while extracting and starting the payload."
        }
        if ($smokeProcess.ExitCode -ne 0) { throw "Packaged application smoke failed: $($smokeProcess.ExitCode)" }
    } finally {
        $smokeTimer.Stop()
        $env:LIGHTOVERLEAF_PACKAGE_SMOKE = $savedSmoke
        if ($smokeProcess) { $smokeProcess.Dispose() }
    }
    $hash = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
    "$hash  $([IO.Path]::GetFileName($target))" | Set-Content -LiteralPath "$target.sha256" -Encoding ASCII
    [ordered]@{
        mode = $Mode
        executable = $target
        bytes = (Get-Item -LiteralPath $target).Length
        sha256 = $hash
        runtimeFiles = $manifest.Count
        portableTexIncluded = ($Mode -eq 'Full')
        texRuntimeKind = $texRuntimeKind
        texRuntimeSource = if ($texRuntimeKind -eq 'MiKTeX') { 'source-built' } elseif ($texRuntimeKind -eq 'TeXLive') { 'external-portable' } else { 'system-or-configured' }
        packaging = 'IExpress GUI container with a native GUI launcher and directory-preserving 7z payload'
        consoleLauncher = $false
        packagedSmoke = 'passed'
        packagedSmokeTimeoutSeconds = $SmokeTimeoutSeconds
        packagedSmokeElapsedMilliseconds = $smokeTimer.ElapsedMilliseconds
        signed = $false
    } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runRoot 'package-report.json') -Encoding UTF8
    Write-Host "Runnable single EXE ready: $target"
} finally {
    Pop-Location
}
