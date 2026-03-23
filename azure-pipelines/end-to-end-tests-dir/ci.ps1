. $PSScriptRoot/../end-to-end-tests-prelude.ps1

# test skipped ports
$Output = Run-VcpkgAndCaptureOutput ci --dry-run --triplet=$Triplet --x-builtin-ports-root="$PSScriptRoot/../e2e-ports/ci"  --binarysource=clear --ci-baseline="$PSScriptRoot/../e2e-assets/ci/ci.baseline.txt"
Throw-IfNotFailed
$ErrorOutput = Run-VcpkgAndCaptureStdErr ci --dry-run --triplet=$Triplet --x-builtin-ports-root="$PSScriptRoot/../e2e-ports/ci"  --binarysource=clear --ci-baseline="$PSScriptRoot/../e2e-assets/ci/ci.baseline.txt"
Throw-IfNotFailed
# dep-on-feature-not-sup must cascade because it depends on a features that is not supported
Throw-IfNonContains -Actual $Output -Expected "dep-on-feature-not-sup:${Triplet}: cascade"
# not-sup-host-b must be skipped because it is not supported
Throw-IfNonContains -Actual $Output -Expected "not-sup-host-b:${Triplet}: unsupported"
# feature-not-sup must be built because the port that causes this port to skip should not be installed
Throw-IfNonContains -Actual $Output -Expected "feature-not-sup:${Triplet}:      *:"
# feature-dep-missing must be built because the broken feature is not selected.
Throw-IfNonContains -Actual $Output -Expected "feature-dep-missing:${Triplet}:      *:"
if ($Output.Split("*").Length -ne 4) {
    throw 'base-port should not be installed for the host'
}
Throw-IfNonContains -Actual $Output -Expected @"
SUMMARY FOR $Triplet
  CASCADED_DUE_TO_MISSING_DEPENDENCIES: 1
  SKIPPED_BY_DRY_RUN: 3
  UNSUPPORTED: 1
"@
# feature-not-sup's baseline fail entry should result in a regression because the port is not supported
Throw-IfNonContains -Actual $ErrorOutput -Expected "REGRESSION: not-sup-host-b:${Triplet} is marked as fail but not supported for ${Triplet}."
# feature-not-sup's baseline fail entry should result in a regression because the port is cascade for this triplet
Throw-IfNonContains -Actual $ErrorOutput -Expected "REGRESSION: dep-on-feature-not-sup:${Triplet} is marked as fail but one dependency is not supported for ${Triplet}."

# pass means pass
Run-Vcpkg ci --dry-run --triplet=$Triplet --x-builtin-ports-root="$PSScriptRoot/../e2e-ports/ci"  --binarysource=clear --ci-baseline="$PSScriptRoot/../e2e-assets/ci/ci.baseline.txt"
Throw-IfNotFailed
$ErrorOutput = Run-VcpkgAndCaptureStdErr ci --dry-run --triplet=$Triplet --x-builtin-ports-root="$PSScriptRoot/../e2e-ports/ci"  --binarysource=clear --ci-baseline="$PSScriptRoot/../e2e-assets/ci/ci.pass.baseline.txt"
Throw-IfNotFailed
# feature-not-sup's baseline pass entry should result in a regression because the port is not supported
Throw-IfNonContains -Actual $ErrorOutput -Expected "REGRESSION: not-sup-host-b:${Triplet} is marked as pass but not supported for ${Triplet}."
# feature-not-sup's baseline pass entry should result in a regression because the port is cascade for this triplet
Throw-IfNonContains -Actual $ErrorOutput -Expected "REGRESSION: dep-on-feature-not-sup:${Triplet} cascaded, but it is required to pass. ("

# any invalid manifest must raise an error
Remove-Problem-Matchers
Run-Vcpkg ci --dry-run --triplet=$Triplet --x-builtin-ports-root="$PSScriptRoot/../e2e-ports/broken-manifests"  --binarysource=clear --ci-baseline="$PSScriptRoot/../e2e-assets/ci/ci.baseline.txt"
Restore-Problem-Matchers
Throw-IfNotFailed

