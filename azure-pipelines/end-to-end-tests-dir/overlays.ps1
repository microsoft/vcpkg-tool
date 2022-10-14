. $PSScriptRoot/../end-to-end-tests-prelude.ps1

# Tests a simple project with overlay ports and triplets configured on a vcpkg-configuration.json file
$e2eProjects = "$PSScriptRoot/../e2e_projects"

$manifestRoot = "$e2eProjects/overlays-project-with-config"
$env:VCPKG_OVERLAY_PORTS = "$manifestRoot/env-overlays"

Run-Vcpkg install --x-manifest-root=$manifestRoot --overlay-ports=$manifestRoot/cli-overlays --overlay-triplets=$manifestRoot/my-triplets

# Tests overlays configured in env and cli on a project with configuration embedded on the manifest file
$manifestRoot = "$e2eProjects/overlays-project-config-embedded"
$env:VCPKG_OVERLAY_PORTS = "$manifestRoot/env-overlays"
Run-Vcpkg install --x-manifest-root=$manifestRoot --overlay-ports=$manifestRoot/cli-overlays --overlay-triplets=$manifestRoot/my-triplets