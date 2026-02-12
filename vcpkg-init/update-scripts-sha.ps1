$sha = git ls-remote https://github.com/microsoft/vcpkg master |
    ForEach-Object { [regex]::Match($_, '^[0-9a-f]+').Value } |
    Select-Object -First 1

if (-not $sha) { throw "Failed to determine vcpkg scripts SHA." }

"$sha`n" | Out-File -LiteralPath "$PSScriptRoot/vcpkg-scripts-sha.txt" -Encoding Ascii -NoNewline

$configPath = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../src/vcpkg-configuration.json')).Path
$config = Get-Content -LiteralPath $configPath -Raw -Encoding Ascii | ConvertFrom-Json

if (-not $config.'default-registry') { throw "Missing default-registry in vcpkg-configuration.json." }

$config.'default-registry'.baseline = $sha
$configJson = $config | ConvertTo-Json -Depth 100
$configJson = $configJson -replace "`r`n", "`n"
[System.IO.File]::WriteAllText($configPath, "$configJson`n", [System.Text.Encoding]::ASCII)