# test malformed individual overlay port manifest
Remove-Problem-Matchers
$Output = Run-VcpkgAndCaptureOutput ci --dry-run --triplet=$Triplet --x-builtin-ports-root="$PSScriptRoot/../e2e-ports/ci"  --binarysource=clear --ci-baseline="$PSScriptRoot/../e2e-assets/ci/ci.baseline.txt" --overlay-ports="$PSScriptRoot/../e2e-ports/broken-manifests/malformed"
Restore-Problem-Matchers
Throw-IfNotFailed
# malformed port manifest must raise a parsing error
Throw-IfNonContains -Actual $Output -Expected "vcpkg.json:3:17: error: Trailing comma"

# test malformed overlay port manifests
Remove-Problem-Matchers
$Output = Run-VcpkgAndCaptureOutput ci --dry-run --triplet=$Triplet --x-builtin-ports-root="$PSScriptRoot/../e2e-ports/ci"  --binarysource=clear --ci-baseline="$PSScriptRoot/../e2e-assets/ci/ci.baseline.txt" --overlay-ports="$PSScriptRoot/../e2e-ports/broken-manifests"
Restore-Problem-Matchers
Throw-IfNotFailed
# malformed overlay manifest must raise a parsing error
Throw-IfNonContains -Actual $Output -Expected "vcpkg.json:3:17: error: Trailing comma"

# test that file conflicts are detected
Remove-Item -Recurse -Force $installRoot -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $installRoot -Force | Out-Null
Remove-Problem-Matchers
$emptyDir = "$TestingRoot/empty"
New-Item -ItemType Directory -Path $emptyDir -Force | Out-Null
$Output = Run-VcpkgAndCaptureOutput ci @commonArgs --x-builtin-ports-root="$emptyDir" --binarysource=clear --overlay-ports="$PSScriptRoot/../e2e-ports/duplicate-file-a" --overlay-ports="$PSScriptRoot/../e2e-ports/duplicate-file-b"
Restore-Problem-Matchers
Throw-IfNotFailed

# test effect of parent hashes
Refresh-TestRoot
$Output = Run-VcpkgAndCaptureOutput ci --dry-run @commonArgs --x-builtin-ports-root="$PSScriptRoot/../e2e-ports/ci" --binarysource=clear --output-hashes="$TestingRoot/parent-hashes.json"
Throw-IfFailed
Throw-IfNonContains -Actual $Output -Expected "base-port:${Triplet}:      *: "
Throw-IfNonContains -Actual $Output -Expected @"
The following packages will be built and installed:
    base-port:${Triplet}@1
    feature-dep-missing:${Triplet}@1
    feature-not-sup:${Triplet}@1
"@
$Output = Run-VcpkgAndCaptureOutput ci --dry-run @commonArgs --x-builtin-ports-root="$PSScriptRoot/../e2e-ports/ci" --binarysource=clear --parent-hashes="$TestingRoot/parent-hashes.json"
Throw-IfFailed
# base-port must not be rebuilt again
Throw-IfNonContains -Actual $Output -Expected "base-port:${Triplet}: parent: "
Throw-IfNonContains -Actual $Output -Expected @"
SUMMARY FOR $Triplet
  CASCADED_DUE_TO_MISSING_DEPENDENCIES: 1
  SKIPPED_BY_PARENT_HASHES: 3
  UNSUPPORTED: 1
"@

