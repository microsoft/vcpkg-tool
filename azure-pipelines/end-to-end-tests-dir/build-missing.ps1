. $PSScriptRoot/../end-to-end-tests-prelude.ps1

$CurrentTest = "Build Missing tests"

Run-Vcpkg -TestArgs ($commonArgs + @("install", "vcpkg-hello-world-1", "--only-binarycaching","--x-binarysource=clear;files,$ArchiveRoot,read"))
Throw-IfNotFailed
Require-FileNotExists "$installRoot/$Triplet/include/hello-1.h"

# Create the vcpkg-hello-world-1 archive
Remove-Item -Recurse -Force $installRoot
Run-Vcpkg -TestArgs ($commonArgs + @("install", "vcpkg-hello-world-1","--x-binarysource=clear;files,$ArchiveRoot,write"))
Throw-IfFailed
Require-FileExists "$installRoot/$Triplet/include/hello-1.h"

Remove-Item -Recurse -Force $installRoot
Run-Vcpkg -TestArgs ($commonArgs + @("install", "vcpkg-hello-world-1", "--only-binarycaching","--x-binarysource=clear;files,$ArchiveRoot,read"))
Throw-IfFailed
Require-FileExists "$installRoot/$Triplet/include/hello-1.h"
