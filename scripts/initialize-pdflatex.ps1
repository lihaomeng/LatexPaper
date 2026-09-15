param([Parameter(Mandatory)][string]$Root)
$ErrorActionPreference = 'Stop'
$Root = [IO.Path]::GetFullPath($Root)
$bin = @('texmfs/install/miktex/bin/x64','miktex/bin/x64') | ForEach-Object { Join-Path $Root $_ } | Where-Object {
    Test-Path -LiteralPath (Join-Path $_ 'miktex.exe') -PathType Leaf
} | Select-Object -First 1
if (!$bin) { throw "MiKTeX utility is missing below $Root" }
$logDirectory = Join-Path $Root 'lightoverleaf-deployment-logs'
New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null
$outputLog = Join-Path $logDirectory 'pdflatex-format.stdout.log'
$errorLog = Join-Path $logDirectory 'pdflatex-format.stderr.log'
Write-Host 'Preparing bundled pdflatex.fmt offline (up to 180 seconds)...'
$process = Start-Process -FilePath (Join-Path $bin 'miktex.exe') -ArgumentList @('--disable-installer','formats','build','pdflatex','--engine','pdftex') -WorkingDirectory $Root -WindowStyle Hidden -RedirectStandardOutput $outputLog -RedirectStandardError $errorLog -PassThru
try {
    $timer = [Diagnostics.Stopwatch]::StartNew()
    while (!$process.WaitForExit(1000)) {
        if ($timer.Elapsed.TotalSeconds -ge 180) {
            & (Join-Path $env:WINDIR 'System32/taskkill.exe') /PID $process.Id /T /F | Out-Null
            throw "pdfLaTeX format initialization timed out. See $logDirectory"
        }
    }
    $process.Refresh()
    if ($process.ExitCode -ne 0) {
        Get-Content -LiteralPath $errorLog -Tail 15
        throw "pdfLaTeX format initialization failed ($($process.ExitCode)). See $logDirectory"
    }
} finally { $process.Dispose() }
$format = Get-ChildItem -LiteralPath (Join-Path $Root 'texmfs') -Recurse -File -Filter pdflatex.fmt | Where-Object { $_.Length -gt 0 } | Select-Object -First 1
if (!$format) { throw "MiKTeX reported success but pdflatex.fmt is missing below $Root" }
Write-Host "pdfLaTeX format ready: $($format.FullName)"
$global:LASTEXITCODE = 0