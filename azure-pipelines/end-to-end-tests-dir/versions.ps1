. $PSScriptRoot/../end-to-end-tests-prelude.ps1

$versionFilesPathSources = "$PSScriptRoot/../e2e-ports/version-files"
$versionFilesPath = "$TestingRoot/version-files"

function Refresh-VersionFiles() {
    Refresh-TestRoot
    Copy-Item -Recurse $versionFilesPathSources $versionFilesPath
    git -C $versionFilesPath @gitConfigOptions init
    git -C $versionFilesPath @gitConfigOptions add -A
    git -C $versionFilesPath @gitConfigOptions commit -m testing
}

Refresh-VersionFiles

# Ensure transitive packages can be used even if they add version constraints
$CurrentTest = "transitive constraints without baseline"
Run-Vcpkg install @commonArgs --dry-run `
    "--x-builtin-ports-root=$versionFilesPath/transitive-constraints/ports" `
    "--x-manifest-root=$versionFilesPath/transitive-constraints"
Throw-IfFailed

# Test verify versions
mkdir $VersionFilesRoot | Out-Null
Copy-Item -Recurse "$versionFilesPath/versions-incomplete" $VersionFilesRoot
$portsRedirectArgsOK = @(
    "--feature-flags=versions",
    "--x-builtin-ports-root=$versionFilesPath/ports",
    "--x-builtin-registry-versions-dir=$versionFilesPath/versions"
)
$portsRedirectArgsIncomplete = @(
    "--feature-flags=versions",
    "--x-builtin-ports-root=$versionFilesPath/ports-incomplete",
    "--x-builtin-registry-versions-dir=$VersionFilesRoot/versions-incomplete"
)
$CurrentTest = "x-verify-ci-versions (All files OK)"
Write-Host $CurrentTest
Run-Vcpkg @portsRedirectArgsOK x-ci-verify-versions --verbose
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
Run-Vcpkg @portsRedirectArgsIncomplete x-add-version fish
Throw-IfNotFailed
$CurrentTest = "x-add-version fish --overwrite-version"
Run-Vcpkg @portsRedirectArgsIncomplete x-add-version fish --overwrite-version --skip-version-format-check
Throw-IfFailed

$CurrentTest = "x-add-version mouse"
# Missing baseline entry
Run-Vcpkg @portsRedirectArgsIncomplete x-add-version mouse
Throw-IfFailed

# Validate changes
Run-Vcpkg @portsRedirectArgsIncomplete x-ci-verify-versions --verbose
Throw-IfFailed

# Validate port-version
$CurrentTest = "x-add-version octopus"
Set-EmptyTestPort -Name octopus -Version 1.0 -PortVersion "1" -PortsRoot "$versionFilesPath/ports"
$output = Run-VcpkgAndCaptureOutput @portsRedirectArgsOK x-add-version octopus
Throw-IfNotFailed
if ($output -notmatch @"
warning: In octopus, 1.0 is a completely new version, so there should be no "port-version". Remove "port-version" and try again. To skip this check, rerun with --skip-version-format-check .
"@) {
    throw "Expected detecting present port-version when a new version is added as bad"
}

Run-Vcpkg @portsRedirectArgsOK x-add-version octopus --skip-version-format-check
Throw-IfFailed
git -C $versionFilesPath @gitConfigOptions add -A
git -C $versionFilesPath @gitConfigOptions commit -m "add octopus 1.0#1"

Set-EmptyTestPort -Name octopus -Version 2.0 -PortVersion "1" -PortsRoot "$versionFilesPath/ports"
$output = Run-VcpkgAndCaptureOutput @portsRedirectArgsOK x-add-version octopus
Throw-IfNotFailed
if ($output -notmatch @"
warning: In octopus, 2.0 is a completely new version, so there should be no "port-version". Remove "port-version" and try again. To skip this check, rerun with --skip-version-format-check .
"@) {
    throw "Expected detecting present port-version when a new version is added as bad"
}

Run-Vcpkg @portsRedirectArgsOK x-add-version octopus --skip-version-format-check
Throw-IfFailed
git -C $versionFilesPath @gitConfigOptions add -A
git -C $versionFilesPath @gitConfigOptions commit -m "add octopus 2.0#1"

