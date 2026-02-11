$sha = git ls-remote https://github.com/microsoft/vcpkg master |
    ForEach-Object { [regex]::Match($_, '^[0-9a-f]+').Value } |
    Select-Object -First 1

if (-not $sha) { throw "Failed to determine vcpkg scripts SHA." }

"$sha`n" | Out-File -LiteralPath "$PSScriptRoot/vcpkg-scripts-sha.txt" -Encoding Ascii -NoNewline

$configPath = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\vcpkg-configuration.json')).Path
$configText = Get-Content -LiteralPath $configPath -Raw -Encoding Ascii
$newConfigText = $configText -replace '("baseline"\s*:\s*")[0-9a-f]+(")', "`$1$sha`$2"
$newConfigText | Out-File -LiteralPath $configPath -Encoding Ascii -NoNewline
