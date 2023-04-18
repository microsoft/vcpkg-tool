. $PSScriptRoot/../end-to-end-tests-prelude.ps1

# Tests a simple project with overlay ports and triplets configured on a vcpkg-configuration.json file
$e2eProjects = "$PSScriptRoot/../e2e_projects"

$manifestRoot = "$e2eProjects/overlays-project-with-config"
$env:VCPKG_OVERLAY_PORTS = "$manifestRoot/env-overlays"

Run-Vcpkg install --x-manifest-root=$manifestRoot `
    --overlay-ports=$manifestRoot/cli-overlays `
    --overlay-triplets=$manifestRoot/my-triplets `
    --x-install-root=$installRoot
Throw-IfFailed

# Tests overlays configured in env and cli on a project with configuration embedded on the manifest file
$manifestRoot = "$e2eProjects/overlays-project-config-embedded"
$env:VCPKG_OVERLAY_PORTS = "$manifestRoot/env-overlays"
Run-Vcpkg install --x-manifest-root=$manifestRoot `
    --overlay-ports=$manifestRoot/cli-overlays `
    --overlay-triplets=$manifestRoot/my-triplets `
    --x-install-root=$installRoot
Throw-IfFailed

# Config with bad paths
$manifestRoot = "$e2eProjects/overlays-bad-paths"
$env:VCPKG_OVERLAY_PORTS = "$manifestRoot/env_overlays"
Run-Vcpkg install --x-manifest-root=$manifestRoot `
    --overlay-triplets=$manifestRoot/my-triplets `
    --x-install-root=$installRoot
Throw-IfNotFailed

# Test overlay_triplet paths remain relative to the manifest root after x-update-baseline
$manifestRoot = "$e2eProjects/overlays-project-with-config"
$overlayTripletsBeforeUpdate = Get-Content "$manifestRoot/vcpkg-configuration.json" | ConvertFrom-Json
Run-Vcpkg x-update-baseline --x-manifest-root=$manifestRoot
$overlayTripletsAfterUpdate = Get-Content "$manifestRoot/vcpkg-configuration.json" | ConvertFrom-Json

$sortedBeforeUpdate = ($overlayTripletsBeforeUpdate."overlay-triplets" | Sort-Object) -join ""
$sortedAfterUpdate = ($overlayTripletsAfterUpdate."overlay-triplets" | Sort-Object) -join ""

if ($sortedBeforeUpdate -ne $sortedAfterUpdate) {
	Throw "Overlay triplets paths changed after x-update-baseline"
}
