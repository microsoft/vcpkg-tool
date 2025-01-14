. $PSScriptRoot/../end-to-end-tests-prelude.ps1

# Testing x-script
Refresh-TestRoot
Run-Vcpkg -TestArgs ($commonArgs + @('fetch', 'cmake'))
Run-Vcpkg -TestArgs ($commonArgs + @("install", "vcpkg-test-x-script", "--x-binarysource=clear", "--overlay-ports=$PSScriptRoot/../e2e-ports", "--x-asset-sources=x-script,$TestScriptAssetCacheExe {url} {sha512} {dst};x-block-origin"))
Throw-IfFailed

$env:VCPKG_FORCE_DOWNLOADED_BINARIES = "ON"
$assetCacheRegex = [regex]::Escape($AssetCache)

# Testing asset cache miss (not configured) + x-block-origin enabled
Refresh-TestRoot
$actual = Run-VcpkgAndCaptureOutput -TestArgs ($commonArgs + @("fetch", "cmake", "--x-asset-sources=clear;x-block-origin", "--downloads-root=$DownloadsRoot"))
Throw-IfNotFailed
$expected = @(
"A suitable version of cmake was not found \(required v[0-9.]+\)\.",
"Downloading cmake-[0-9.]+-[^.]+\.(zip|tar\.gz)",
"error: there were no asset cache hits, and x-block-origin blocks trying the authoritative source https://github\.com/Kitware/CMake/releases/download/[^ ]+"
) -join "`n"

if (-not ($actual -match $expected)) {
    throw "Failure: asset cache miss (not configured) + x-block-origin enabled"
}

# Testing asset cache miss (not configured) + x-block-origin disabled
$actual = Run-VcpkgAndCaptureOutput -TestArgs ($commonArgs + @("fetch", "cmake", "--x-asset-sources=clear;", "--downloads-root=$DownloadsRoot"))
Throw-IfFailed
$expected = @(
"A suitable version of cmake was not found \(required v[0-9.]+\)\.",
"Downloading https://github\.com/Kitware/CMake/releases/download/[^ ]+ -> cmake-[0-9.]+-[^.]+\.(zip|tar\.gz)",
"Successfully downloaded cmake-[0-9.]+-[^.]+\.(zip|tar\.gz)"
) -join "`n"

if (-not ($actual -match $expected)) {
    throw "Failure: asset cache miss (not configured) + x-block-origin disabled"
}

# Testing asset cache miss (configured) + x-block-origin enabled
Refresh-TestRoot
$actual = Run-VcpkgAndCaptureOutput -TestArgs ($commonArgs + @("fetch", "cmake", "--x-asset-sources=x-azurl,file://$AssetCache,,readwrite;x-block-origin", "--downloads-root=$DownloadsRoot"))
Throw-IfNotFailed
$expected = @(
"A suitable version of cmake was not found \(required v[0-9.]+\)\.",
"Trying to download cmake-[0-9.]+-[^.]+\.(zip|tar\.gz) using asset cache file://$assetCacheRegex/[0-9a-z]+",
"error: curl: \(37\) Couldn't open file [^\n]+",
"error: there were no asset cache hits, and x-block-origin blocks trying the authoritative source https://github\.com/Kitware/CMake/releases/download/[^ ]+",
"note: If you are using a proxy, please ensure your proxy settings are correct\.",
"Possible causes are:",
"1\. You are actually using an HTTP proxy, but setting HTTPS_PROXY variable to ``https//address:port``\.",
"This is not correct, because ``https://`` prefix claims the proxy is an HTTPS proxy, while your proxy \(v2ray, shadowsocksr, etc\.\.\.\) is an HTTP proxy\.",
"Try setting ``http://address:port`` to both HTTP_PROXY and HTTPS_PROXY instead\."
"2\. If you are using Windows, vcpkg will automatically use your Windows IE Proxy Settings set by your proxy software\. See: https://github\.com/microsoft/vcpkg-tool/pull/77",
"The value set by your proxy might be wrong, or have same ``https://`` prefix issue\.",
"3\. Your proxy's remote server is our of service\.",
"If you've tried directly download the link, and believe this is not a temporary download server failure, please submit an issue at https://github\.com/Microsoft/vcpkg/issues",
"to report this upstream download server failure\."
) -join "`n"

