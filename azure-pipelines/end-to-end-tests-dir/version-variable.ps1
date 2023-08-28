. $PSScriptRoot/../end-to-end-tests-prelude.ps1

$portsPath = "$PSScriptRoot/../e2e-ports/version-variable"

$CurrentTest = "version variable in portfile.cmake"
Run-Vcpkg install @commonArgs `
    "--x-builtin-ports-root=$portsPath" `
    --binarysource=clear `
    version version-string version-date version-semver
Throw-IfFailed
Refresh-TestRoot
