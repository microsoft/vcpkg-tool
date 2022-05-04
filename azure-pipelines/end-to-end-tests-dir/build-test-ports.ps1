. $PSScriptRoot/../end-to-end-tests-prelude.ps1

$CurrentTest = "Build Test Ports"

Run-Vcpkg --overlay-ports="$PSScriptRoot/../e2e_ports/overlays" install vcpkg-empty-port
Run-Vcpkg --overlay-ports="$PSScriptRoot/../e2e_ports" install vcpkg-internal-e2e-test-port
if ($IsWindows) {
    Run-Vcpkg --overlay-ports="$PSScriptRoot/../e2e_ports" install vcpkg-find-acquire-program
}
Throw-IfFailed