if (-not ($actual -match $expected)) {
    throw "Failure: asset cache miss (configured) + x-block-origin disabled"
}

# Testing asset cache miss (configured) + x-block-origin disabled
Refresh-TestRoot
$actual = Run-VcpkgAndCaptureOutput -TestArgs ($commonArgs + @("fetch", "cmake", "--x-asset-sources=x-azurl,file://$AssetCache,,readwrite;", "--downloads-root=$DownloadsRoot"))
Throw-IfFailed
$expected = @(
"A suitable version of cmake was not found \(required v[0-9\.]+\)\.",
"Trying to download cmake-[0-9.]+-[^.]+\.(zip|tar\.gz) using asset cache file://$assetCacheRegex/[0-9a-f]+"
"Asset cache miss; trying authoritative source https://github\.com/Kitware/CMake/releases/download/[^ ]+",
"Successfully downloaded cmake-[0-9.]+-[^.]+\.(zip|tar\.gz), storing to file://$assetCacheRegex/[0-9a-f]+",
"Store success"
) -join "`n"

if (-not ($actual -match $expected)) {
    throw "Failure: asset cache miss (configured) + x-block-origin disabled"
}

# Testing asset cache hit
Refresh-Downloads
Run-Vcpkg -TestArgs ($commonArgs + @('remove', 'vcpkg-internal-e2e-test-port'))
$actual = Run-VcpkgAndCaptureOutput -TestArgs ($commonArgs + @("fetch", "cmake", "--x-asset-sources=x-azurl,file://$AssetCache,,readwrite;", "--downloads-root=$DownloadsRoot"))

$expected = @(
"A suitable version of cmake was not found \(required v[0-9\.]+\)\.",
"Trying to download cmake-[0-9.]+-[^.]+\.(zip|tar\.gz) using asset cache file://$assetCacheRegex/[0-9a-z]+",
"Download successful! Asset cache hit, did not try authoritative source https://github\.com/Kitware/CMake/releases/download/[^ ]+"
) -join "`n"

if (-not ($actual -match $expected)) {
    throw "Failure: asset cache hit"
}

# azurl (no), x-block-origin (no), asset-cache (n/a), download (fail)
# Expected: Download failure message, nothing about asset caching
Refresh-TestRoot
$expected = @(
"^Downloading https://localhost:1234/foobar\.html -> example3\.html",
"error: curl: \(7\) Failed to connect to localhost port 1234( after \d+ ms)?: ((Could not|Couldn't) connect to server|Connection refused)",
"note: If you are using a proxy, please ensure your proxy settings are correct\.",
"Possible causes are:",
"1\. You are actually using an HTTP proxy, but setting HTTPS_PROXY variable to ``https//address:port``\.",
"This is not correct, because ``https://`` prefix claims the proxy is an HTTPS proxy, while your proxy \(v2ray, shadowsocksr, etc\.\.\.\) is an HTTP proxy\.",
"Try setting ``http://address:port`` to both HTTP_PROXY and HTTPS_PROXY instead\."
"2\. If you are using Windows, vcpkg will automatically use your Windows IE Proxy Settings set by your proxy software\. See: https://github\.com/microsoft/vcpkg-tool/pull/77",
"The value set by your proxy might be wrong, or have same ``https://`` prefix issue\.",
"3\. Your proxy's remote server is our of service\.",
"If you've tried directly download the link, and believe this is not a temporary download server failure, please submit an issue at https://github\.com/Microsoft/vcpkg/issues",
"to report this upstream download server failure\.",
"$"
) -join "`n"

$actual = Run-VcpkgAndCaptureOutput -TestArgs ($commonArgs + @("x-download", "$downloadsRoot/example3.html", "--sha512", "d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73a", "--url", "https://localhost:1234/foobar.html"))
Throw-IfNotFailed
if (-not ($actual -match $expected)) {
    throw "Failure: azurl (no), x-block-origin (no), asset-cache (n/a), download (fail)"
}

