. $PSScriptRoot/../end-to-end-tests-prelude.ps1

$env:X_VCPKG_REGISTRIES_CACHE = Join-Path $TestingRoot 'registries'
New-Item -ItemType Directory -Force $env:X_VCPKG_REGISTRIES_CACHE | Out-Null

# =====================================================================
# Helper: write a minimal empty test port
# =====================================================================
function New-TestPort {
    param(
        [string]$PortsRoot,
        [string]$Name,
        [string]$Version
    )
    $portDir = Join-Path $PortsRoot $Name
    New-Item -Path $portDir -ItemType Directory -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $portDir 'portfile.cmake') `
        -Value 'set(VCPKG_POLICY_EMPTY_PACKAGE enabled)' -Encoding Ascii
    Set-Content -LiteralPath (Join-Path $portDir 'vcpkg.json') `
        -Value "{`"name`": `"$Name`", `"version`": `"$Version`"}" -Encoding Ascii
}

# =====================================================================
# Builtin registry tests use two real vcpkg release tags so that:
#  - ref resolution is exercised against real tags in VCPKG_ROOT
#  - the expected version change is stable and documented
#
# curl changed 8.11.0 -> 8.11.1 between the 2024.11.16 and 2024.12.16
# releases (https://github.com/microsoft/vcpkg/releases).
# =====================================================================
Write-Trace "Resolve vcpkg release tags to SHAs"
$oldTag = '2024.11.16'
$newTag = '2024.12.16'
# builtin-baseline in vcpkg.json must be a full 40-char SHA.
$oldSha = git -C $env:VCPKG_ROOT rev-parse $oldTag
Throw-IfFailed
$newSha = git -C $env:VCPKG_ROOT rev-parse $newTag
Throw-IfFailed

# Feature flag needed because the manifest carries builtin-baseline.
$builtinArgs = @('--feature-flags=versions')

# Manifest: depends on curl, whose version changed between the two releases.
$manifestDir = "$TestingRoot/manifest-builtin"
New-Item -Path $manifestDir -ItemType Directory | Out-Null
$manifestDir = (Get-Item $manifestDir).FullName
Set-Content -LiteralPath "$manifestDir/vcpkg.json" -Encoding Ascii -Value @"
{
    "name": "baseline-diff-test",
    "version-string": "0",
    "builtin-baseline": "$oldSha",
    "dependencies": [ "curl" ]
}
"@

# =====================================================================
# Test 1: Builtin registry — two explicit SHAs, expect known curl update
# =====================================================================
Write-Trace "Test: builtin registry with two explicit SHAs"
$CurrentTest = 'x-baseline-diff builtin two SHAs'
$Output = Run-VcpkgAndCaptureOutput x-baseline-diff @commonArgs @builtinArgs `
    "--x-manifest-root=$manifestDir" $oldSha $newSha
Throw-IfFailed
Throw-IfNonContains -Actual $Output -Expected 'curl'
Throw-IfNonContains -Actual $Output -Expected '8.11.0'
Throw-IfNonContains -Actual $Output -Expected '8.11.1'

# =====================================================================
# Test 2: Builtin registry — one SHA, manifest builtin-baseline as old
# =====================================================================
Write-Trace "Test: builtin registry with one SHA (manifest provides old baseline)"
$CurrentTest = 'x-baseline-diff builtin one SHA'
$Output = Run-VcpkgAndCaptureOutput x-baseline-diff @commonArgs @builtinArgs `
    "--x-manifest-root=$manifestDir" $newSha
Throw-IfFailed
Throw-IfNonContains -Actual $Output -Expected 'curl'
Throw-IfNonContains -Actual $Output -Expected '8.11.0'
Throw-IfNonContains -Actual $Output -Expected '8.11.1'

# =====================================================================
# Test 3: Builtin registry — real release tags as arguments (ref resolution)
# No temporary tags needed; the release tags already exist in VCPKG_ROOT.
# =====================================================================
Write-Trace "Test: builtin registry with release tags (ref resolution)"
$CurrentTest = 'x-baseline-diff builtin with tags'
$Output = Run-VcpkgAndCaptureOutput x-baseline-diff @commonArgs @builtinArgs `
    "--x-manifest-root=$manifestDir" $oldTag $newTag