Set-EmptyTestPort -Name octopus -Version 2.0 -PortVersion "3" -PortsRoot "$versionFilesPath/ports"
$output = Run-VcpkgAndCaptureOutput @portsRedirectArgsOK x-add-version octopus
Throw-IfNotFailed
if ($output -notmatch @"
warning: In octopus, the current "port-version" for 2.0 is 1, so the expected new "port-version" is 2, but the port declares "port-version" 3. Change "port-version" to 2 and try again. To skip this check, rerun with --skip-version-format-check .
"@) {
    throw "Expected detecting present port-version when a new version is added as bad"
}

Run-Vcpkg @portsRedirectArgsOK x-add-version octopus --skip-version-format-check
Throw-IfFailed
git -C $versionFilesPath @gitConfigOptions add -A
git -C $versionFilesPath @gitConfigOptions commit -m "add octopus 2.0#3"

$CurrentTest = "default baseline"
$out = Run-VcpkgAndCaptureOutput @commonArgs "--feature-flags=versions" install --x-manifest-root=$versionFilesPath/default-baseline-1
Throw-IfNotFailed
if ($out -notmatch ".*error: while checking out baseline\.*")
{
    throw "Expected to fail due to missing baseline"
}

$CurrentTest = "mismatched version database"
$out = Run-VcpkgAndCaptureOutput @commonArgs "--feature-flags=versions" install --x-manifest-root="$PSScriptRoot/../e2e-ports/mismatched-version-database"
Throw-IfNotFailed
if (($out -notmatch ".*error: Failed to load port because versions are inconsistent*") -or
  ($out -notmatch ".*version database indicates that it should be arrow@6.0.0.20210925#4.*") -or
  ($out -notmatch ".*contains the version arrow@6.0.0.20210925.*"))
{
    throw "Expected to fail due to mismatched versions between portfile and the version database"
}

Write-Trace "testing baselines"
Copy-Item -Recurse "$versionFilesPath/old-ports/zlib-1.2.11-8" "$versionFilesPath/ports/zlib"
Run-Vcpkg @portsRedirectArgsOK x-add-version zlib
Throw-IfFailed
git -C $versionFilesPath @gitConfigOptions add -A
git -C $versionFilesPath @gitConfigOptions commit -m "set zlib-1.2.11-8"
$baselineSha = git -C $versionFilesPath @gitConfigOptions rev-parse HEAD
Remove-Item -Recurse -Force -LiteralPath "$versionFilesPath/ports/zlib"
Copy-Item -Recurse "$versionFilesPath/old-ports/zlib-1.2.11-9" "$versionFilesPath/ports/zlib"
Run-Vcpkg @portsRedirectArgsOK x-add-version zlib
Throw-IfFailed
git -C $versionFilesPath @gitConfigOptions add -A
git -C $versionFilesPath @gitConfigOptions commit -m "set zlib-1.2.11-9"

$CurrentTest = "without default baseline 2 -- enabling versions should not change behavior"
Remove-Item -Recurse $buildtreesRoot/versioning_ -ErrorAction SilentlyContinue
Run-Vcpkg @commonArgs "--feature-flags=versions" install `
    "--dry-run" `
    "--x-manifest-root=$versionFilesPath/without-default-baseline-2" `
    "--x-builtin-registry-versions-dir=$versionFilesPath/versions"
Throw-IfFailed
Require-FileNotExists $buildtreesRoot/versioning_

$CurrentTest = "default baseline 2"
$baselinedVcpkgJson = @"
{
  "name": "default-baseline-test-2",
  "version-string": "0",
  "builtin-baseline": "$baselineSha",
  "dependencies": [
    "zlib"
  ]
}
"@

$defaultBaseline2 = "$TestingRoot/default-baseline-2"
if (Test-Path $defaultBaseline2) {
    Remove-Item -Recurse -Force -LiteralPath $defaultBaseline2 | Out-Null
}

New-Item -ItemType Directory -Force $defaultBaseline2 | Out-Null
Set-Content -LiteralPath "$defaultBaseline2/vcpkg.json" -Value $baselinedVcpkgJson -NoNewline -Encoding Ascii

Run-Vcpkg @commonArgs "--feature-flags=versions" install `
    "--dry-run" `
    "--x-manifest-root=$defaultBaseline2" `
    "--x-builtin-registry-versions-dir=$versionFilesPath/versions"
Throw-IfFailed
Require-FileExists $buildtreesRoot/versioning_

$CurrentTest = "using version features fails without flag"
Run-Vcpkg @commonArgs "--feature-flags=-versions" install `
    "--dry-run" `
    "--x-manifest-root=$defaultBaseline2" `
    "--x-builtin-registry-versions-dir=$versionFilesPath/versions"
Throw-IfNotFailed
