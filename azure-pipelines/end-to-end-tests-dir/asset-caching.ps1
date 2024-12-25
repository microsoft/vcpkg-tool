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
"Downloading .*"
"Successfully downloaded .*."
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
if (-not ($actual.Contains("[DEBUG] External asset downloads are blocked (x-block-origin is enabled)") -and $actual.Contains("[DEBUG] Asset caching is enabled."))) {
    throw "Failure: couldn't find expected debug promises (asset caching enabled + x-block-origin enabled)"
}

# Testing asset caching && x-block-orgin promises when --debug is passed (disabled)
Refresh-TestRoot
$actual = Run-VcpkgAndCaptureOutput -TestArgs ($commonArgs + @("install", "vcpkg-internal-e2e-test-port", "--overlay-ports=$PSScriptRoot/../e2e-ports", "--x-asset-sources=clear", "--downloads-root=$DownloadsRoot", "--debug"))
if (-not ($actual.Contains("[DEBUG] External asset downloads are allowed (x-block-origin is disabled)") -and $actual.Contains("[DEBUG] Asset cache is not configured"))) {
    throw "Failure: couldn't find expected debug promises (asset caching disabled + x-block-origin disabled)"
}

# azurl (no), x-block-origin (no), asset-cache (n/a), download (fail)
# Expected: Download failure message, nothing about asset caching
Refresh-TestRoot
$actual = Run-VcpkgAndCaptureOutput -TestArgs ($commonArgs + @("x-download", "$downloadsRoot/example3.html", "--sha512", "d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73a", "--url", "https://localhost:1234/foobar.html"))
if (-not ($actual.Contains("error: https://localhost:1234/foobar.html: curl failed to download with exit code 7"))) {
    throw "Failure: azurl (no), x-block-origin (no), asset-cache (n/a), download (fail)"
}

#azurl (no), x-block-origin (no), asset-cache (n/a), download (sha-mismatch)
#Expected: Download message with the "you might need to configure a proxy" message and with expected/actual sha
Refresh-TestRoot
$actual = Run-VcpkgAndCaptureOutput -TestArgs ($commonArgs + @("x-download", "$downloadsRoot/example3.html", "--sha512", "d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73b", "--url", "https://example.com"))
if (-not ($actual.Contains("Failed to download example3.html.") -and
          $actual.Contains("If you are using a proxy, please ensure your proxy settings are correct.") -and
          $actual.Contains("error: File does not have the expected hash:") -and
          $actual.Contains("url: https://example.com") -and
          $actual.Contains("Expected hash: d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73b") -and
          $actual.Contains("Actual hash: d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73a"))) {
    throw "Failure: azurl (no), x-block-origin (no), asset-cache (n/a), download (sha-mismatch)"
}

# azurl (no), x-block-origin (no), asset-cache (n/a), download (succeed)
# Expected: Download success message, nothing about asset caching
Refresh-TestRoot
$actual = Run-VcpkgAndCaptureOutput -TestArgs ($commonArgs + @("x-download", "$downloadsRoot/example3.html", "--sha512", "d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73a", "--url", "https://example.com"))
if (-not ($actual.Contains("Downloading example3.html") -and
          $actual.Contains("Successfully downloaded example3.html."))) {
    throw "Failure: azurl (no), x-block-origin (no), asset-cache (n/a), download (succeed)"
}

# azurl (no), x-block-origin (yes), asset-cache (n/a), download (n/a)
# Expected: Download failure message, nothing about asset caching, x-block-origin complaint
Refresh-TestRoot
$actual = Run-VcpkgAndCaptureOutput -TestArgs ($commonArgs + @("x-download", "$downloadsRoot/example3.html", "--sha512", "d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73a", "--url", "https://example.com", "--x-asset-sources=clear;x-block-origin"))
if (-not ($actual.Contains("error: Missing example3.html and downloads are blocked by x-block-origin."))) {
    throw "Failure: azurl (no), x-block-origin (yes), asset-cache (n/a), download (n/a)"
}

# azurl (yes), x-block-origin (no), asset-cache (miss), download (fail)
# Expected: Download failure message, asset cache named, nothing about x-block-origin
Refresh-TestRoot
$actual = Run-VcpkgAndCaptureOutput -TestArgs ($commonArgs + @("x-download", "$downloadsRoot/example3.html", "--sha512", "d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73a", "--url", "https://localhost:1234/foobar.html", "--x-asset-sources=x-azurl,file://$AssetCache,,readwrite"))
if (-not ($actual.Contains("Asset cache miss; downloading from https://localhost:1234/foobar.html") -and
          $actual.Contains("Downloading example3.html") -and
          $actual.Contains("error: file://$AssetCache") -and
          $actual.Contains("curl failed to download with exit code 37"))) {
    throw "Failure: azurl (yes), x-block-origin (no), asset-cache (miss), download (fail)"
}

# azurl (yes), x-block-origin (no), asset-cache (hit), download (n/a)
# Expected: Download success message, asset cache named, nothing about x-block-origin
Refresh-TestRoot
$actual = $actual -replace "`r`n", "`n"
Run-VcpkgAndCaptureOutput -TestArgs ($commonArgs + @("x-download", "$downloadsRoot/example3.html", "--sha512", "d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73a", "--url", "https://example.com", "--x-asset-sources=x-azurl,file://$AssetCache,,readwrite"))
$actual = Run-VcpkgAndCaptureOutput -TestArgs ($commonArgs + @("x-download", "$downloadsRoot/example3.html", "--sha512", "d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73a", "--url", "https://example.com", "--x-asset-sources=x-azurl,file://$AssetCache,,readwrite"))
if (-not ($actual.Contains("Asset cache hit for example3.html; downloaded from: file://$AssetCache"))) {
    throw "Failure: azurl (yes), x-block-origin (no), asset-cache (hit), download (n/a)"
}

