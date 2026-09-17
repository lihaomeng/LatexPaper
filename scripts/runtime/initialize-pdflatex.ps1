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
[void]$process.Handle
try {
    $timer = [Diagnostics.Stopwatch]::StartNew()
    while (!$process.WaitForExit(1000)) {
        if ($timer.Elapsed.TotalSeconds -ge 180) {
            & (Join-Path $env:WINDIR 'System32/taskkill.exe') /PID $process.Id /T /F | Out-Null
            throw "pdfLaTeX format initialization timed out. See $logDirectory"
        }
    }
    $process.WaitForExit()
    $process.Refresh()
    if ($process.ExitCode -ne 0) {
        Get-Content -LiteralPath $errorLog -Tail 15
        throw "pdfLaTeX format initialization failed ($($process.ExitCode)). See $logDirectory"
    }
} finally { $process.Dispose() }
$format = Get-ChildItem -LiteralPath (Join-Path $Root 'texmfs') -Recurse -File -Filter pdflatex.fmt | Where-Object { $_.Length -gt 0 } | Select-Object -First 1
if (!$format) { throw "MiKTeX reported success but pdflatex.fmt is missing below $Root" }
Write-Host "pdfLaTeX format ready: $($format.FullName)"
# Deployment readiness check: compile a controlled Chinese document offline.
$checkDirectory = Join-Path $logDirectory ('chinese-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $checkDirectory | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '../../support/resources/tex/chinese-check.tex') -Destination (Join-Path $checkDirectory 'main.tex')
$latexArguments = @('--disable-installer','-no-shell-escape','-interaction=nonstopmode','-halt-on-error','-file-line-error','main.tex')
$stages = @(
    @{Name='latex-1'; Tool='pdflatex.exe'; Arguments=$latexArguments},
    @{Name='bibtex'; Tool='bibtex.exe'; Arguments=@('--disable-installer','main.aux')},
    @{Name='latex-2'; Tool='pdflatex.exe'; Arguments=$latexArguments},
    @{Name='latex-3'; Tool='pdflatex.exe'; Arguments=$latexArguments}
)
foreach ($stage in $stages) {
    Write-Host ('Offline readiness: ' + $stage.Name)
$process = Start-Process -FilePath (Join-Path $bin $stage.Tool) -ArgumentList $stage.Arguments -WorkingDirectory $checkDirectory -WindowStyle Hidden -RedirectStandardOutput (Join-Path $checkDirectory ($stage.Name + '.stdout.log')) -RedirectStandardError (Join-Path $checkDirectory ($stage.Name + '.stderr.log')) -PassThru
[void]$process.Handle
try {
    $timer = [Diagnostics.Stopwatch]::StartNew()
    while (!$process.WaitForExit(1000)) {
        if ($timer.Elapsed.TotalSeconds -ge 60) {
            & (Join-Path $env:WINDIR 'System32/taskkill.exe') /PID $process.Id /T /F | Out-Null
            throw "Chinese runtime check timed out: $checkDirectory"
        }
    }
    $process.WaitForExit()
    $process.Refresh()
    if ($process.ExitCode -ne 0) {
        Get-Content -LiteralPath (Join-Path $checkDirectory ($stage.Name + '.stdout.log')) -Tail 20
        throw "Chinese runtime check failed: $checkDirectory"
    }
} finally { $process.Dispose() }
}
$pdf = Get-Item -LiteralPath (Join-Path $checkDirectory 'main.pdf') -ErrorAction Stop
if ($pdf.Length -eq 0) { throw 'Chinese runtime check produced an empty PDF.' }
Write-Host "Chinese pdfLaTeX ready: $($pdf.FullName)"
$bbl = Get-Content -LiteralPath (Join-Path $checkDirectory 'main.bbl') -Raw
if (!$bbl.Contains('\bibitem{runtimecheck}')) { throw 'Bibliography readiness check did not resolve the fixture citation.' }
$global:LASTEXITCODE = 0