# Also with multiple URLs
Refresh-TestRoot
$expected = @(
"^Downloading example3\.html, trying https://localhost:1234/foobar\.html",
"Trying https://localhost:1235/baz\.html",
"error: curl: \(7\) Failed to connect to localhost port 1234( after \d+ ms)?: ((Could not|Couldn't) connect to server|Connection refused)",
"error: curl: \(7\) Failed to connect to localhost port 1235( after \d+ ms)?: ((Could not|Couldn't) connect to server|Connection refused)",
"note: If you are using a proxy, please ensure your proxy settings are correct\.",
"Possible causes are:",
"1\. You are actually using an HTTP proxy, but setting HTTPS_PROXY variable to ``https//address:port``\.",
"This is not correct, because ``https://`` prefix claims the proxy is an HTTPS proxy, while your proxy \(v2ray, shadowsocksr, etc\.\.\.\) is an HTTP proxy\.",
"Try setting ``http://address:port`` to both HTTP_PROXY and HTTPS_PROXY instead\."
"2\. If you are using Windows, vcpkg will automatically use your Windows IE Proxy Settings set by your proxy software\. See: https://github\.com/microsoft/vcpkg-tool/pull/77",
"The value set by your proxy might be wrong, or have same ``https://`` prefix issue\.",
"3\. Your proxy's remote server is our of service\.",
"If you've tried directly download the link, and believe this is not a temporary download server failure, please submit an issue at https://github\.com/Microsoft/vcpkg/issues",
"to report this upstream download server failure\.",
"$"
) -join "`n"

$actual = Run-VcpkgAndCaptureOutput -TestArgs ($commonArgs + @("x-download", "$downloadsRoot/example3.html", "--sha512", "d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73a", "--url", "https://localhost:1234/foobar.html", "--url", "https://localhost:1235/baz.html"))
Throw-IfNotFailed
if (-not ($actual -match $expected)) {
    throw "Failure: azurl (no), x-block-origin (no), asset-cache (n/a), download (fail)"
}

#azurl (no), x-block-origin (no), asset-cache (n/a), download (sha-mismatch)
#Expected: Hash check failed message expected/actual sha
Refresh-TestRoot
$expected = @(
"^Downloading https://example\.com -> example3\.html",
"[^\n]+example3\.html\.\d+\.part: error: download from https://example\.com had an unexpected hash",
"note: Expected: d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73b",
"note: Actual  : d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73a",
"$"
) -join "`n"

$actual = Run-VcpkgAndCaptureOutput -TestArgs ($commonArgs + @("x-download", "$downloadsRoot/example3.html", "--sha512", "d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73b", "--url", "https://example.com"))
Throw-IfNotFailed
if (-not ($actual -match $expected)) {
    throw "Failure: azurl (no), x-block-origin (no), asset-cache (n/a), download (sha-mismatch)"
}

# azurl (no), x-block-origin (no), asset-cache (n/a), download (succeed)
# Expected: Download success message, nothing about asset caching
Refresh-TestRoot
$expected = @(
"^Downloading https://example\.com -> example3\.html",
"Successfully downloaded example3\.html",
"$"
) -join "`n"

$actual = Run-VcpkgAndCaptureOutput -TestArgs ($commonArgs + @("x-download", "$downloadsRoot/example3.html", "--sha512", "d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73a", "--url", "https://example.com"))
Throw-IfFailed
if (-not ($actual -match $expected)) {
    throw "Failure: azurl (no), x-block-origin (no), asset-cache (n/a), download (succeed)"
}

# ... also with multiple authoritative URLs
if ($IsWindows) {
    # WinHTTP
    Refresh-TestRoot
    $expected = @(
    "^Downloading example3\.html, trying https://nonexistent\.example\.com",
    "warning: Download https://nonexistent\.example\.com failed -- retrying after 1000ms",
    "warning: Download https://nonexistent\.example\.com failed -- retrying after 2000ms",
    "warning: Download https://nonexistent\.example\.com failed -- retrying after 4000ms",
    "Trying https://example\.com",
    "Successfully downloaded example3\.html",
    "$"
    ) -join "`n"

    $actual = Run-VcpkgAndCaptureOutput -TestArgs ($commonArgs + @("x-download", "$downloadsRoot/example3.html", "--sha512", "d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73a", "--url", "https://nonexistent.example.com", "--url", "https://example.com"))
    Throw-IfFailed
    if (-not ($actual -match $expected)) {
        throw "Failure: azurl (no), x-block-origin (no), asset-cache (n/a), download (succeed)"
    }
}

