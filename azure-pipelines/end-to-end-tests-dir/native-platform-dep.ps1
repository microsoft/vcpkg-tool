. "$PSScriptRoot/../end-to-end-tests-prelude.ps1"

if ($IsLinux) {
    if ([System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture -eq 'Arm64') {
        $host_triplet = 'arm64-linux-e2e'
    } else {
        $host_triplet = 'x64-linux-e2e'
    }
} elseif ($IsMacOS) {
    $host_triplet = 'arm64-osx-e2e'
} elseif ($IsWindows) {
    $host_triplet = 'x64-windows-e2e'
} else {
    Write-Warning "Unknown platform."
    return
}

Write-Trace "test native qualified dependencies"

$commonArgs += @("--x-builtin-ports-root=$PSScriptRoot/../e2e-ports", "--overlay-triplets=$PSScriptRoot/../overlay-triplets")

Run-Vcpkg install @commonArgs --host-triplet $Triplet vcpkg-native-dependency:$Triplet
Throw-IfFailed
Require-FileExists "$installRoot/$Triplet/share/vcpkg-empty-port"
Refresh-TestRoot

Run-Vcpkg install @commonArgs --host-triplet $host_triplet vcpkg-native-dependency:$Triplet
Throw-IfFailed
Require-FileNotExists "$installRoot/$Triplet/share/vcpkg-empty-port"
Refresh-TestRoot
