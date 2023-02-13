. $PSScriptRoot/../end-to-end-tests-prelude.ps1

$CurrentTest = "Test pkgconfig locations"

$commonArgs += @("--enforce-port-checks", "--binarysource=clear")

Run-Vcpkg @commonArgs install "wrong-pkgconfig[header-only-good]"
Throw-IfFailed
Remove-Item -Recurse -Force $installRoot

Run-Vcpkg @commonArgs install "wrong-pkgconfig[header-only-bad]"
Throw-IfNotFailed
Remove-Item -Recurse -Force $installRoot

Run-Vcpkg @commonArgs install "wrong-pkgconfig[lib-good]"
Throw-IfFailed
Remove-Item -Recurse -Force $installRoot

Run-Vcpkg @commonArgs install "wrong-pkgconfig[lib-bad]"
Throw-IfNotFailed
Remove-Item -Recurse -Force $installRoot

Run-Vcpkg @commonArgs install "wrong-pkgconfig[debug-good]"
Throw-IfFailed
Remove-Item -Recurse -Force $installRoot

Run-Vcpkg @commonArgs install "wrong-pkgconfig[debug-bad]"
Throw-IfNotFailed
Remove-Item -Recurse -Force $installRoot
