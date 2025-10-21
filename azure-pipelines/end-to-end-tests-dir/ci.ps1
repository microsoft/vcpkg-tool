. $PSScriptRoot/../end-to-end-tests-prelude.ps1

# test skipped ports
$Output = Run-VcpkgAndCaptureOutput ci --dry-run --triplet=$Triplet --x-builtin-ports-root="$PSScriptRoot/../e2e-ports/ci"  --binarysource=clear --ci-baseline="$PSScriptRoot/../e2e-assets/ci/ci.baseline.txt"
Throw-IfNotFailed
$ErrorOutput = Run-VcpkgAndCaptureStdErr ci --dry-run --triplet=$Triplet --x-builtin-ports-root="$PSScriptRoot/../e2e-ports/ci"  --binarysource=clear --ci-baseline="$PSScriptRoot/../e2e-assets/ci/ci.baseline.txt"
Throw-IfNotFailed
if (-not ($Output.Contains("dep-on-feature-not-sup:${Triplet}:  cascade"))) {
    throw 'dep-on-feature-not-sup must cascade because it depends on a features that is not supported'
}
if (-not ($Output.Contains("not-sup-host-b:${Triplet}:     skip"))) {
    throw 'not-sup-host-b must be skipped because it is not supported'
}
if (-not ($Output.Contains("feature-not-sup:${Triplet}:        *"))) {
    throw 'feature-not-sup must be built because the port that causes this port to skip should not be installed'
}
if (-not ($Output.Contains("feature-dep-missing:${Triplet}:        *"))) {
    throw 'feature-dep-missing must be built because the broken feature is not selected.'
}
if ($Output.Split("*").Length -ne 4) {
    throw 'base-port should not be installed for the host'
}
if (-not ($ErrorOutput.Contains("REGRESSION: not-sup-host-b:${Triplet} is marked as fail but not supported for ${Triplet}."))) {
    throw "feature-not-sup's baseline fail entry should result in a regression because the port is not supported"
}
if (-not ($ErrorOutput.Contains("REGRESSION: dep-on-feature-not-sup:${Triplet} is marked as fail but one dependency is not supported for ${Triplet}."))) {
    throw "feature-not-sup's baseline fail entry should result in a regression because the port is cascade for this triplet"
}

# any invalid manifest must raise an error
$Output = Run-VcpkgAndCaptureOutput ci --dry-run --triplet=$Triplet --x-builtin-ports-root="$PSScriptRoot/../e2e-ports/broken-manifests"  --binarysource=clear --ci-baseline="$PSScriptRoot/../e2e-assets/ci/ci.baseline.txt"
Throw-IfNotFailed

# test malformed individual overlay port manifest
Remove-Problem-Matchers
$Output = Run-VcpkgAndCaptureOutput ci --dry-run --triplet=$Triplet --x-builtin-ports-root="$PSScriptRoot/../e2e-ports/ci"  --binarysource=clear --ci-baseline="$PSScriptRoot/../e2e-assets/ci/ci.baseline.txt" --overlay-ports="$PSScriptRoot/../e2e-ports/broken-manifests/malformed"
Restore-Problem-Matchers
Throw-IfNotFailed
if (-not ($Output.Contains("vcpkg.json:3:17: error: Trailing comma"))) {
    throw 'malformed port manifest must raise a parsing error'
}

# test malformed overlay port manifests
Remove-Problem-Matchers
$Output = Run-VcpkgAndCaptureOutput ci --dry-run --triplet=$Triplet --x-builtin-ports-root="$PSScriptRoot/../e2e-ports/ci"  --binarysource=clear --ci-baseline="$PSScriptRoot/../e2e-assets/ci/ci.baseline.txt" --overlay-ports="$PSScriptRoot/../e2e-ports/broken-manifests"
Restore-Problem-Matchers
Throw-IfNotFailed
if (-not ($Output.Contains("vcpkg.json:3:17: error: Trailing comma"))) {
    throw 'malformed overlay port manifest must raise a parsing error'
}

# test that file conflicts are detected
Remove-Problem-Matchers
$emptyDir = "$TestingRoot/empty"
New-Item -ItemType Directory -Path $emptyDir -Force | Out-Null
$Output = Run-VcpkgAndCaptureOutput ci --triplet=$Triplet --x-builtin-ports-root="$emptyDir" --binarysource=clear --overlay-ports="$PSScriptRoot/../e2e-ports/duplicate-file-a" --overlay-ports="$PSScriptRoot/../e2e-ports/duplicate-file-b"
Throw-IfNotFailed
Restore-Problem-Matchers

# Test CI baseline early filtering functionality
# Test that ports marked as skip in ci.baseline.txt are excluded early from CI
$Output = Run-VcpkgAndCaptureOutput ci --dry-run --triplet=$Triplet --x-builtin-ports-root="$PSScriptRoot/../e2e-ports/ci-feature-baseline" --binarysource=clear --ci-baseline="$PSScriptRoot/../e2e-assets/ci/ci-baseline-test.txt"
Throw-IfFailed
# skip-dep should be marked as skip in the output (verifies it appears AND has skip status)
if (-not ($Output -match "skip-dep:${Triplet}:\s+skip:")) {
    throw 'skip-dep should be marked as skip in ci.baseline.txt and appear in output'
}
# base-dep should be in the installation list (not skipped)
if (-not ($Output -match "base-dep:${Triplet}@")) {
    throw 'base-dep should be in the installation list'
}
# skip-dep should NOT be in the installation list
if ($Output -match "skip-dep:${Triplet}@") {
    throw 'skip-dep should NOT be in the installation list (marked as skip)'
}

# Test that without CI baseline, both ports are included
$Output2 = Run-VcpkgAndCaptureOutput ci --dry-run --triplet=$Triplet --x-builtin-ports-root="$PSScriptRoot/../e2e-ports/ci-feature-baseline" --binarysource=clear
Throw-IfFailed
# Both ports should be in the installation list (may have features like [core,feature-to-skip])
if (-not ($Output2 -match "base-dep(\[.+?\])?:${Triplet}@")) {
    throw 'base-dep should be in the installation list without baseline'
}
if (-not ($Output2 -match "skip-dep(\[.+?\])?:${Triplet}@")) {
    throw 'skip-dep should be in the installation list without baseline'
}