# azurl (yes), x-block-origin (no), asset-cache (miss), download (sha-mismatch)
# Expected: Download message with "you might need to configure a proxy" and expected/actual sha
Refresh-TestRoot
$actual = Run-VcpkgAndCaptureOutput -TestArgs ($commonArgs + @("x-download", "$downloadsRoot/example3.html", "--sha512", "d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73b", "--url", "https://example.com", "--x-asset-sources=x-azurl,file://$AssetCache,,readwrite"))
if (-not ($actual.Contains("Asset cache miss; downloading from https://example.com") -and
          $actual.Contains("Downloading example3.html") -and
          $actual.Contains("error: file://$AssetCache") -and
          $actual.Contains("curl failed to download with exit code 37") -and
          $actual.Contains("error: File does not have the expected hash:") -and
          $actual.Contains("url: https://example.com") -and
          $actual.Contains("Expected hash: d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73b") -and
          $actual.Contains("Actual hash: d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73a"))) {
    throw "Failure: azurl (yes), x-block-origin (no), asset-cache (miss), download (sha-mismatch)"
}

# azurl (yes), x-block-origin (no), asset-cache (miss), download (succeed)
# Expected: Download success message, asset cache upload, nothing about x-block-origin
Refresh-TestRoot
$actual = Run-VcpkgAndCaptureOutput -TestArgs ($commonArgs + @("x-download", "$downloadsRoot/example3.html", "--sha512", "d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73a", "--url", "https://example.com", "--x-asset-sources=x-azurl,file://$AssetCache,,readwrite"))
if (-not ($actual.Contains("Asset cache miss; downloading from https://example.com") -and
          $actual.Contains("Downloading example3.html") -and
          $actual.Contains("Successfully downloaded example3.html.") -and
          $actual.Contains("Successfully stored example3.html to file://$AssetCache"))) {
    throw "Failure: azurl (yes), x-block-origin (no), asset-cache (miss), download (succeed)"
}

# azurl (yes), x-block-origin (yes), asset-cache (miss), download (n/a)
# Expected: Download failure message, which asset cache was tried, x-block-origin complaint
Refresh-TestRoot
$actual = Run-VcpkgAndCaptureOutput -TestArgs ($commonArgs + @("x-download", "$downloadsRoot/example3.html", "--sha512", "d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73a", "--url", "https://example.com", "--x-asset-sources=x-azurl,file://$AssetCache,,readwrite;x-block-origin"))
if (-not ($actual.Contains("Asset cache miss for example3.html and downloads are blocked by x-block-origin.") -and
          $actual.Contains("error: Missing example3.html and downloads are blocked by x-block-origin."))) {
    throw "Failure: azurl (yes), x-block-origin (yes), asset-cache (miss), download (n/a)"
}

# azurl (yes), x-block-origin (yes), asset-cache (hit), download (n/a)
# Expected: Download success message, asset cache named, nothing about x-block-origin
Refresh-TestRoot
Run-VcpkgAndCaptureOutput -TestArgs ($commonArgs + @("x-download", "$downloadsRoot/example3.html", "--sha512", "d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73a", "--url", "https://example.com", "--x-asset-sources=x-azurl,file://$AssetCache,,readwrite"))
$actual = Run-VcpkgAndCaptureOutput -TestArgs ($commonArgs + @("x-download", "$downloadsRoot/example3.html", "--sha512", "d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73a", "--url", "https://example.com", "--x-asset-sources=x-azurl,file://$AssetCache,,readwrite;x-block-origin"))
if (-not ($actual.Contains("Asset cache hit for example3.html; downloaded from: file://$AssetCache"))) {
    throw "Failure: azurl (yes), x-block-origin (yes), asset-cache (hit), download (n/a)"
}

# Testing x-download failure with asset cache (x-script) and x-block-origin settings
$env:X_VCPKG_ASSET_SOURCES = "clear;x-script,pwsh $PSScriptRoot/../e2e-assets/asset-caching/failing-script.ps1 {url} {sha512} {dst};x-block-origin"
$actual = Run-VcpkgAndCaptureOutput -TestArgs ($commonArgs + @("x-download", "$downloadsRoot/example3.html", "--url", "https://example.com", "--sha512", "d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73a"))
# Check for the expected messages in order
$expectedOrder = @(
    "error: <mirror-script> failed with exit code: (1).",
    "error: Missing example3.html and downloads are blocked by x-block-origin."
)

# Verify order
$index = 0
foreach ($message in $expectedOrder) {
    $index = $actual.IndexOf($message, $index)
    if ($index -lt 0) {
        throw "Failure: Expected message '$message' not found in the correct order."
    }
    $index += $message.Length
}

# Testing x-download success with asset cache (x-script) and x-block-origin settings
Refresh-TestRoot
$env:X_VCPKG_ASSET_SOURCES = "clear;x-script,$TestScriptAssetCacheExe {url} {sha512} {dst};x-block-origin"
$actual = Run-VcpkgAndCaptureOutput -TestArgs ($commonArgs + @("x-download", "$downloadsRoot/example3.html", "--url", "https://example.com/hello-world.txt", "--sha512", "09e1e2a84c92b56c8280f4a1203c7cffd61b162cfe987278d4d6be9afbf38c0e8934cdadf83751f4e99d111352bffefc958e5a4852c8a7a29c95742ce59288a8"))
if (-not ($actual.Contains("Successfully downloaded example3.html."))) {
    throw "Failure: x-script download success message"
}