Throw-IfFailed
Throw-IfNonContains -Actual $Output -Expected 'curl'
Throw-IfNonContains -Actual $Output -Expected '8.11.0'
Throw-IfNonContains -Actual $Output -Expected '8.11.1'

# =====================================================================
# Test 4: Builtin registry — same tag twice, expect no changes
# =====================================================================
Write-Trace "Test: builtin registry — same baseline, no changes"
$CurrentTest = 'x-baseline-diff builtin no change'
$Output = Run-VcpkgAndCaptureOutput x-baseline-diff @commonArgs @builtinArgs `
    "--x-manifest-root=$manifestDir" $newTag $newTag
Throw-IfFailed
Throw-IfContains -Actual $Output -Expected ' -> '
Throw-IfNonContains -Actual $Output -Expected 'No changes to installed packages between baselines'

# =====================================================================
# Test 5: Missing manifest — must fail
# =====================================================================
Write-Trace "Test: x-baseline-diff fails without a manifest"
$CurrentTest = 'x-baseline-diff no manifest'
$noManifestDir = "$TestingRoot/no-manifest-dir"
New-Item -Path $noManifestDir -ItemType Directory | Out-Null
Run-Vcpkg x-baseline-diff @commonArgs @builtinArgs "--x-manifest-root=$noManifestDir" $oldSha $newSha
Throw-IfNotFailed

# =====================================================================
# Test 6: One-arg mode fails when manifest has no builtin-baseline
# =====================================================================
Write-Trace "Test: one-arg mode fails when manifest has no builtin-baseline"
$manifestDirNoBaseline = "$TestingRoot/manifest-no-baseline"
New-Item -Path $manifestDirNoBaseline -ItemType Directory | Out-Null
Set-Content -LiteralPath "$manifestDirNoBaseline/vcpkg.json" -Encoding Ascii -Value @"
{
    "name": "baseline-diff-test",
    "version-string": "0",
    "dependencies": [ "zlib" ]
}
"@
$CurrentTest = 'x-baseline-diff one arg no manifest baseline'
Run-Vcpkg x-baseline-diff @commonArgs @builtinArgs `
    "--x-manifest-root=$manifestDirNoBaseline" $newSha
Throw-IfNotFailed

# =====================================================================
# Build a filesystem registry used as the DEFAULT registry.
# It carries two named baselines ("v1", "v2") with different versions
# of the same port — a supported but uncommon filesystem-registry
# feature (https://learn.microsoft.com/vcpkg/maintainers/registries
# #filesystem-registries).
#
# Port files live in versioned subdirectories so both versions are
# accessible simultaneously via the "path" entries.
# =====================================================================
Write-Trace "Build filesystem registry with two named baselines"
$filesystemRegistryRoot = "$TestingRoot/filesystem-registry"
New-Item -Path $filesystemRegistryRoot -ItemType Directory | Out-Null
$filesystemRegistryRoot = (Get-Item $filesystemRegistryRoot).FullName

New-TestPort -PortsRoot "$filesystemRegistryRoot/v1" -Name 'vcpkg-baseline-diff-test-a' -Version '1.0.0'
New-TestPort -PortsRoot "$filesystemRegistryRoot/v2" -Name 'vcpkg-baseline-diff-test-a' -Version '2.0.0'
New-Item -Path "$filesystemRegistryRoot/versions/v-" -ItemType Directory -Force | Out-Null
Set-Content -LiteralPath "$filesystemRegistryRoot/versions/baseline.json" -Encoding Ascii -Value @"
{
    "v1": {
        "vcpkg-baseline-diff-test-a": { "baseline": "1.0.0", "port-version": 0 }
    },
    "v2": {
        "vcpkg-baseline-diff-test-a": { "baseline": "2.0.0", "port-version": 0 }
    }
}
"@
Set-Content -LiteralPath "$filesystemRegistryRoot/versions/v-/vcpkg-baseline-diff-test-a.json" -Encoding Ascii -Value @"
{
    "versions": [
        { "version": "2.0.0", "port-version": 0, "path": "$/v2/vcpkg-baseline-diff-test-a" },
        { "version": "1.0.0", "port-version": 0, "path": "$/v1/vcpkg-baseline-diff-test-a" }
    ]
}
"@