# test that skipped ports aren't "put back" by downstream dependencies that aren't skipped,
# and that self-unsupported cases are listed as unsupported
Refresh-TestRoot
$Output = Run-VcpkgAndCaptureOutput ci @commonArgs --x-builtin-ports-root="$PSScriptRoot/../e2e-assets/ci-skipped-ports" --binarysource="clear;files,$ArchiveRoot,readwrite" --ci-baseline="$PSScriptRoot/../e2e-assets/ci-skipped-ports/baseline.skip.txt"
Throw-IfFailed
Throw-IfNonContains -Actual $Output -Expected "always-built:$($Triplet):      *:"
if (-not ($Output -match 'Building always-built:[^@]+@1\.0\.0\.\.\.')) {
    throw 'did not attempt to build always-built'
}
Throw-IfNonContains -Actual $Output -Expected "always-skip:$($Triplet): skip"
Throw-IfNonContains -Actual $Output -Expected "always-cascade:$($Triplet): cascade"
Throw-IfNonContains -Actual $Output -Expected "never-built-unsupported:$($Triplet): unsupported"
Throw-IfNonContains -Actual $Output -Expected "never-built-unsupported-feature:$($Triplet): unsupported"
Throw-IfNonContains -Actual $Output -Expected "never-built-unsupported-feature-default:$($Triplet): unsupported"
Throw-IfNonContains -Actual $Output -Expected "never-built-unsupported-host:$($Triplet): unsupported"
Throw-IfNonContains -Actual $Output -Expected "never-built-unsupported-host-feature:$($Triplet): unsupported"
# prerequisite for next tests
Throw-IfNonContains -Actual $Output -Expected "maybe-transitive-cascade:$($Triplet): skip"
Throw-IfNonContains -Actual $Output -Expected "maybe-cross-cascade:$($Triplet):      *:"
if (-not ($Output -match 'Building maybe-cross-cascade:[^@]+@1\.0\.0\.\.\.')) {
    throw 'did not attempt to build maybe-cross-cascade'
}
Throw-IfNonContains -Actual $Output -Expected @"
SUMMARY FOR $Triplet
  SUCCEEDED: 4
  CASCADED_DUE_TO_MISSING_DEPENDENCIES: 1
  SKIPPED: 2
  UNSUPPORTED: 5
"@
# test with --skip-failures and cached artifacts
Remove-Item -Recurse -Force $installRoot -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $installRoot -Force | Out-Null
$Output = Run-VcpkgAndCaptureOutput ci --skip-failures @commonArgs --x-builtin-ports-root="$PSScriptRoot/../e2e-assets/ci-skipped-ports" --binarysource="clear;files,$ArchiveRoot" --ci-baseline="$PSScriptRoot/../e2e-assets/ci-skipped-ports/baseline.fail.txt"
Throw-IfFailed
Throw-IfNonContains -Actual $Output -Expected "always-built:$($Triplet): cached:"
if ($Output -match 'Building always-built:[^@]+@1\.0\.0\.\.\.') {
    throw 'did not reuse the cached artifact for always-built'
}
Throw-IfNonContains -Actual $Output -Expected "always-skip:$($Triplet): skip"
Throw-IfNonContains -Actual $Output -Expected "always-cascade:$($Triplet): cascade"
Throw-IfNonContains -Actual $Output -Expected "maybe-skip:$($Triplet): skip"
# not cached and transitive dependency on maybe-skip which is skipped
Throw-IfNonContains -Actual $Output -Expected "maybe-transitive-cascade:$($Triplet): cascade"
# cached, but direct dependency on maybe-skip which is skipped
Throw-IfNonContains -Actual $Output -Expected "maybe-cross-cascade:$($Triplet): cascade"
Throw-IfNonContains -Actual $Output -Expected "never-built-unsupported:$($Triplet): unsupported"
Throw-IfNonContains -Actual $Output -Expected "never-built-unsupported-feature:$($Triplet): unsupported"
Throw-IfNonContains -Actual $Output -Expected "never-built-unsupported-feature-default:$($Triplet): unsupported"
Throw-IfNonContains -Actual $Output -Expected "never-built-unsupported-host:$($Triplet): unsupported"
Throw-IfNonContains -Actual $Output -Expected "never-built-unsupported-host-feature:$($Triplet): unsupported"
Throw-IfNonContains -Actual $Output -Expected @"
SUMMARY FOR $Triplet
  CASCADED_DUE_TO_MISSING_DEPENDENCIES: 4
  SKIPPED: 2
  UNSUPPORTED: 5
  CACHED: 1
