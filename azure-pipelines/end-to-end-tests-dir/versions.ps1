. $PSScriptRoot/../end-to-end-tests-prelude.ps1

$versionFilesPath = "$PSScriptRoot/../e2e_ports/version-files"

# create custom registry for testing
$RegistryRootComplete = Join-Path $TestingRoot 'registry_for_versions_complete'
& git init $RegistryRootComplete
Throw-IfFailed
$RegistryRootIncomplete = Join-Path $TestingRoot 'registry_for_versions_incomplete'
& git init $RegistryRootIncomplete
Throw-IfFailed

# Test verify versions
Copy-Item -Recurse "$versionFilesPath/versions" "$RegistryRootComplete/versions"
Copy-Item -Recurse "$versionFilesPath/ports" "$RegistryRootComplete/ports"
& git -C "$RegistryRootComplete" add -A 
Throw-IfFailed
& git -C "$RegistryRootComplete" -c user.name='vcpkg-tool' -c user.email='ci@vcpkg.io' commit -m "add files"
Throw-IfFailed
Copy-Item -Recurse "$versionFilesPath/versions_incomplete" "$RegistryRootIncomplete/versions"
Copy-Item -Recurse "$versionFilesPath/ports_incomplete" "$RegistryRootIncomplete/ports"
& git -C "$RegistryRootIncomplete" add -A
Throw-IfFailed
& git -C "$RegistryRootIncomplete" -c user.name='vcpkg-tool' -c user.email='ci@vcpkg.io' commit -m "add files"
Throw-IfFailed
$portsRedirectArgsOK = @(
    "--feature-flags=versions",
    "--x-registry-root=$RegistryRootComplete"
)
$portsRedirectArgsIncomplete = @(
    "--feature-flags=versions",
    "--x-registry-root=$RegistryRootIncomplete"
)
$CurrentTest = "x-verify-ci-versions (All files OK)"
Write-Host $CurrentTest
Run-Vcpkg @portsRedirectArgsOK x-ci-verify-versions --verbose --debug
Throw-IfFailed

$CurrentTest = "x-verify-ci-versions (Incomplete)"
Run-Vcpkg @portsRedirectArgsIncomplete x-ci-verify-versions --verbose
Throw-IfNotFailed

$CurrentTest = "x-add-version cat"
# Do not fail if there's nothing to update
Run-Vcpkg @portsRedirectArgsIncomplete x-add-version cat
Throw-IfFailed

$CurrentTest = "x-add-version dog"
# Local version is not in baseline and versions file
Run-Vcpkg @portsRedirectArgsIncomplete x-add-version dog
Throw-IfFailed

$CurrentTest = "x-add-version duck"
# Missing versions file
Run-Vcpkg @portsRedirectArgsIncomplete x-add-version duck
Throw-IfFailed

$CurrentTest = "x-add-version ferret"
# Missing versions file and missing baseline entry
Run-Vcpkg @portsRedirectArgsIncomplete x-add-version ferret
Throw-IfFailed

$CurrentTest = "x-add-version fish (must fail)"
# Discrepancy between local SHA and SHA in fish.json. Requires --overwrite-version.
$out = Run-Vcpkg @portsRedirectArgsIncomplete x-add-version fish
Throw-IfNotFailed
$CurrentTest = "x-add-version fish --overwrite-version"
Run-Vcpkg @portsRedirectArgsIncomplete x-add-version fish --overwrite-version
Throw-IfFailed

$CurrentTest = "x-add-version mouse"
# Missing baseline entry
Run-Vcpkg @portsRedirectArgsIncomplete x-add-version mouse
Throw-IfFailed
# Validate changes
Run-Vcpkg @portsRedirectArgsIncomplete x-ci-verify-versions --verbose
Throw-IfFailed

$CurrentTest = "default baseline"
$out = Run-Vcpkg @commonArgs "--feature-flags=versions" install --x-manifest-root=$versionFilesPath/default-baseline-1 2>&1 | Out-String
Throw-IfNotFailed
if ($out -notmatch ".*Error: while checking out baseline.*")
{
    $out
    throw "Expected to fail due to missing baseline"
}

git -C "$env:VCPKG_ROOT" fetch https://github.com/vicroms/test-registries
foreach ($opt_registries in @("",",registries"))
{
    Write-Trace "testing baselines: $opt_registries"
    Refresh-TestRoot
    $CurrentTest = "without default baseline 2 -- enabling versions should not change behavior"
    Remove-Item -Recurse $buildtreesRoot/versioning -ErrorAction SilentlyContinue
    Run-Vcpkg @commonArgs "--feature-flags=versions$opt_registries" install `
        "--dry-run" `
        "--x-manifest-root=$versionFilesPath/without-default-baseline-2"
    Throw-IfFailed
    Require-FileNotExists $buildtreesRoot/versioning

    $CurrentTest = "default baseline 2"
    Run-Vcpkg @commonArgs "--feature-flags=versions$opt_registries" install `
        "--dry-run" `
        "--x-manifest-root=$versionFilesPath/default-baseline-2"
    Throw-IfFailed
    Require-FileExists $buildtreesRoot/versioning

    $CurrentTest = "using version features fails without flag"
    Run-Vcpkg @commonArgs "--feature-flags=-versions$opt_registries" install `
        "--dry-run" `
        "--x-manifest-root=$versionFilesPath/default-baseline-2"
    Throw-IfNotFailed
}