# Force curl with --header
Refresh-TestRoot
$expected = @(
"^Downloading example3\.html, trying https://nonexistent\.example\.com",
"warning: Problem : timeout. Will retry in 1 seconds. 3 retries left.",
"warning: Problem : timeout. Will retry in 2 seconds. 2 retries left.",
"warning: Problem : timeout. Will retry in 4 seconds. 1 retries left.",
"Trying https://example\.com",
"Successfully downloaded example3\.html",
"$"
) -join "`n"

$actual = Run-VcpkgAndCaptureOutput -TestArgs ($commonArgs + @("x-download", "$downloadsRoot/example3.html", "--sha512", "d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73a", "--url", "https://nonexistent.example.com", "--url", "https://example.com", "--header", "Cache-Control: no-cache"))
Throw-IfFailed
if (-not ($actual -match $expected)) {
    throw "Failure: azurl (no), x-block-origin (no), asset-cache (n/a), download (succeed)"
}

# azurl (no), x-block-origin (yes), asset-cache (n/a), download (n/a)
# Expected: Download failure message, nothing about asset caching, x-block-origin complaint
Refresh-TestRoot
$expected = @(
"^Downloading example3\.html",
"error: there were no asset cache hits, and x-block-origin blocks trying the authoritative source https://example\.com",
"$"
) -join "`n"
$actual = Run-VcpkgAndCaptureOutput -TestArgs ($commonArgs + @("x-download", "$downloadsRoot/example3.html", "--sha512", "d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73a", "--url", "https://example.com", "--x-asset-sources=clear;x-block-origin"))
Throw-IfNotFailed
 if (-not ($actual -match $expected)) {
    throw "Failure: azurl (no), x-block-origin (yes), asset-cache (n/a), download (n/a)"
}


# azurl (yes), x-block-origin (no), asset-cache (miss), download (fail)
# Expected: Download failure message, asset cache named, nothing about x-block-origin
Refresh-TestRoot
$expected = @(
"^Trying to download example3\.html using asset cache file://$assetCacheRegex/[0-9a-z]+",
"Asset cache miss; trying authoritative source https://localhost:1234/foobar\.html",
"error: curl: \(37\) Couldn't open file [^\n]+",
"error: curl: \(7\) Failed to connect to localhost port 1234( after \d+ ms)?: ((Could not|Couldn't) connect to server|Connection refused)",
"note: If you are using a proxy, please ensure your proxy settings are correct\.",
"Possible causes are:",
"1\. You are actually using an HTTP proxy, but setting HTTPS_PROXY variable to ``https//address:port``\.",
"This is not correct, because ``https://`` prefix claims the proxy is an HTTPS proxy, while your proxy \(v2ray, shadowsocksr, etc\.\.\.\) is an HTTP proxy\.",
"Try setting ``http://address:port`` to both HTTP_PROXY and HTTPS_PROXY instead\."
"2\. If you are using Windows, vcpkg will automatically use your Windows IE Proxy Settings set by your proxy software\. See: https://github\.com/microsoft/vcpkg-tool/pull/77",
"The value set by your proxy might be wrong, or have same ``https://`` prefix issue\.",
"3\. Your proxy's remote server is our of service\.",
"If you've tried directly download the link, and believe this is not a temporary download server failure, please submit an issue at https://github\.com/Microsoft/vcpkg/issues",
"to report this upstream download server failure\.",
"$"
) -join "`n"

$actual = Run-VcpkgAndCaptureOutput -TestArgs ($commonArgs + @("x-download", "$downloadsRoot/example3.html", "--sha512", "d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73a", "--url", "https://localhost:1234/foobar.html", "--x-asset-sources=x-azurl,file://$AssetCache,,readwrite"))
Throw-IfNotFailed
if (-not ($actual -match $expected)) {
    throw "Failure: azurl (yes), x-block-origin (no), asset-cache (miss), download (fail)"
}

