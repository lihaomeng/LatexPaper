# Return configure arguments when an existing output directory belongs to the old source root.
function Get-CMakeRefreshArguments {
    param([string]$Repo, [string]$Preset, [switch]$Fresh)
    $cachePath = Join-Path $Repo "out/$Preset/CMakeCache.txt"
    $arguments = @()
    $sourceChanged = $false
    if (Test-Path -LiteralPath $cachePath -PathType Leaf) {
        $cacheLines = Get-Content -LiteralPath $cachePath
        $homeEntry = $cacheLines | Where-Object { $_ -match '^CMAKE_HOME_DIRECTORY:INTERNAL=' } | Select-Object -First 1
        if ($homeEntry) {
            $previousSource = [IO.Path]::GetFullPath(($homeEntry -replace '^[^=]+=', ''))
            $expectedSource = [IO.Path]::GetFullPath((Join-Path $Repo 'backend/latexlocalservice'))
            $sourceChanged = $previousSource -ne $expectedSource
        }
        if ($sourceChanged) {
            Write-Host 'CMake source directory moved: refreshing generated configuration.'
            # Preserve explicit typed cache settings while discarding only generated INTERNAL/STATIC entries.
            foreach ($line in $cacheLines) {
                if ($line -match '^([^#/:][^:]*):(BOOL|STRING|PATH|FILEPATH)=(.*)$') {
                    $arguments += "-D$($Matches[1]):$($Matches[2])=$($Matches[3])"
                }
            }
        }
    }
    if ($Fresh -or $sourceChanged) { $arguments += '--fresh' }
    return $arguments
}
