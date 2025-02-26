. $PSScriptRoot/../end-to-end-tests-prelude.ps1

$vcpkgDir = Join-Path -Path $installRoot -ChildPath "vcpkg"
$manifestInfoPath = Join-Path -Path $vcpkgDir -ChildPath "manifest-info.json"

# Tests a simple project with overlay ports and triplets configured on a vcpkg-configuration.json file
Copy-Item -Recurse -LiteralPath @(
    "$PSScriptRoot/../e2e-projects/overlays-malformed-shadowing",
    "$PSScriptRoot/../e2e-projects/overlays-malformed-shadowing-builtin",
    "$PSScriptRoot/../e2e-projects/overlays-project-config-embedded",
    "$PSScriptRoot/../e2e-projects/overlays-project-with-config"
    ) $TestingRoot

$manifestRoot = "$TestingRoot/overlays-project-with-config"
$env:VCPKG_OVERLAY_PORTS = "$manifestRoot/env-overlays"

Run-Vcpkg install --x-manifest-root=$manifestRoot `
    --overlay-ports=$manifestRoot/cli-overlays `
    --overlay-triplets=$manifestRoot/my-triplets `
    --x-install-root=$installRoot `
    --triplet fancy-triplet
Throw-IfFailed
Test-ManifestInfo -ManifestInfoPath $ManifestInfoPath -VcpkgDir $vcpkgDir -ManifestRoot $manifestRoot

# Tests overlays configured in env and cli on a project with configuration embedded on the manifest file
$manifestRoot = "$TestingRoot/overlays-project-config-embedded"
$env:VCPKG_OVERLAY_PORTS = "$manifestRoot/env-overlays"
Run-Vcpkg install --x-manifest-root=$manifestRoot `
    --overlay-ports=$manifestRoot/cli-overlays `
    --overlay-triplets=$manifestRoot/my-triplets `
    --x-install-root=$installRoot `
    --triplet fancy-config-embedded-triplet
Throw-IfFailed
Test-ManifestInfo -ManifestInfoPath $ManifestInfoPath -VcpkgDir $vcpkgDir -ManifestRoot $manifestRoot

# ... and with command line overlay-ports being 'dot'
pushd "$manifestRoot/cli-overlays"
try {
    Run-Vcpkg install --x-manifest-root=$manifestRoot `
    --overlay-ports=. `
    --overlay-triplets=$manifestRoot/my-triplets `
    --x-install-root=$installRoot `
    --triplet fancy-config-embedded-triplet
    Throw-IfFailed
} finally {
    popd
}

# Config with bad paths
$manifestRoot = "$PSScriptRoot/../e2e-projects/overlays-bad-paths"
$env:VCPKG_OVERLAY_PORTS = "$manifestRoot/env_overlays"
Run-Vcpkg install --x-manifest-root=$manifestRoot `
    --overlay-triplets=$manifestRoot/my-triplets `
    --x-install-root=$installRoot
Throw-IfNotFailed
Remove-Item env:VCPKG_OVERLAY_PORTS

# Test that once an overlay port is loaded for a name, subsequent ports are not considered
$manifestRoot = "$TestingRoot/overlays-malformed-shadowing"
Run-Vcpkg install --x-manifest-root=$manifestRoot
Throw-IfFailed

# ... even if that subsequent port is the builtin root
$manifestRoot = "$TestingRoot/overlays-malformed-shadowing-builtin"
Run-Vcpkg install --x-manifest-root=$manifestRoot --x-builtin-ports-root "$manifestRoot/builtin-malformed"
Throw-IfFailed

# Test overlay_triplet paths remain relative to the manifest root after x-update-baseline
$manifestRoot = "$TestingRoot/overlays-project-with-config"
$configurationBefore = Get-Content "$manifestRoot/vcpkg-configuration.json" | ConvertFrom-Json
Run-Vcpkg x-update-baseline --x-manifest-root=$manifestRoot
$configurationAfter = Get-Content "$manifestRoot/vcpkg-configuration.json" | ConvertFrom-Json

$overlaysBefore = $configurationBefore."overlay-triplets"
$overlaysAfter = $configurationAfter."overlay-triplets"

$notEqual = @(Compare-Object $overlaysBefore $overlaysAfter -SyncWindow 0).Length -ne 0

if ($notEqual) {
    Throw "Overlay triplets paths changed after x-update-baseline"
}

# Test that trying to declare overlay-ports as '.' fails
$manifestRoot = "$PSScriptRoot/../e2e-projects/overlays-dot"
$output = Run-VcpkgAndCaptureStdErr install --x-manifest-root=$manifestRoot --x-install-root=$installRoot
Throw-IfNotFailed
Throw-IfNonContains -Actual $output -Expected @"
error: The manifest directory cannot be the same as a directory configured in overlay-ports, so "overlay-ports" values cannot be ".".
"@

# Test that trying to declare overlay-ports as the same directory in a roundabout way fails
$manifestRoot = "$PSScriptRoot/../e2e-projects/overlays-not-quite-dot"
$canonicalManifestRoot = (Get-Item $manifestRoot).FullName
$output = Run-VcpkgAndCaptureStdErr install --x-manifest-root=$manifestRoot --x-install-root=$installRoot
Throw-IfNotFailed
Throw-IfNonContains -Actual $output -Expected @"
The manifest directory ($canonicalManifestRoot) cannot be the same as a directory configured in overlay-ports.
"@

# Test that removals can happen without the overlay triplets
Refresh-TestRoot
Run-Vcpkg install another-vcpkg-empty-port:fancy-triplet `
    --overlay-ports=$PSScriptRoot/../e2e-projects/overlays-project-with-config/cli-overlays `
    --overlay-triplets=$PSScriptRoot/../e2e-projects/overlays-project-with-config/my-triplets
Throw-IfFailed

Run-Vcpkg remove another-vcpkg-empty-port:fancy-triplet `
    --overlay-ports=$PSScriptRoot/../e2e-projects/overlays-project-with-config/cli-overlays
Throw-IfFailed

# ... or ports
Refresh-TestRoot
Run-Vcpkg install another-vcpkg-empty-port:fancy-triplet `
    --overlay-ports=$PSScriptRoot/../e2e-projects/overlays-project-with-config/cli-overlays `
    --overlay-triplets=$PSScriptRoot/../e2e-projects/overlays-project-with-config/my-triplets
Throw-IfFailed

Run-Vcpkg remove another-vcpkg-empty-port:fancy-triplet `
    --overlay-triplets=$PSScriptRoot/../e2e-projects/overlays-project-with-config/my-triplets
Throw-IfFailed

# ... or either
Refresh-TestRoot
Run-Vcpkg install another-vcpkg-empty-port:fancy-triplet `
    --overlay-ports=$PSScriptRoot/../e2e-projects/overlays-project-with-config/cli-overlays `
    --overlay-triplets=$PSScriptRoot/../e2e-projects/overlays-project-with-config/my-triplets
Throw-IfFailed

Run-Vcpkg remove another-vcpkg-empty-port:fancy-triplet
Throw-IfFailed
