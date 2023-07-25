. $PSScriptRoot/../end-to-end-tests-prelude.ps1

$CurrentTest = "Build Test Ports"

Run-Vcpkg @commonArgs --overlay-ports="$PSScriptRoot/../e2e-ports" install vcpkg-internal-e2e-test-port3
Throw-IfFailed

$output = Run-VcpkgAndCaptureOutput @commonArgs --overlay-ports="$PSScriptRoot/../e2e-ports/vcpkg-internal-e2e-test-port2" install vcpkg-internal-e2e-test-port2
Throw-IfFailed
if ($output -match 'vcpkg-internal-e2e-test-port3') {
    throw "Should not emit messages about -port3 while checking -port2"
}

Run-Vcpkg @commonArgs --overlay-ports="$PSScriptRoot/../e2e-ports/overlays" install vcpkg-empty-port
Throw-IfFailed
Run-Vcpkg @commonArgs --overlay-ports="$PSScriptRoot/../e2e-ports" install vcpkg-internal-e2e-test-port
Throw-IfFailed
Run-Vcpkg @commonArgs --overlay-ports="$PSScriptRoot/../e2e-ports" install control-file
Throw-IfFailed
$output = Run-VcpkgAndCaptureOutput @commonArgs --overlay-ports="$PSScriptRoot/../e2e-ports/overlays" install broken-no-name
Throw-IfNotFailed
if ($output -notmatch "missing required field 'name'") {
    throw 'Did not detect missing field'
}

$output = Run-VcpkgAndCaptureOutput @commonArgs --overlay-ports="$PSScriptRoot/../e2e-ports/overlays" install broken-no-version
Throw-IfNotFailed
if ($output -notmatch 'expected a versioning field') {
    throw 'Did not detect missing field'
}