# azurl (yes), x-block-origin (no), asset-cache (hit), download (n/a)
# Expected: Download success message, asset cache named, nothing about x-block-origin
Refresh-TestRoot
Run-Vcpkg -TestArgs ($commonArgs + @("x-download", "$downloadsRoot/example3.html", "--sha512", "d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73a", "--url", "https://example.com", "--x-asset-sources=x-azurl,file://$AssetCache,,readwrite"))
Throw-IfFailed
Remove-Item "$downloadsRoot/example3.html"
$expected = @(
"^Trying to download example3\.html using asset cache file://$assetCacheRegex/[0-9a-z]+",
"Download successful! Asset cache hit, did not try authoritative source https://example\.com",
"$"
) -join "`n"

$actual = Run-VcpkgAndCaptureOutput -TestArgs ($commonArgs + @("x-download", "$downloadsRoot/example3.html", "--sha512", "d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73a", "--url", "https://example.com", "--x-asset-sources=x-azurl,file://$AssetCache,,readwrite"))
Throw-IfFailed
if (-not ($actual -match $expected)) {
    throw "Success: azurl (yes), x-block-origin (no), asset-cache (hit), download (n/a)"
}

# azurl (yes), x-block-origin (no), asset-cache (hash mismatch), download (n/a)
# Expected: Hash mismatch message, asset cache named, nothing about x-block-origin
Remove-Item "$downloadsRoot/example3.html"
Copy-Item "$AssetCache/d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73a" "$AssetCache/d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73b"
$expected = @(
"^Trying to download example3\.html using asset cache file://$assetCacheRegex/[0-9a-z]+",
"[^\n]+example3\.html\.\d+\.part: error: download from file://$assetCacheRegex/[0-9a-z]+ had an unexpected hash",
"note: Expected: d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73b",
"note: Actual  : d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73a",
"$"
) -join "`n"

$actual = Run-VcpkgAndCaptureOutput -TestArgs ($commonArgs + @("x-download", "$downloadsRoot/example3.html", "--sha512", "d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73b", "--url", "https://example.com", "--x-asset-sources=x-azurl,file://$AssetCache,,read"))
Throw-IfNotFailed
if (-not ($actual -match $expected)) {
    throw "Success: azurl (yes), x-block-origin (no), asset-cache (hit), download (n/a)"
}

# azurl (yes), x-block-origin (no), asset-cache (miss), download (sha-mismatch)
# Expected: Hash check failed message expected/actual sha
Refresh-TestRoot
$expected = @(
"^Trying to download example3\.html using asset cache file://$assetCacheRegex/[0-9a-z]+",
"Asset cache miss; trying authoritative source https://example\.com",
"[^\n]+example3\.html\.\d+\.part: error: download from https://example\.com had an unexpected hash",
"note: Expected: d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73b",
"note: Actual  : d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73a",
"$"
) -join "`n"
$actual = Run-VcpkgAndCaptureOutput -TestArgs ($commonArgs + @("x-download", "$downloadsRoot/example3.html", "--sha512", "d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73b", "--url", "https://example.com", "--x-asset-sources=x-azurl,file://$AssetCache,,readwrite"))
Throw-IfNotFailed
if (-not ($actual -match $expected)) {
    throw "Failure: azurl (yes), x-block-origin (no), asset-cache (miss), download (sha-mismatch)"
}

# azurl (yes), x-block-origin (no), asset-cache (miss), download (succeed)
# Expected: Download success message, asset cache upload, nothing about x-block-origin
Refresh-TestRoot
$expected = @(
"^Trying to download example3\.html using asset cache file://$assetCacheRegex/[0-9a-z]+",
"Asset cache miss; trying authoritative source https://example\.com",
"Successfully downloaded example3\.html, storing to file://$assetCacheRegex/[0-9a-z]+",
"Store success",
"$"
) -join "`n"

