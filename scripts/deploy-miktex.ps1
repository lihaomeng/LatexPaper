param([Parameter(Mandatory)][string]$Source, [Parameter(Mandatory)][string]$Destination)
$ErrorActionPreference = 'Stop'
$Source = [IO.Path]::GetFullPath($Source).TrimEnd('\', '/')
$Destination = [IO.Path]::GetFullPath($Destination).TrimEnd('\', '/')
if ($Source -eq $Destination -or $Destination.StartsWith($Source + '\', [StringComparison]::OrdinalIgnoreCase) -or $Source.StartsWith($Destination + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'MiKTeX source and destination must be separate directories.'
}
$metadata = Join-Path $Source 'lightoverleaf-miktex-runtime.json'
if (!(Test-Path -LiteralPath $metadata -PathType Leaf)) { throw "Missing MiKTeX runtime metadata: $metadata" }
$bin = @('texmfs/install/miktex/bin/x64', 'miktex/bin/x64') | Where-Object {
    Test-Path -LiteralPath (Join-Path $Source "$_/pdflatex.exe") -PathType Leaf
} | Select-Object -First 1
if (!$bin) { throw "Missing MiKTeX pdflatex.exe below $Source" }
foreach ($tool in @('pdflatex.exe','synctex.exe')) {
    if (!(Test-Path -LiteralPath (Join-Path $Source "$bin/$tool") -PathType Leaf)) { throw "Missing MiKTeX tool: $tool" }
}
# Copy the complete installed tree, not just executables. No archive or deletion.
Write-Host 'Deploying bundled MiKTeX Runtime (incremental directory copy)...'
& robocopy $Source $Destination /E /R:1 /W:1 /NFL /NDL /NJH /NJS /NP
if ($LASTEXITCODE -ge 8) { throw "MiKTeX copy failed: $LASTEXITCODE" }
$files = @(Get-ChildItem -LiteralPath $Source -Recurse -File -Force)
foreach ($file in $files) {
    $relative = $file.FullName.Substring($Source.Length).TrimStart('\', '/')
    $copied = Get-Item -LiteralPath (Join-Path $Destination $relative) -ErrorAction Stop
    if ($copied.Length -ne $file.Length) { throw "MiKTeX deployment size mismatch: $relative" }
}
& (Join-Path $PSScriptRoot 'initialize-pdflatex.ps1') -Root $Destination
Write-Host "MiKTeX Runtime deployed: $Destination ($($files.Count) files verified by size)"
$global:LASTEXITCODE = 0