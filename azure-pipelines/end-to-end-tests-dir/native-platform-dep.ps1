. "$PSScriptRoot/../end-to-end-tests-prelude.ps1"

Write-Trace "test native qualified dependencies"

$commonArgs += @("--x-builtin-ports-root=$PSScriptRoot/../e2e-ports", "--overlay-triplets=$PSScriptRoot/../overlay-triplets")

Run-Vcpkg install @commonArgs --host-triplet $Triplet vcpkg-native-dependency:$Triplet
Throw-IfFailed
Require-FileExists "$installRoot/$Triplet/share/vcpkg-empty-port"
Refresh-TestRoot

Run-Vcpkg install @commonArgs --host-triplet $HostE2ETriplet vcpkg-native-dependency:$Triplet
Throw-IfFailed
Require-FileNotExists "$installRoot/$Triplet/share/vcpkg-empty-port"
Refresh-TestRoot