# Manifest: plain dependency, no builtin-baseline needed.
# vcpkg-configuration.json sets the filesystem registry as default with
# "baseline": "v1" so that the 1-arg test can use it as the old baseline.
$manifestDirFs = "$TestingRoot/manifest-filesystem"
New-Item -Path $manifestDirFs -ItemType Directory | Out-Null
$manifestDirFs = (Get-Item $manifestDirFs).FullName

Set-Content -LiteralPath "$manifestDirFs/vcpkg.json" -Encoding Ascii -Value @"
{
    "name": "baseline-diff-test",
    "version-string": "0",
    "dependencies": [ "vcpkg-baseline-diff-test-a" ]
}
"@
Set-Content -LiteralPath "$manifestDirFs/vcpkg-configuration.json" -Encoding Ascii `
    -Value (ConvertTo-Json -Depth 5 @{
        "default-registry" = @{
            "kind" = "filesystem"
            "path" = $filesystemRegistryRoot
            "baseline" = "v1"
        }
    })

# =====================================================================
# Test 7: Filesystem registry as default — two named baselines
# =====================================================================
Write-Trace "Test: filesystem registry as default registry, two named baselines"
$CurrentTest = 'x-baseline-diff filesystem two baselines'
$Output = Run-VcpkgAndCaptureOutput x-baseline-diff @commonArgs '--feature-flags=registries' `
    "--x-manifest-root=$manifestDirFs" 'v1' 'v2'
Throw-IfFailed
Throw-IfNonContains -Actual $Output -Expected 'vcpkg-baseline-diff-test-a'
Throw-IfNonContains -Actual $Output -Expected '1.0.0'
Throw-IfNonContains -Actual $Output -Expected '2.0.0'

# =====================================================================
# Test 7b: Filesystem registry — one baseline arg, registry config
#           baseline used as old
# =====================================================================
Write-Trace "Test: filesystem registry, one arg (registry config baseline as old)"
$CurrentTest = 'x-baseline-diff filesystem one baseline'
$Output = Run-VcpkgAndCaptureOutput x-baseline-diff @commonArgs '--feature-flags=registries' `
    "--x-manifest-root=$manifestDirFs" 'v2'
Throw-IfFailed
Throw-IfNonContains -Actual $Output -Expected 'vcpkg-baseline-diff-test-a'
Throw-IfNonContains -Actual $Output -Expected '1.0.0'
Throw-IfNonContains -Actual $Output -Expected '2.0.0'

# =====================================================================
# Test 8: Git registry pointing at https://github.com/microsoft/vcpkg
# as the default registry — same SHAs as the builtin tests above since
# VCPKG_ROOT is a clone of that repo.  Also exercises the
# is_builtin_git_registry ref-resolution code path.
# =====================================================================
Write-Trace "Test: git registry (github.com/microsoft/vcpkg) as default registry"
$manifestDirGit = "$TestingRoot/manifest-git-registry"
New-Item -Path $manifestDirGit -ItemType Directory | Out-Null
$manifestDirGit = (Get-Item $manifestDirGit).FullName

Set-Content -LiteralPath "$manifestDirGit/vcpkg.json" -Encoding Ascii -Value @"
{
    "name": "baseline-diff-test",
    "version-string": "0",
    "dependencies": [ "curl" ]
}
"@
Set-Content -LiteralPath "$manifestDirGit/vcpkg-configuration.json" -Encoding Ascii `
    -Value (ConvertTo-Json -Depth 5 @{
        "default-registry" = @{
            "kind" = "git"
            "repository" = "https://github.com/microsoft/vcpkg"
            "baseline" = $oldSha
        }
    })

$CurrentTest = 'x-baseline-diff git registry two SHAs'
$Output = Run-VcpkgAndCaptureOutput x-baseline-diff @commonArgs '--feature-flags=registries' `
    "--x-manifest-root=$manifestDirGit" $oldSha $newSha
Throw-IfFailed
Throw-IfNonContains -Actual $Output -Expected 'curl'
Throw-IfNonContains -Actual $Output -Expected '8.11.0'
Throw-IfNonContains -Actual $Output -Expected '8.11.1'