"@
# test cross build; skipping always-skip and host maybe-skip
Copy-Item $tripletFile "$TestingRoot/cross.cmake"
$Output = Run-VcpkgAndCaptureOutput ci --dry-run --skip-failures --triplet cross --overlay-triplets $TestingRoot @directoryArgs --x-builtin-ports-root="$PSScriptRoot/../e2e-assets/ci-skipped-ports" --binarysource=clear --ci-baseline="$PSScriptRoot/../e2e-assets/ci-skipped-ports/baseline.fail.txt"
Throw-IfFailed
Throw-IfNonContains -Actual $Output -Expected 'always-built:cross:      *:'
Throw-IfNonContains -Actual $Output -Expected 'always-cascade:cross: cascade'
Throw-IfNonContains -Actual $Output -Expected 'maybe-skip:cross:      *:'
Throw-IfNonContains -Actual $Output -Expected 'maybe-transitive-cascade:cross:      *:'
Throw-IfNonContains -Actual $Output -Expected 'maybe-cross-cascade:cross: cascade'
Throw-IfNonContains -Actual $Output -Expected 'never-built-unsupported:cross: unsupported'
Throw-IfNonContains -Actual $Output -Expected 'never-built-unsupported-feature:cross: unsupported'
Throw-IfNonContains -Actual $Output -Expected 'never-built-unsupported-feature-default:cross: unsupported'
Throw-IfNonContains -Actual $Output -Expected 'never-built-unsupported-host:cross: unsupported'
Throw-IfNonContains -Actual $Output -Expected 'never-built-unsupported-host-feature:cross: unsupported'
Throw-IfNonContains -Actual $Output -Expected @"
SUMMARY FOR cross
  CASCADED_DUE_TO_MISSING_DEPENDENCIES: 2
  SKIPPED: 1
  SKIPPED_BY_DRY_RUN: 4
  UNSUPPORTED: 5
"@

# test that features included only by skipped ports are not included
Refresh-TestRoot
$xunitFile = Join-Path $TestingRoot 'xunit.xml'
Refresh-TestRoot
Remove-Problem-Matchers
$Output = Run-VcpkgAndCaptureOutput ci @commonArgs --x-builtin-ports-root="$PSScriptRoot/../e2e-assets/ci-skipped-features" --binarysource=clear --ci-baseline="$PSScriptRoot/../e2e-assets/ci-skipped-features/baseline.txt" --x-xunit-all --x-xunit="$xunitFile"
Restore-Problem-Matchers
Throw-IfFailed
Throw-IfNonContains -Actual $Output -Expected "skipped-features:$($Triplet):      *:"
if (-not ($Output -match 'Building skipped-features:[^@]+@1\.0\.0\.\.\.')) {
    throw 'did not attempt to build skipped-features'
} 
Throw-IfContains -Actual $Output -Expected 'Building skipped-depends'
Throw-IfNonContains -Actual $Output -Expected @"
SUMMARY FOR $Triplet
  SUCCEEDED: 1
  CASCADED_DUE_TO_MISSING_DEPENDENCIES: 1
  SKIPPED: 1
"@

$xunitContent = Get-Content $xunitFile -Raw
$expected = @"
<\?xml version="1\.0" encoding="utf-8"\?><assemblies>
  <assembly name="skipped-depends" run-date="\d\d\d\d-\d\d-\d\d" run-time="\d\d:\d\d:\d\d" time="0">
    <collection name="$Triplet" time="0">
      <test name="skipped-depends:$Triplet" method="skipped-depends:$Triplet" time="0" result="Skip">
        <traits>
          <trait name="owner" value="$Triplet"/>
        </traits>
        <reason><!\[CDATA\[SKIPPED\]\]></reason>
      </test>
    </collection>
  </assembly>
  <assembly name="skipped-features" run-date="\d\d\d\d-\d\d-\d\d" run-time="\d\d:\d\d:\d\d" time="0">
    <collection name="$Triplet" time="0">
      <test name="skipped-features:$Triplet" method="skipped-features\[core\]:$Triplet" time="0" result="Pass">
        <traits>
          <trait name="abi_tag" value="[^"]+"/>
          <trait name="features" value="core"/>
          <trait name="owner" value="$Triplet"/>
        </traits>
      </test>
    </collection>
  </assembly>
  <assembly name="unskipped-cascade" run-date="1970-01-01" run-time="00:00:00" time="0">
    <collection name="$Triplet" time="0">
      <test name="unskipped-cascade:$Triplet" method="unskipped-cascade:$Triplet" time="0" result="Skip">
        <traits>
          <trait name="owner" value="$Triplet"/>
        </traits>
        <reason><!\[CDATA\[CASCADED_DUE_TO_MISSING_DEPENDENCIES\]\]></reason>
      </test>
    </collection>
  </assembly>
</assemblies>

"@

if (-not ($xunitContent -match $expected)) {
    Write-Diff -Actual $xunitContent -Expected $expected
    throw 'xUnit output did not match expected output'
}
