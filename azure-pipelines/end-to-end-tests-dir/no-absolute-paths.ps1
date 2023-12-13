. $PSScriptRoot/../end-to-end-tests-prelude.ps1

$CurrentTest = "No absolute paths"

$commonArgs += @("--enforce-port-checks", "--binarysource=clear")

Run-Vcpkg @commonArgs install "absolute-paths[hash]"
Throw-IfNotFailed
Remove-Item -Recurse -Force $installRoot

Run-Vcpkg @commonArgs install "absolute-paths[python]"
Throw-IfNotFailed
Remove-Item -Recurse -Force $installRoot

Run-Vcpkg @commonArgs install "absolute-paths[python-comment]"
Throw-IfFailed
Remove-Item -Recurse -Force $installRoot

Run-Vcpkg @commonArgs install "absolute-paths[header]"
Throw-IfNotFailed
Remove-Item -Recurse -Force $installRoot

Run-Vcpkg @commonArgs install "absolute-paths[header-comment]"
Throw-IfFailed
Remove-Item -Recurse -Force $installRoot

Run-Vcpkg @commonArgs install "absolute-paths[usage]"
Throw-IfNotFailed
Remove-Item -Recurse -Force $installRoot

Run-Vcpkg @commonArgs install "absolute-paths[usage, new-policy]"
Throw-IfFailed
Remove-Item -Recurse -Force $installRoot

Run-Vcpkg @commonArgs install "absolute-paths[packages]"
Throw-IfNotFailed
Remove-Item -Recurse -Force $installRoot

Run-Vcpkg @commonArgs install "absolute-paths[native]"
Throw-IfNotFailed
Remove-Item -Recurse -Force $installRoot
