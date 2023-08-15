. $PSScriptRoot/../end-to-end-tests-prelude.ps1

# test skipped ports
$Output = Run-VcpkgAndCaptureOutput ci --dry-run --triplet=$Triplet --x-builtin-ports-root="$PSScriptRoot/../e2e-ports/ci"  --binarysource=clear --ci-baseline="$PSScriptRoot/../e2e-assets/ci/ci.baseline.txt"
Throw-IfFailed
if (-not ($Output.Contains("dep-on-feature-not-sup:${Triplet}:  cascade"))) {
    throw 'dep-on-feature-not-sup must cascade because it depends on a features that is not supported'
}
if (-not ($Output.Contains("not-sup-host-b:${Triplet}:     skip"))) {
    throw 'not-sup-host-b must be skipped because it is not supported'
}
if (-not ($Output.Contains("feature-not-sup:${Triplet}:        *"))) {
    throw 'feature-not-sup must be build because the port that causes this port to skip should not be installed'
}
if ($Output.Split("*").Length -ne 3) {
    throw 'base-port should not be installed for the host'
}
if (-not ($Output.Contains("REGRESSION: not-sup-host-b:${Triplet} is marked as fail but not supported for ${Triplet}."))) {
    throw "feature-not-sup's baseline fail entry should result in a regression because the port is not supported"
}
if (-not ($Output.Contains("REGRESSION: dep-on-feature-not-sup:${Triplet} is marked as fail but one dependency is not supported for ${Triplet}."))) {
    throw "feature-not-sup's baseline fail entry should result in a regression because the port is cascade for this triplet"
}