$actual = Run-VcpkgAndCaptureOutput -TestArgs ($commonArgs + @("x-download", "$downloadsRoot/example3.html", "--sha512", "d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73a", "--url", "https://example.com", "--x-asset-sources=x-azurl,file://$AssetCache,,readwrite"))
Throw-IfFailed
if (-not ($actual -match $expected)) {
    throw "Success: azurl (yes), x-block-origin (no), asset-cache (miss), download (succeed)"
}

# azurl (yes), x-block-origin (yes), asset-cache (miss), download (n/a)
# Expected: Download failure message, which asset cache was tried, x-block-origin complaint
Refresh-TestRoot
$expected = @(
"^Trying to download example3\.html using asset cache file://$assetCacheRegex/[0-9a-z]+",
"error: curl: \(37\) Couldn't open file [^\n]+",
"error: there were no asset cache hits, and x-block-origin blocks trying the authoritative source https://example\.com",
"note: or https://alternate\.example\.com",
"note: If you are using a proxy, please ensure your proxy settings are correct\.",
"Possible causes are:",
"1\. You are actually using an HTTP proxy, but setting HTTPS_PROXY variable to ``https//address:port``\.",
"This is not correct, because ``https://`` prefix claims the proxy is an HTTPS proxy, while your proxy \(v2ray, shadowsocksr, etc\.\.\.\) is an HTTP proxy\.",
"Try setting ``http://address:port`` to both HTTP_PROXY and HTTPS_PROXY instead\."
"2\. If you are using Windows, vcpkg will automatically use your Windows IE Proxy Settings set by your proxy software\. See: https://github\.com/microsoft/vcpkg-tool/pull/77",
"The value set by your proxy might be wrong, or have same ``https://`` prefix issue\.",
"3\. Your proxy's remote server is our of service\.",
"If you've tried directly download the link, and believe this is not a temporary download server failure, please submit an issue at https://github\.com/Microsoft/vcpkg/issues",
"to report this upstream download server failure\."
"$"
) -join "`n"

$actual = Run-VcpkgAndCaptureOutput -TestArgs ($commonArgs + @("x-download", "$downloadsRoot/example3.html", "--sha512", "d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73a", "--url", "https://example.com", "--url", "https://alternate.example.com", "--x-asset-sources=x-azurl,file://$AssetCache,,readwrite;x-block-origin"))
Throw-IfNotFailed
if (-not ($actual -match $expected)) {
    throw "Failure: azurl (yes), x-block-origin (yes), asset-cache (miss), download (n/a)"
}

# azurl (yes), x-block-origin (yes), asset-cache (hit), download (n/a)
# Expected: Download success message, asset cache named, nothing about x-block-origin
Refresh-TestRoot
$expected = @(
"^Trying to download example3\.html using asset cache file://$assetCacheRegex/[0-9a-z]+",
"Download successful! Asset cache hit, did not try authoritative source https://example\.com, or https://alternate\.example\.com",
"$"
) -join "`n"
Run-Vcpkg -TestArgs ($commonArgs + @("x-download", "$downloadsRoot/example3.html", "--sha512", "d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73a", "--url", "https://example.com", "--x-asset-sources=x-azurl,file://$AssetCache,,readwrite"))
Throw-IfFailed
Remove-Item "$downloadsRoot/example3.html"
$actual = Run-VcpkgAndCaptureOutput -TestArgs ($commonArgs + @("x-download", "$downloadsRoot/example3.html", "--sha512", "d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73a", "--url", "https://example.com", "--url", "https://alternate.example.com", "--x-asset-sources=x-azurl,file://$AssetCache,,readwrite;x-block-origin"))
Throw-IfFailed
if (-not ($actual -match $expected)) {
    throw "Success: azurl (yes), x-block-origin (yes), asset-cache (hit), download (n/a)"
}

