. $PSScriptRoot/../end-to-end-tests-prelude.ps1

$CurrentTest = "Build Test Ports"

Run-Vcpkg @commonArgs --overlay-ports="$PSScriptRoot/../e2e_ports" install vcpkg-internal-e2e-test-port3
Throw-IfFailed

$output = Run-Vcpkg @commonArgs --overlay-ports="$PSScriptRoot/../e2e_ports/vcpkg-internal-e2e-test-port2" install vcpkg-internal-e2e-test-port2
$output
Throw-IfFailed
if ($output -match "vcpkg-internal-e2e-test-port3") {
    throw "Should not emit messages about -port3 while checking -port2"
}

return

Run-Vcpkg @commonArgs --overlay-ports="$PSScriptRoot/../e2e_ports/overlays" install vcpkg-empty-port
Throw-IfFailed
Run-Vcpkg @commonArgs --overlay-ports="$PSScriptRoot/../e2e_ports" install vcpkg-internal-e2e-test-port
Throw-IfFailed
if ($IsWindows) {
    Run-Vcpkg @commonArgs --overlay-ports="$PSScriptRoot/../e2e_ports" install vcpkg-find-acquire-program
    Throw-IfFailed
}
