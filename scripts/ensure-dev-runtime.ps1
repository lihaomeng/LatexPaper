param([Parameter(Mandatory)][string]$Source,
    [Parameter(Mandatory)][string]$Destination, [switch]$Refresh)
$ErrorActionPreference = 'Stop'
$Source = [IO.Path]::GetFullPath($Source)
$Destination = [IO.Path]::GetFullPath($Destination)
if (!(Test-Path -LiteralPath (Join-Path $Source 'lightoverleaf-miktex-runtime.json'))) {
    throw "Missing Runtime metadata below $Source"
}
# Incremental-build fingerprint, not a release integrity certificate.
$entries = [Collections.Generic.List[string]]::new()
$entries.Add($Source)
foreach ($file in (Get-ChildItem -LiteralPath $Source -File -Recurse -Force | Sort-Object FullName)) {
    $entries.Add($file.FullName.Substring($Source.Length) + ':' + $file.Length + ':' + $file.LastWriteTimeUtc.Ticks)
}
foreach ($name in @('ensure-dev-runtime.ps1','deploy-miktex.ps1','prepare-chinese-runtime.ps1','initialize-pdflatex.ps1')) {
    $entries.Add((Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $name)).Hash)
}
$sha = [Security.Cryptography.SHA256]::Create()
try { $fingerprint = [BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes(($entries -join "`n")))) }
finally { $sha.Dispose() }
$stamp = Join-Path $Destination '.lightoverleaf-dev-deployment'
$ready = Test-Path -LiteralPath $stamp -PathType Leaf
foreach ($relative in @('lightoverleaf-miktex-runtime.json',
    'texmfs/data/miktex/data/le/pdftex/pdflatex.fmt')) {
    $ready = $ready -and (Test-Path -LiteralPath (Join-Path $Destination $relative) -PathType Leaf)
}
$bin = @('texmfs/install/miktex/bin/x64','miktex/bin/x64') | Where-Object {
    Test-Path -LiteralPath (Join-Path $Destination "$_/pdflatex.exe")
} | Select-Object -First 1
$ready = $ready -and [bool]$bin
if ($bin) {
    foreach ($tool in @('pdflatex.exe','bibtex.exe','synctex.exe')) {
        $ready = $ready -and (Test-Path -LiteralPath (Join-Path $Destination "$bin/$tool") -PathType Leaf)
    }
}
if (!$Refresh -and $ready -and ([IO.File]::ReadAllText($stamp) -eq $fingerprint)) {
    Write-Host 'MiKTeX Runtime unchanged: skipping deployment and format initialization.'
    return
}
& (Join-Path $PSScriptRoot 'deploy-miktex.ps1') -Source $Source -Destination $Destination
if ($LASTEXITCODE -ne 0) { throw 'MiKTeX Runtime deployment failed.' }
[IO.File]::WriteAllText($stamp, $fingerprint)
