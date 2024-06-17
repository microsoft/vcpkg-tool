. $PSScriptRoot/../end-to-end-tests-prelude.ps1

$CurrentTest = "Build Test Ports"

$output = Run-VcpkgAndCaptureOutput @commonArgs --overlay-ports="$PSScriptRoot/../e2e-ports" install vcpkg-internal-e2e-test-port3
Throw-IfFailed
if ($output -match "Compiler found: ([^\r\n]+)") {
    $detected = $matches[1]
    if (-Not (Test-Path $detected)) {
        throw "Did not print a valid compiler path (detected $detected)"
    }
} else {
    throw "Did not detect a compiler"
}


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
$output = Run-VcpkgAndCaptureOutput @commonArgs --overlay-ports="$PSScriptRoot/../e2e-ports/broken-manifests" install broken-no-name
Throw-IfNotFailed
if ($output -notmatch "missing required field 'name'") {
    throw 'Did not detect missing field'
}

$output = Run-VcpkgAndCaptureOutput @commonArgs --overlay-ports="$PSScriptRoot/../e2e-ports/broken-manifests" install broken-no-version
Throw-IfNotFailed
if ($output -notmatch 'expected a versioning field') {
    throw 'Did not detect missing field'
}

$output = Run-VcpkgAndCaptureOutput @commonArgs --overlay-ports="$PSScriptRoot/../e2e-ports/broken-manifests" install malformed
Throw-IfNotFailed
if ($output -notmatch 'Trailing comma') {
    throw 'Did not detect malformed JSON'
}

# Check for msgAlreadyInstalled vs. msgAlreadyInstalledNotHead
$output = Run-VcpkgAndCaptureOutput @commonArgs --overlay-ports="$PSScriptRoot/../e2e-ports" install vcpkg-internal-e2e-test-port3
Throw-IfFailed
if ($output -notmatch 'vcpkg-internal-e2e-test-port3:[^ ]+ is already installed') {
    throw 'Wrong already installed message'
}

$output = Run-VcpkgAndCaptureOutput @commonArgs --overlay-ports="$PSScriptRoot/../e2e-ports" install vcpkg-internal-e2e-test-port3 --head
Throw-IfFailed
if ($output -notmatch 'vcpkg-internal-e2e-test-port3:[^ ]+ is already installed -- not building from HEAD') {
    throw 'Wrong already installed message for --head'
}
