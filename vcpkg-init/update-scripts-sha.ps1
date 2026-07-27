param(
    [ValidatePattern('^[0-9a-fA-F]{40}$')]
    [string]$Sha
)

if ($Sha) {
    $Sha = $Sha.ToLowerInvariant()
} else {
    $Sha = git ls-remote https://github.com/microsoft/vcpkg master |
        ForEach-Object { [regex]::Match($_, '^[0-9a-f]+').Value } |
        Select-Object -First 1
}

if (-not $Sha) { throw "Failed to determine vcpkg scripts SHA." }

"$Sha`n" | Out-File -LiteralPath "$PSScriptRoot/vcpkg-scripts-sha.txt" -Encoding Ascii -NoNewline

$configPath = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../src/vcpkg-configuration.json')).Path
$config = Get-Content -LiteralPath $configPath -Raw -Encoding Ascii | ConvertFrom-Json

if (-not $config.'default-registry') { throw "Missing default-registry in vcpkg-configuration.json." }

$config.'default-registry'.baseline = $Sha
$configJson = $config | ConvertTo-Json -Depth 100
$configJson = $configJson -replace "`r`n", "`n"
[System.IO.File]::WriteAllText($configPath, "$configJson`n", [System.Text.Encoding]::ASCII)
