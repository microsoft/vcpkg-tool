. $PSScriptRoot/../end-to-end-tests-prelude.ps1

# Testing x-script
Refresh-TestRoot
Run-Vcpkg -TestArgs ($commonArgs + @('fetch', 'cmake'))
Run-Vcpkg -TestArgs ($commonArgs + @("install", "vcpkg-test-x-script", "--x-binarysource=clear", "--overlay-ports=$PSScriptRoot/../e2e-ports", "--x-asset-sources=x-script,$TestScriptAssetCacheExe {url} {sha512} {dst};x-block-origin"))
Throw-IfFailed

$env:VCPKG_FORCE_DOWNLOADED_BINARIES = "ON"

# Testing asset cache miss (not configured) + x-block-origin enabled
Refresh-TestRoot
$actual = Run-VcpkgAndCaptureOutput -TestArgs ($commonArgs + @("install", "vcpkg-internal-e2e-test-port", "--overlay-ports=$PSScriptRoot/../e2e-ports", "--x-asset-sources=clear;x-block-origin", "--downloads-root=$DownloadsRoot"))
$actual = $actual -replace "`r`n", "`n"

$expected = @(
"A suitable version of .* was not found \(required v[0-9\.]+\)."
"error: Missing .* and downloads are blocked by x-block-origin."
) -join "`n"

if (-not ($actual -match $expected)) {
    throw "Failure: asset cache miss (not configured) + x-block-origin enabled"
}

# Testing asset cache miss (not configured) + x-block-origin disabled
$actual = Run-VcpkgAndCaptureOutput -TestArgs ($commonArgs + @("install", "vcpkg-internal-e2e-test-port", "--overlay-ports=$PSScriptRoot/../e2e-ports", "--x-asset-sources=clear;", "--downloads-root=$DownloadsRoot"))
$actual = $actual -replace "`r`n", "`n"

$expected = @(
"A suitable version of .* was not found \(required v[0-9\.]+\)."
"Downloading .*."
) -join "`n"

if (-not ($actual -match $expected)) {
    throw "Failure: asset cache miss (not configured) + x-block-origin disabled"
}

# Testing asset cache miss (configured) + x-block-origin enabled
Refresh-TestRoot
$actual = Run-VcpkgAndCaptureOutput -TestArgs ($commonArgs + @("install", "vcpkg-internal-e2e-test-port", "--overlay-ports=$PSScriptRoot/../e2e-ports", "--x-asset-sources=x-azurl,file://$AssetCache,,readwrite;x-block-origin", "--downloads-root=$DownloadsRoot"))
$actual = $actual -replace "`r`n", "`n"

$expected = @(
"A suitable version of .* was not found \(required v[0-9\.]+\)."
"Asset cache miss for .* and downloads are blocked by x-block-origin"
) -join "`n"

if (-not ($actual -match $expected)) {
    throw "Failure: asset cache miss (configured) + x-block-origin disabled"
}

# Testing asset cache miss (configured) + x-block-origin disabled
Refresh-TestRoot
$actual = Run-VcpkgAndCaptureOutput -TestArgs ($commonArgs + @("install", "vcpkg-internal-e2e-test-port", "--overlay-ports=$PSScriptRoot/../e2e-ports", "--x-asset-sources=x-azurl,file://$AssetCache,,readwrite;", "--downloads-root=$DownloadsRoot"))
$actual = $actual -replace "`r`n", "`n"

$expected = @(
"A suitable version of .* was not found \(required v[0-9\.]+\)."
"Asset cache miss; downloading from .*"
"Successfully stored .* to .*."
) -join "`n"

if (-not ($actual -match $expected)) {
    throw "Failure: asset cache miss (configured) + x-block-origin disabled"
}

# Testing asset cache hit
Refresh-Downloads
Run-Vcpkg -TestArgs ($commonArgs + @('remove', 'vcpkg-internal-e2e-test-port'))
$actual = Run-VcpkgAndCaptureOutput -TestArgs ($commonArgs + @("install", "vcpkg-internal-e2e-test-port", "--overlay-ports=$PSScriptRoot/../e2e-ports", "--x-asset-sources=x-azurl,file://$AssetCache,,readwrite;", "--downloads-root=$DownloadsRoot"))
$actual = $actual -replace "`r`n", "`n"

$expected = @(
"A suitable version of .* was not found \(required v[0-9\.]+\)."
"Asset cache hit for .*; downloaded from: .*"
) -join "`n"

if (-not ($actual -match $expected)) {
    throw "Failure: asset cache hit"
}

# Testing asset caching && x-block-orgin promises when --debug is passed (enabled)
Refresh-TestRoot
$actual = Run-VcpkgAndCaptureOutput -TestArgs ($commonArgs + @("install", "vcpkg-internal-e2e-test-port", "--overlay-ports=$PSScriptRoot/../e2e-ports", "--x-asset-sources=x-azurl,file://$AssetCache,,readwrite;x-block-origin", "--downloads-root=$DownloadsRoot", "--debug"))
$actual = $actual -replace "`r`n", "`n"

# Define the regex pattern that accounts for multiline input
$expectedPattern = "(?s)" +
                   ".*\[DEBUG\] External asset downloads are blocked \(x-block-origin is enabled\)\.\.\.?" +
                   ".*\[DEBUG\] Asset caching is enabled\..*"

if (-not ($actual -match $expectedPattern)) {
    throw "Failure: couldn't find expected debug promises (asset caching enabled + x-block-origin enabled)"
}

# Testing asset caching && x-block-orgin promises when --debug is passed (disabled)
Refresh-TestRoot
$actual = Run-VcpkgAndCaptureOutput -TestArgs ($commonArgs + @("install", "vcpkg-internal-e2e-test-port", "--overlay-ports=$PSScriptRoot/../e2e-ports", "--x-asset-sources=clear", "--downloads-root=$DownloadsRoot", "--debug"))
$actual = $actual -replace "`r`n", "`n"

$expectedPattern = "(?s)" +
                   ".*\[DEBUG\] External asset downloads are allowed \(x-block-origin is disabled\)\.\.\.?" +
                   ".*\[DEBUG\] Asset cache is not configured.*"

if (-not ($actual -match $expectedPattern)) {
    throw "Failure: couldn't find expected debug promises (asset caching disabled + x-block-origin disabled)"
}