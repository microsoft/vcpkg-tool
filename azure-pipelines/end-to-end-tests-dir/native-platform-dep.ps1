. "$PSScriptRoot/../end-to-end-tests-prelude.ps1"

if ($IsLinux) {
    $triplet = 'x64-linux-e2e'
    $host_triplet = 'x64-linux'
} else if ($IsMacOS) {
    $triplet = 'x64-osx-e2e'
    $host_triplet = 'x64-osx'
} else if ($IsWindows) {
    $triplet = 'x64-windows-e2e'
    $host_triplet = 'x64-windows'
} else {
    Write-Warning "Unknown platform."
    return
}


Write-Trace "test native qualified dependencies"

$Overlays = "--overlay-ports=$PSScriptRoot/../e2e_ports/overlays", "--overlay-triplets=$PSScriptRoot/../e2e_ports/triplets"

Run-Vcpkg x-set-installed
Throw-IfFailed
Run-Vcpkg install @Overlays --triplet $Triplet --host-triplet $Triplet vcpkg-native-dependency
Throw-IfFailed
$ports = Run-Vcpkg list | % { (-split $_)[0] } | % { ($_ -split ':')[0] }

if ('vcpkg-empty-port' -notin $ports) {
    throw "native platform dependency incorrect - vcpkg-empty-port not installed"
}

Run-Vcpkg x-set-installed
Throw-IfFailed
Run-Vcpkg install @Overlays --triplet $Triplet --host-triplet $Triplet vcpkg-native-dependency
Throw-IfFailed
$ports = Run-Vcpkg list | % { (-split $_)[0] } | % { ($_ -split ':')[0] }

if ('vcpkg-empty-port' -in $ports) {
    throw "native platform dependency incorrect - vcpkg-empty-port installed"
}

Run-Vcpkg x-set-installed
Throw-IfFailed
