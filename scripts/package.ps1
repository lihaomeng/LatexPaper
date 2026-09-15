#requires -Version 5.1
param(
    [string]$MiKTeXRoot = 'D:/CodeMyself/QTBest/thirdparty_install/miktex',
    [switch]$SkipBuild,
    [ValidateRange(30, 600)][int]$SmokeTimeoutSeconds = 120
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))

function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed: $LASTEXITCODE" }
}
function Require-File([string]$Path) {
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Missing required file: $Path" }
}
function Require-Directory([string]$Path) {
    if (!(Test-Path -LiteralPath $Path -PathType Container)) { throw "Missing required directory: $Path" }
}
function Resolve-MiKTeXBin([string]$Root) {
    foreach ($relative in @('texmfs/install/miktex/bin/x64', 'miktex/bin/x64')) {
        $candidate = Join-Path $Root $relative
        if (Test-Path -LiteralPath (Join-Path $candidate 'pdflatex.exe') -PathType Leaf) { return $candidate }
    }
    throw "MiKTeX x64 executable directory was not found below: $Root"
}

function Require-GuiSubsystem([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    $reader = [IO.BinaryReader]::new($stream)
    try {
        if ($stream.Length -lt 256 -or $reader.ReadUInt16() -ne 0x5A4D) { throw "Invalid PE image: $Path" }
        $stream.Position = 0x3C
        $peOffset = $reader.ReadInt32()
        if ($peOffset -lt 64 -or $peOffset + 94 -gt $stream.Length) { throw "Invalid PE header: $Path" }
        $stream.Position = $peOffset
        if ($reader.ReadUInt32() -ne 0x00004550) { throw "Invalid PE signature: $Path" }
        $stream.Position = $peOffset + 24 + 68
        if ($reader.ReadUInt16() -ne 2) { throw "Console subsystem is forbidden: $Path" }
    } finally { $reader.Dispose(); $stream.Dispose() }
}

Push-Location $repo
try {
    $MiKTeXRoot = [IO.Path]::GetFullPath($MiKTeXRoot)
    Require-Directory $MiKTeXRoot
    $miKTeXBin = Resolve-MiKTeXBin $MiKTeXRoot
    foreach ($tool in @('pdflatex.exe','synctex.exe')) {
        Require-File (Join-Path $miKTeXBin $tool)
    }
    Require-File (Join-Path $MiKTeXRoot 'lightoverleaf-miktex-runtime.json')
    if (!$SkipBuild) {
        Invoke-Checked 'cmake' @('--preset', 'desktop-release')
        Invoke-Checked 'cmake' @('--build', '--preset', 'desktop-release')
    }
    Invoke-Checked 'ctest' @('--preset', 'desktop-release', '--output-on-failure')

    $source = Join-Path $repo 'out/desktop-release/bin/Release'
    foreach ($file in @('LightOverLeaf.exe', 'LightOverLeaf.dll', 'libcef.dll', 'Qt5Core.dll',
        'Qt5Gui.dll', 'Qt5Widgets.dll', 'icudtl.dat', 'resources.pak', 'v8_context_snapshot.bin',
        'platforms/qwindows.dll', 'web/index.html', 'locales/en-US.pak', 'licenses/CEF.txt')) {
        Require-File (Join-Path $source $file)
    }

    $runRoot = Join-Path $repo ('out/distributions/full-' +
        (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
    $distribution = Join-Path $runRoot 'LightOverLeaf-Full-win64'
    New-Item -ItemType Directory -Path $distribution -Force | Out-Null
    foreach ($item in Get-ChildItem -LiteralPath $source -Force) {
        if ($item.Name -in @('LightOverLeafRuntimeLauncher.exe', 'runtime')) { continue }
        Copy-Item -LiteralPath $item.FullName -Destination $distribution -Recurse -Force
    }

    $runtimeRoot = Join-Path $distribution 'runtime'
    New-Item -ItemType Directory -Path $runtimeRoot -Force | Out-Null
    Copy-Item -LiteralPath $MiKTeXRoot -Destination (Join-Path $runtimeRoot 'miktex') -Recurse
    & (Join-Path $PSScriptRoot 'prepare-chinese-runtime.ps1') -Root (Join-Path $runtimeRoot 'miktex')
    & (Join-Path $PSScriptRoot 'initialize-pdflatex.ps1') -Root (Join-Path $runtimeRoot 'miktex')
    $licenses = Join-Path $distribution 'licenses'
    New-Item -ItemType Directory -Path $licenses -Force | Out-Null
    foreach ($package in @('react', 'react-dom', 'scheduler', 'monaco-editor', 'pdfjs-dist')) {
        $license = Join-Path $repo "web/node_modules/$package/LICENSE"
        Require-File $license
        Copy-Item -LiteralPath $license -Destination (Join-Path $licenses "$package.txt") -Force
    }
    $sqliteLicense = Join-Path $repo '..\QTBest\thirdparty_install\vcpkg\installed\x64-windows\share\sqlite3\copyright'
    Require-File $sqliteLicense
    Copy-Item -LiteralPath $sqliteLicense -Destination (Join-Path $licenses 'sqlite3.txt') -Force
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'package-readme.txt') `
        -Destination (Join-Path $distribution 'README.txt') -Force
    foreach ($forbidden in @('payload.7z', 'payload.zip', 'LightOverLeafRuntimeLauncher.exe')) {
        if (Test-Path -LiteralPath (Join-Path $distribution $forbidden)) {
            throw "Forbidden self-extracting component found: $forbidden"
        }
    }

    $manifestEntries = @(Get-ChildItem -LiteralPath $distribution -Recurse -File |
        Sort-Object FullName | ForEach-Object {
            [pscustomobject][ordered]@{
                path = $_.FullName.Substring($distribution.Length + 1).Replace('\', '/')
                bytes = $_.Length
                sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
            }
        })
    $totalBytes = ($manifestEntries | Measure-Object -Property bytes -Sum).Sum
    $manifestPath = Join-Path $distribution 'runtime-manifest.json'
    [ordered]@{
        schemaVersion = 1
        product = 'LightOverLeaf'
        edition = 'Full'
        delivery = 'expanded-green-directory'
        runtimeExtraction = $false
        texRuntimeKind = 'MiKTeX'
        entrypoint = 'LightOverLeaf.exe'
        fileCount = $manifestEntries.Count
        totalBytes = $totalBytes
        files = $manifestEntries
    } | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
    $manifestEntries | ForEach-Object { "$($_.sha256)  $($_.path)" } |
        Set-Content -LiteralPath (Join-Path $distribution 'package-files.sha256') -Encoding ASCII

    $application = Join-Path $distribution 'LightOverLeaf.exe'
    Require-GuiSubsystem $application
    $packagedMiKTeXBin = Resolve-MiKTeXBin (Join-Path $distribution 'runtime/miktex')
    foreach ($tool in @('pdflatex.exe','synctex.exe')) {
        Require-File (Join-Path $packagedMiKTeXBin $tool)
    }

    $smokeProcess = $null
    $smokeTimer = [Diagnostics.Stopwatch]::StartNew()
    try {
        $smokeProcess = Start-Process -FilePath $application -ArgumentList '--smoke-test' `
            -WorkingDirectory $distribution -WindowStyle Hidden -PassThru
        if (!$smokeProcess.WaitForExit($SmokeTimeoutSeconds * 1000)) {
            $taskkill = Join-Path $env:WINDIR 'System32/taskkill.exe'
            $killResult = Start-Process -FilePath $taskkill `
                -ArgumentList @('/PID', "$($smokeProcess.Id)", '/T', '/F') `
                -Wait -PassThru -WindowStyle Hidden
            if ($killResult.ExitCode -ne 0 -and !$smokeProcess.HasExited) { $smokeProcess.Kill() }
            throw "Expanded Full application smoke timed out after $SmokeTimeoutSeconds seconds."
        }
        if ($smokeProcess.ExitCode -ne 0) {
            throw "Expanded Full application smoke failed: $($smokeProcess.ExitCode)"
        }
    } finally {
        $smokeTimer.Stop()
        if ($smokeProcess) { $smokeProcess.Dispose() }
    }

    $manifestHash = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash
    [ordered]@{
        product = 'LightOverLeaf'
        edition = 'Full'
        directory = $distribution
        entrypoint = $application
        delivery = 'expanded-green-directory'
        runtimeExtraction = $false
        archiveCreated = $false
        bundledArchiveTool = $false
        texRuntimeKind = 'MiKTeX'
        texRuntimeSource = 'source-built'
        manifestedFiles = $manifestEntries.Count
        payloadBytes = $totalBytes
        manifestSha256 = $manifestHash
        consoleApplication = $false
        directorySmoke = 'passed'
        directorySmokeTimeoutSeconds = $SmokeTimeoutSeconds
        directorySmokeElapsedMilliseconds = $smokeTimer.ElapsedMilliseconds
        signed = $false
    } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runRoot 'package-report.json') -Encoding UTF8
    Write-Host "Expanded Full distribution ready: $distribution"
    Write-Host 'Run LightOverLeaf.exe directly; no runtime archive extraction is used.'
} finally { Pop-Location }
