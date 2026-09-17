param([ValidateSet('dev','release')][string]$Mode = 'dev')
$ErrorActionPreference = 'Stop'
Push-Location (Join-Path $PSScriptRoot '../../web')
try {
    $nodeVersion = & node --version
    if ($LASTEXITCODE -ne 0) { throw 'Node.js is unavailable.' }
    $fingerprint = ((Get-FileHash -LiteralPath 'package.json').Hash,
        (Get-FileHash -LiteralPath 'package-lock.json').Hash, $nodeVersion) -join ':'
    $stamp = 'node_modules/.lightoverleaf-dependencies'
    $ready = (Test-Path -LiteralPath $stamp) -and
        (Test-Path -LiteralPath 'node_modules/.bin/tsc.cmd') -and
        (Test-Path -LiteralPath 'node_modules/.bin/vite.cmd')
    if ($Mode -eq 'release' -or !$ready -or ([IO.File]::ReadAllText((Join-Path (Get-Location) $stamp)) -ne $fingerprint)) {
        & npm.cmd ci --no-audit --no-fund
        if ($LASTEXITCODE -ne 0) { throw 'Frontend dependency installation failed.' }
        [IO.File]::WriteAllText((Join-Path (Get-Location) $stamp), $fingerprint)
    } else { Write-Host 'Frontend dependencies unchanged: reusing node_modules.' }
    $command = if ($Mode -eq 'dev') { 'build' } else { 'check' }
    & npm.cmd run $command
    if ($LASTEXITCODE -ne 0) { throw "Frontend $command failed." }
} finally { Pop-Location }