# Testing x-download failure with asset cache (x-script) and x-block-origin settings
$env:X_VCPKG_ASSET_SOURCES = "clear;x-script,pwsh $PSScriptRoot/../e2e-assets/asset-caching/failing-script.ps1 {url} {sha512} {dst};x-block-origin"
$expected = @(
"^Trying to download example3.html using asset cache script",
"Script download error",
"error: the asset cache script returned nonzero exit code 1",
"note: the full script command line was: pwsh .+/failing-script\.ps1 https://example\.com d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73a `"?[^`"]+example3\.html\.\d+\.part`"?",
"error: there were no asset cache hits, and x-block-origin blocks trying the authoritative source https://example\.com",
"$"
) -join "`n"
$actual = Run-VcpkgAndCaptureOutput -TestArgs ($commonArgs + @("x-download", "$downloadsRoot/example3.html", "--url", "https://example.com", "--sha512", "d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73a"))
Throw-IfNotFailed
if (-not ($actual -match $expected)) {
    throw "Failure: azurl (no), x-block-origin (yes), asset-cache (hit), download (n/a)"
}

Refresh-TestRoot
$env:X_VCPKG_ASSET_SOURCES = "clear;x-script,pwsh $PSScriptRoot/../e2e-assets/asset-caching/no-file-script.ps1 {url} {sha512} {dst};x-block-origin"
$expected = @(
"^Trying to download example3.html using asset cache script",
"Not creating a file",
"[^\n]+example3\.html\.\d+\.part: error: the asset cache script returned success but did not create expected result file",
"note: the full script command line was: pwsh .+/no-file-script\.ps1 https://example\.com d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73a `"?[^`"]+example3\.html\.\d+\.part`"?",
"error: there were no asset cache hits, and x-block-origin blocks trying the authoritative source https://example\.com",
"$"
) -join "`n"
$actual = Run-VcpkgAndCaptureOutput -TestArgs ($commonArgs + @("x-download", "$downloadsRoot/example3.html", "--url", "https://example.com", "--sha512", "d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73a"))
Throw-IfNotFailed
if (-not ($actual -match $expected)) {
    throw "Failure: azurl (no), x-block-origin (yes), asset-cache (hit), download (n/a)"
}

Refresh-TestRoot
$env:X_VCPKG_ASSET_SOURCES = "clear;x-script,pwsh $PSScriptRoot/../e2e-assets/asset-caching/bad-hash-script.ps1 -File {dst};x-block-origin"
$expected = @(
"^Trying to download example3.html using asset cache script",
"Creating file with the wrong hash",
"[^\n]+example3\.html\.\d+\.part: error: the asset cache script returned success but the resulting file has an unexpected hash",
"note: the full script command line was: pwsh .+/bad-hash-script\.ps1 -File `"?[^`"]+example3\.html\.\d+\.part`"?",
"note: Expected: d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73a",
"note: Actual  : cc9c9070d8a54bfc32d6be2eb01b531f22f657d868200fbcdc7c4cc5f31e92909bd7c83971bebefa918c2c34e53d859ed49a79f4a943f36ec521fc0544b30d9e",
"$"
) -join "`n"
$actual = Run-VcpkgAndCaptureOutput -TestArgs ($commonArgs + @("x-download", "$downloadsRoot/example3.html", "--url", "https://example.com", "--sha512", "d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73a"))
Throw-IfNotFailed
if (-not ($actual -match $expected)) {
    throw "Failure: azurl (no), x-block-origin (yes), asset-cache (hit), download (n/a)"
}

# Testing x-download success with asset cache (x-script) and x-block-origin settings
Refresh-TestRoot
$expected = @(
"^Trying to download example3.html using asset cache script",
"Download successful! Asset cache hit, did not try authoritative source https://example\.com/hello-world.txt",
"$"
) -join "`n"
$env:X_VCPKG_ASSET_SOURCES = "clear;x-script,$TestScriptAssetCacheExe {url} {sha512} {dst};x-block-origin"
$actual = Run-VcpkgAndCaptureOutput -TestArgs ($commonArgs + @("x-download", "$downloadsRoot/example3.html", "--url", "https://example.com/hello-world.txt", "--sha512", "09e1e2a84c92b56c8280f4a1203c7cffd61b162cfe987278d4d6be9afbf38c0e8934cdadf83751f4e99d111352bffefc958e5a4852c8a7a29c95742ce59288a8"))
Throw-IfFailed
if (-not ($actual -match $expected)) {
    throw "Success: x-script download success message"
}

