git ls-remote https://github.com/microsoft/vcpkg master | `
    ForEach-Object { $x = [regex]::Match($_, '^[0-9a-f]+').Value; "$x`n" } | `
    Out-File -LiteralPath "$PSScriptRoot/vcpkg-scripts-sha.txt" -Encoding Ascii -NoNewline
