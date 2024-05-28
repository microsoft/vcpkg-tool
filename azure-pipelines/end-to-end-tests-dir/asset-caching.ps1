. $PSScriptRoot/../end-to-end-tests-prelude.ps1

# Testing x-script
Refresh-TestRoot
Run-Vcpkg -TestArgs ($commonArgs + @('fetch', 'cmake'))
Run-Vcpkg -TestArgs ($commonArgs + @("install", "vcpkg-test-x-script", "--x-binarysource=clear", "--overlay-ports=$PSScriptRoot/../e2e-ports", "--x-asset-sources=x-script,$TestScriptAssetCacheExe {url} {sha512} {dst};x-block-origin"))
Throw-IfFailed

# Test Asset Cache not configured + x-block-origin enabled
Refresh-TestRoot
$actual = Run-VcpkgAndCaptureOutput -TestArgs ($commonArgs + @("install", "vcpkg-internal-e2e-test-port", "--overlay-ports=$PSScriptRoot/../e2e-ports", "--x-asset-sources=clear;x-block-origin", "--downloads-root=$DownloadsRoot"))

$expected = @(
"Computing installation plan..."
"A suitable version of .* was not found \(required v[0-9\.]+\)."
"error: Failed to download .*."
) -join "`n"

if (-not ($actual -match $expected)) {
    throw "Asset cache not configured + x-block-origin enabled failed"
}

# Test Asset Cache not configured + x-block-origin disabled
$actual = Run-VcpkgAndCaptureOutput -TestArgs ($commonArgs + @("install", "vcpkg-internal-e2e-test-port", "--overlay-ports=$PSScriptRoot/../e2e-ports", "--x-asset-sources=clear;", "--downloads-root=$DownloadsRoot"))

$expected = @(
"Computing installation plan..."
"A suitable version of .* was not found \(required v[0-9\.]+\)."
"Downloading .*"
"Extracting .*..."
) -join "`n"

if (-not ($actual -match $expected)) {
    throw "Asset cache not configured + x-block-origin disabled failed"
}

# Test Asset Cache Miss + x-block-origin enabled
Refresh-TestRoot
$actual = Run-VcpkgAndCaptureOutput -TestArgs ($commonArgs + @("install", "vcpkg-internal-e2e-test-port", "--overlay-ports=$PSScriptRoot/../e2e-ports", "--x-asset-sources=x-azurl,file://$AssetCache,,readwrite;x-block-origin", "--downloads-root=$DownloadsRoot"))

$expected = @(
"Computing installation plan..."
"A suitable version of .* was not found \(required v[0-9\.]+\)."
"Asset cache miss for .*."
"error: Failed to download .*"
) -join "`n"

if (-not ($actual -match $expected)) {
    throw "Asset cache miss + x-block-origin enabled failed"
}

# Test Asset Cache Miss + x-block-origin disabled
$actual = Run-VcpkgAndCaptureOutput -TestArgs ($commonArgs + @("install", "vcpkg-internal-e2e-test-port", "--overlay-ports=$PSScriptRoot/../e2e-ports", "--x-asset-sources=x-azurl,file://$AssetCache,,readwrite;", "--downloads-root=$DownloadsRoot"))

$expected = @(
"Computing installation plan..."
"A suitable version of .* was not found \(required v[0-9\.]+\)."
"Asset cache miss for .*."
"Downloading .*"
"Successfully stored .* to mirror."
"Extracting .*..."
) -join "`n"

if (-not ($actual -match $expected)) {
    throw "Asset cache miss + x-block-origin disabled failed"
}

# Test Asset Cache Hit
Refresh-Downloads
Run-Vcpkg -TestArgs ($commonArgs + @('remove', 'vcpkg-internal-e2e-test-port'))
$actual = Run-VcpkgAndCaptureOutput -TestArgs ($commonArgs + @("install", "vcpkg-internal-e2e-test-port", "--overlay-ports=$PSScriptRoot/../e2e-ports", "--x-asset-sources=x-azurl,file://$AssetCache,,readwrite;", "--downloads-root=$DownloadsRoot"))
$expected = @(
"Computing installation plan..."
"A suitable version of .* was not found \(required v[0-9\.]+\)."
"Asset cache hit for .*."
"Downloading: .*"
"Extracting .*..."
) -join "`n"

if (-not ($actual -match $expected)) {
    throw "Asset cache hit failed"
}

# Test asset caching && x-block-orgin promises when --debug is passed (enabled)
Refresh-TestRoot
$actual = Run-VcpkgAndCaptureOutput -TestArgs ($commonArgs + @("install", "vcpkg-internal-e2e-test-port", "--overlay-ports=$PSScriptRoot/../e2e-ports", "--x-asset-sources=x-azurl,file://$AssetCache,,readwrite;x-block-origin", "--downloads-root=$DownloadsRoot", "--debug"))
# Define the regex pattern that accounts for multiline input
$expectedPattern = "(?s)" +
                   ".*\[DEBUG\] External asset downloads are blocked \(x-block-origin is enabled\)\.\.?" +
                   ".*\[DEBUG\] Asset caching is enabled\..*"

if (-not ($actual -match $expectedPattern)) {
    throw "Test failed: Debug messages mismatch"
}

# Test asset caching && x-block-orgin promises when --debug is passed (disabled)
Refresh-TestRoot
$actual = Run-VcpkgAndCaptureOutput -TestArgs ($commonArgs + @("install", "vcpkg-internal-e2e-test-port", "--overlay-ports=$PSScriptRoot/../e2e-ports", "--x-asset-sources=clear", "--downloads-root=$DownloadsRoot", "--debug"))
$expectedPattern = "(?s)" +
                   ".*\[DEBUG\] External asset downloads are allowed \(x-block-origin is disabled\)\.\.\.?" +
                   ".*\[DEBUG\] Asset cache is not configured.*"

if (-not ($actual -match $expectedPattern)) {
    throw "Test failed: Debug messages mismatch"
}