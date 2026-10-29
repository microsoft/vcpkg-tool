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

function Get-VersionFileSnapshot([string]$Root) {
    return (Get-ChildItem -LiteralPath $Root -Recurse -File |
        Sort-Object FullName |
        ForEach-Object {
            $relativePath = $_.FullName.Substring($Root.Length).TrimStart('\', '/')
            $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash
            "$relativePath`:$hash"
        }) -join "`n"
}

Refresh-VersionFiles

# Ensure transitive packages can be used even if they add version constraints
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
# Verify a complete versions database
Run-Vcpkg @portsRedirectArgsOK x-ci-verify-versions --verbose
Throw-IfFailed

# x-add-version --all removes stale baseline entries
Set-Content -LiteralPath "$versionFilesPath/versions/baseline.json" -Value @"
{
  "default": {
    "cat": {
      "baseline": "1.0",
      "port-version": 0
    },
    "dog": {
      "baseline": "2001-01-01",
      "port-version": 0
    },
    "duck": {
      "baseline": "mallard",
      "port-version": 0
    },
    "mouse": {
      "baseline": "1.0.0",
      "port-version": 0
    },
    "stale-entry": {
      "baseline": "9.9.9",
      "port-version": 0
    }
  }
}
"@ -Encoding Ascii -NoNewline
Run-Vcpkg @portsRedirectArgsOK x-add-version --all
Throw-IfFailed
Require-JsonFileEquals "$versionFilesPath/versions/baseline.json" @"
{
  "default": {
    "cat": {
      "baseline": "1.0",
      "port-version": 0
    },
    "dog": {
      "baseline": "2001-01-01",
      "port-version": 0
    },
    "duck": {
      "baseline": "mallard",
      "port-version": 0
    },
    "mouse": {
      "baseline": "1.0.0",
      "port-version": 0
    }
  }
}
"@

# x-add-version --all creates a missing baseline
Remove-Item -LiteralPath "$versionFilesPath/versions/baseline.json"
Run-Vcpkg @portsRedirectArgsOK x-add-version --all
Throw-IfFailed
Require-JsonFileEquals "$versionFilesPath/versions/baseline.json" @"
{
  "default": {
    "cat": {
      "baseline": "1.0",
      "port-version": 0
    },
    "dog": {
      "baseline": "2001-01-01",
      "port-version": 0
    },
    "duck": {
      "baseline": "mallard",
      "port-version": 0
    },
    "mouse": {
      "baseline": "1.0.0",
      "port-version": 0
    }
  }
}
"@

# x-add-version --all recreates a missing versions directory
Remove-Item -Recurse -Force -LiteralPath "$versionFilesPath/versions"
Run-Vcpkg @portsRedirectArgsOK x-add-version --all
Throw-IfFailed
Require-JsonFileEquals "$versionFilesPath/versions/baseline.json" @"
{
  "default": {
    "cat": {
      "baseline": "1.0",
      "port-version": 0
    },
    "dog": {
      "baseline": "2001-01-01",
      "port-version": 0
    },
    "duck": {
      "baseline": "mallard",
      "port-version": 0
    },
    "mouse": {
      "baseline": "1.0.0",
      "port-version": 0
    }
  }
}
"@
Remove-Item -Recurse -Force -LiteralPath "$versionFilesPath/versions"
Copy-Item -Recurse "$versionFilesPathSources/versions" "$versionFilesPath/versions"

# Reject an incomplete versions database
Run-Vcpkg @portsRedirectArgsIncomplete x-ci-verify-versions --verbose
Throw-IfNotFailed

# A failed x-add-version invocation must not change any version files
$versionFileSnapshotBefore = Get-VersionFileSnapshot "$VersionFilesRoot/versions-incomplete"
Run-Vcpkg @portsRedirectArgsIncomplete x-add-version dog fish
Throw-IfNotFailed
$versionFileSnapshotAfter = Get-VersionFileSnapshot "$VersionFilesRoot/versions-incomplete"
Throw-IfNonEqual $versionFileSnapshotAfter $versionFileSnapshotBefore

# Do not fail if there's nothing to update
Run-Vcpkg @portsRedirectArgsIncomplete x-add-version cat
Throw-IfFailed

# Local version is not in baseline and versions file
Run-Vcpkg @portsRedirectArgsIncomplete x-add-version dog
Throw-IfFailed

# Missing versions file
Run-Vcpkg @portsRedirectArgsIncomplete x-add-version duck
Throw-IfFailed

# Missing versions file and missing baseline entry
Run-Vcpkg @portsRedirectArgsIncomplete x-add-version ferret
Throw-IfFailed

# Discrepancy between local SHA and SHA in fish.json. Requires --overwrite-version.
Run-Vcpkg @portsRedirectArgsIncomplete x-add-version fish
Throw-IfNotFailed

# --overwrite-version replaces the recorded git tree when the port contents change
Run-Vcpkg @portsRedirectArgsIncomplete x-add-version fish --overwrite-version --skip-version-format-check
Throw-IfFailed

# Missing baseline entry
Run-Vcpkg @portsRedirectArgsIncomplete x-add-version mouse
Throw-IfFailed

# Validate changes
Run-Vcpkg @portsRedirectArgsIncomplete x-ci-verify-versions --verbose
Throw-IfFailed

# Validate port-version
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

# Changing only the version scheme must be picked up by --overwrite-version
Set-EmptyTestPort -Name squid -Version 1.0.0 -PortsRoot "$versionFilesPath/ports"
Run-Vcpkg @portsRedirectArgsOK x-add-version squid
Throw-IfFailed
git -C $versionFilesPath @gitConfigOptions add -A
git -C $versionFilesPath @gitConfigOptions commit -m "add squid 1.0.0"

function Get-SquidVersionEntry {
    return (Get-Content -Raw -LiteralPath "$versionFilesPath/versions/s-/squid.json" | ConvertFrom-Json).versions[0]
}

$squidJson = @"
{
  "name": "squid",
  "version-semver": "1.0.0"
}

"@
Set-Content -Value $squidJson -LiteralPath "$versionFilesPath/ports/squid/vcpkg.json" -Encoding Ascii -NoNewline
git -C $versionFilesPath @gitConfigOptions add -A
git -C $versionFilesPath @gitConfigOptions commit -m "squid uses version-semver"

# Reject a version scheme change without --overwrite-version
Run-Vcpkg @portsRedirectArgsOK x-add-version squid
Throw-IfNotFailed

# --overwrite-version updates both the git tree and version scheme
Run-Vcpkg @portsRedirectArgsOK x-add-version squid --overwrite-version
Throw-IfFailed
$squidEntry = Get-SquidVersionEntry
if ($squidEntry.'version-semver' -ne '1.0.0' -or $null -ne $squidEntry.version) {
    throw "Expected --overwrite-version to record squid 1.0.0 as version-semver"
}

# The git tree is already correct and only the recorded scheme is stale
$squidVersionsFile = "$versionFilesPath/versions/s-/squid.json"
$staleSchemeContent = (Get-Content -Raw -LiteralPath $squidVersionsFile).Replace('"version-semver"', '"version"')
Set-Content -Value $staleSchemeContent -LiteralPath $squidVersionsFile -NoNewline -Encoding Ascii

# Reject a stale version scheme even when the git tree matches
Run-Vcpkg @portsRedirectArgsOK x-add-version squid
Throw-IfNotFailed

# --overwrite-version repairs the version scheme even when the git tree is unchanged
Run-Vcpkg @portsRedirectArgsOK x-add-version squid --overwrite-version
Throw-IfFailed
$squidEntry = Get-SquidVersionEntry
if ($squidEntry.'version-semver' -ne '1.0.0' -or $null -ne $squidEntry.version) {
    throw "Expected --overwrite-version to fix the stale version scheme of squid 1.0.0"
}

git -C $versionFilesPath @gitConfigOptions add -A
git -C $versionFilesPath @gitConfigOptions commit -m "squid version database uses version-semver"

# Reject a builtin baseline that cannot be checked out
$out = Run-VcpkgAndCaptureOutput @commonArgs "--feature-flags=versions" install --x-manifest-root=$versionFilesPath/default-baseline-1
Throw-IfNotFailed
if ($out -notmatch ".*error: while checking out baseline\.*")
{
    throw "Expected to fail due to missing baseline"
}

# Reject a port whose version differs from its version database entry
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

# Enabling versions without a builtin baseline must not change behavior
Remove-Item -Recurse $buildtreesRoot/versioning_ -ErrorAction SilentlyContinue
Run-Vcpkg @commonArgs "--feature-flags=versions" install `
    "--dry-run" `
    "--x-manifest-root=$versionFilesPath/without-default-baseline-2" `
    "--x-builtin-registry-versions-dir=$versionFilesPath/versions"
Throw-IfFailed
Require-FileNotExists $buildtreesRoot/versioning_

# A builtin baseline enables versioned dependency resolution
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

# Reject versioning features when the versions feature flag is disabled
Run-Vcpkg @commonArgs "--feature-flags=-versions" install `
    "--dry-run" `
    "--x-manifest-root=$defaultBaseline2" `
    "--x-builtin-registry-versions-dir=$versionFilesPath/versions"
Throw-IfNotFailed
