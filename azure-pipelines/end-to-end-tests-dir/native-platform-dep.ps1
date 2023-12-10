. "$PSScriptRoot/../end-to-end-tests-prelude.ps1"

if ($IsLinux) {
    $host_triplet = 'x64-linux-e2e'
} elseif ($IsMacOS) {
    $host_triplet = 'x64-osx-e2e'
} elseif ($IsWindows) {
    $host_triplet = 'x64-windows-e2e'
} else {
    Write-Warning "Unknown platform."
    return
}


Write-Trace "test native qualified dependencies"

Run-Vcpkg install @CommonArgs --host-triplet $Triplet vcpkg-native-dependency:$Triplet
Throw-IfFailed
Require-FileExists "$installRoot/$Triplet/share/vcpkg-empty-port"
Refresh-TestRoot

Run-Vcpkg install @CommonArgs --host-triplet $host_triplet vcpkg-native-dependency:$Triplet
Throw-IfFailed
Require-FileNotExists "$installRoot/$Triplet/share/vcpkg-empty-port"
Refresh-TestRoot
