. $PSScriptRoot/../end-to-end-tests-prelude.ps1

$CurrentTest = "Build Command"

# Install the dependencies of vcpkg-hello-world-1
Run-Vcpkg -TestArgs ($commonArgs + @("install","vcpkg-cmake","vcpkg-cmake-config","--host-triplet",$Triplet))
Throw-IfFailed

# Test that the build command works
Run-Vcpkg -TestArgs ($commonArgs + @("build","vcpkg-hello-world-1","--host-triplet",$Triplet))
Throw-IfFailed

# Regression test https://github.com/microsoft/vcpkg/issues/13933
Run-Vcpkg -TestArgs ($commonArgs + @("install","vcpkg-hello-world-1","--host-triplet",$Triplet))
Throw-IfFailed
Run-Vcpkg -TestArgs ($commonArgs + @("build","vcpkg-hello-world-1","--host-triplet",$Triplet))
Throw-IfFailed
