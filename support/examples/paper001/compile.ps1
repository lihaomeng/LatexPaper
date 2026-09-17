param([string]$RuntimeRoot = (Join-Path $PSScriptRoot '../../../out/electron-dev/bin/Release/runtime/miktex'))
$ErrorActionPreference = 'Stop'
$bin = Join-Path ([IO.Path]::GetFullPath($RuntimeRoot)) 'texmfs/install/miktex/bin/x64'
foreach ($tool in @('pdflatex.exe','bibtex.exe')) {
    if (!(Test-Path -LiteralPath (Join-Path $bin $tool))) { throw "Missing $tool in $bin" }
}
# Work on a private copy, keeping intermediate files out of the source project.
$output = Join-Path ([IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../out'))) ('paper1-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $output | Out-Null
foreach ($entry in @('main.tex','references.bib','sections','data','figures')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $entry) -Destination $output -Recurse
}
$latex = @('--disable-installer','-no-shell-escape','-interaction=nonstopmode','-halt-on-error','-file-line-error','-synctex=1','main.tex')
$steps = @(
    @{Name='latex-1'; Exe='pdflatex.exe'; Args=$latex},
    @{Name='bibtex'; Exe='bibtex.exe'; Args=@('--disable-installer','main.aux')},
    @{Name='latex-2'; Exe='pdflatex.exe'; Args=$latex},
    @{Name='latex-3'; Exe='pdflatex.exe'; Args=$latex}
)
foreach ($step in $steps) {
    $process = Start-Process -FilePath (Join-Path $bin $step.Exe) -ArgumentList $step.Args -WorkingDirectory $output -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $output ($step.Name+'.stdout.log')) -RedirectStandardError (Join-Path $output ($step.Name+'.stderr.log'))
    [void]$process.Handle
    try {
        if (!$process.WaitForExit(60000)) {
            & (Join-Path $env:WINDIR 'System32/taskkill.exe') /PID $process.Id /T /F | Out-Null
            throw "Timeout: $($step.Name). Logs: $output"
        }
        $process.WaitForExit(); $process.Refresh()
        if ($process.ExitCode -ne 0) { throw "Failed: $($step.Name). Logs: $output" }
    } finally { $process.Dispose() }
}
$log = Get-Content -LiteralPath (Join-Path $output 'main.log') -Raw
if ($log -match 'undefined references|Citation .+ undefined|Label\(s\) may have changed|There were undefined') { throw "Unresolved references: $output" }
$bbl = Get-Content -LiteralPath (Join-Path $output 'main.bbl') -Raw
foreach ($key in @('lolData2026','lolMetrics2026','lolReproduce2026')) {
    if (!$bbl.Contains('\bibitem{'+$key+'}')) { throw "Missing bibliography item: $key" }
}
if (!(Get-Item -LiteralPath (Join-Path $output 'main.pdf')).Length) { throw 'Empty PDF' }
Write-Host "PAPER READY: $output/main.pdf"
Select-String -LiteralPath (Join-Path $output 'main.log') -Pattern 'Output written|Warning|Overfull|Underfull'
