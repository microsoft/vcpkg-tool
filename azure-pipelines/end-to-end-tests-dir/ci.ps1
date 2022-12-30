. $PSScriptRoot/../end-to-end-tests-prelude.ps1

# Not a number
Run-Vcpkg ci --triplet=$Triplet --x-skipped-cascade-count=fish
Throw-IfNotFailed

# Negative
Run-Vcpkg ci --triplet=$Triplet --x-skipped-cascade-count=-1
Throw-IfNotFailed

# Clearly not the correct answer
Run-Vcpkg ci --triplet=$Triplet --x-skipped-cascade-count=1000
Throw-IfNotFailed

# Regular ci; may take a few seconds to complete
$port_list = Run-VcpkgAndCaptureOutput ci --dry-run --triplet=$Triplet --binarysource=clear --overlay-ports="$PSScriptRoot/../e2e_ci"
Throw-IfFailed
if ($port_list -match "zzz-unexpected-port[^ ]*[ ]*->") {
    throw 'Detected installation of "zzz-unexpected-port"'
}
if ($port_list -match "zzz-unexpected-feature[^ ]*[ ]*->") {
    throw 'Detected installation of "zzz-unexpected-feature"'
}
if ($port_list -notmatch "zzz-expected-port[^ ]*[ ]*->") {
    throw 'Did not detect installation of "zzz-expected-port"'
}
