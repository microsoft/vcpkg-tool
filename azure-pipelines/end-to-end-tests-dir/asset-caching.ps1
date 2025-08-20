. $PSScriptRoot/../end-to-end-tests-prelude.ps1

# Testing x-script
Refresh-TestRoot
$env:VCPKG_FORCE_DOWNLOADED_BINARIES = "ON"
Run-Vcpkg @commonArgs fetch cmake
Throw-IfFailed
Run-Vcpkg @commonArgs fetch ninja
Throw-IfFailed
Remove-Item env:VCPKG_FORCE_DOWNLOADED_BINARIES
$helloPath = Join-Path $DefaultDownloadsRoot 'hello-world.txt'
if (Test-Path $helloPath) {
    Remove-Item $helloPath
}

Run-Vcpkg @commonArgs install vcpkg-test-x-script --x-binarysource=clear "--overlay-ports=$PSScriptRoot/../e2e-ports" "--x-asset-sources=x-script,$TestScriptAssetCacheExe {url} {sha512} {dst};x-block-origin"
Throw-IfFailed

$env:VCPKG_FORCE_DOWNLOADED_BINARIES = "ON"
$assetCacheRegex = [regex]::Escape($AssetCache)

# Testing asset cache miss (not configured) + x-block-origin enabled
Refresh-TestRoot
$actual = Run-VcpkgAndCaptureOutput @commonArgs fetch cmake "--x-asset-sources=clear;x-block-origin" "--downloads-root=$TestDownloadsRoot"
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
$actual = Run-VcpkgAndCaptureOutput @commonArgs fetch cmake "--x-asset-sources=clear;" "--downloads-root=$TestDownloadsRoot"
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
$actual = Run-VcpkgAndCaptureOutput @commonArgs fetch cmake "--x-asset-sources=x-azurl,file://$AssetCache,,readwrite;x-block-origin" "--downloads-root=$TestDownloadsRoot"
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
"3\. Your proxy's remote server is out of service\.",
"If you believe this is not a temporary download server failure and vcpkg needs to be changed to download this file from a different location, please submit an issue to https://github\.com/Microsoft/vcpkg/issues"
) -join "`n"

if (-not ($actual -match $expected)) {
    throw "Failure: asset cache miss (configured) + x-block-origin disabled"
}

# Testing asset cache miss (configured) + x-block-origin disabled
Refresh-TestRoot
$actual = Run-VcpkgAndCaptureOutput @commonArgs fetch cmake "--x-asset-sources=x-azurl,file://$AssetCache,,readwrite;" "--downloads-root=$TestDownloadsRoot"
Throw-IfFailed
$expected = @(
"A suitable version of cmake was not found \(required v[0-9\.]+\)\.",
"Trying to download cmake-[0-9.]+-[^.]+\.(zip|tar\.gz) using asset cache file://$assetCacheRegex/[0-9a-f]+"
"Asset cache miss; trying authoritative source https://github\.com/Kitware/CMake/releases/download/[^ ]+",
"Successfully downloaded cmake-[0-9.]+-[^.]+\.(zip|tar\.gz), storing to file://$assetCacheRegex/[0-9a-f]+"
) -join "`n"

if (-not ($actual -match $expected)) {
    throw "Failure: asset cache miss (configured) + x-block-origin disabled"
}

# Testing asset cache hit
Refresh-TestDownloads
Run-Vcpkg @commonArgs remove vcpkg-internal-e2e-test-port
$actual = Run-VcpkgAndCaptureOutput @commonArgs fetch cmake "--x-asset-sources=x-azurl,file://$AssetCache,,readwrite;" "--downloads-root=$TestDownloadsRoot"

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
"3\. Your proxy's remote server is out of service\.",
"If you believe this is not a temporary download server failure and vcpkg needs to be changed to download this file from a different location, please submit an issue to https://github\.com/Microsoft/vcpkg/issues",
"$"
) -join "`n"

$actual = Run-VcpkgAndCaptureOutput @commonArgs x-download "$TestDownloadsRoot/example3.html" --sha512 d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73a --url https://localhost:1234/foobar.html
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
"3\. Your proxy's remote server is out of service\.",
"If you believe this is not a temporary download server failure and vcpkg needs to be changed to download this file from a different location, please submit an issue to https://github\.com/Microsoft/vcpkg/issues",
"$"
) -join "`n"

$actual = Run-VcpkgAndCaptureOutput @commonArgs x-download "$TestDownloadsRoot/example3.html" --sha512 D06B93C883F8126A04589937A884032DF031B05518EED9D433EFB6447834DF2596AEBD500D69B8283E5702D988ED49655AE654C1683C7A4AE58BFA6B92F2B73A --url https://localhost:1234/foobar.html --url https://localhost:1235/baz.html
Throw-IfNotFailed
if (-not ($actual -match $expected)) {
    throw "Failure: azurl (no), x-block-origin (no), asset-cache (n/a), download (fail)"
}

#azurl (no), x-block-origin (no), asset-cache (n/a), download (sha-mismatch)
#Expected: Hash check failed message expected/actual sha. Note that the expected sha is changed to lowercase.
Refresh-TestRoot
$expected = @(
"^Downloading https://example\.com -> example3\.html",
"[^\n]+example3\.html\.\d+\.part: error: download from https://example\.com had an unexpected hash",
"note: Expected: d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73b",
"note: Actual  : d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73a",
"$"
) -join "`n"

$actual = Run-VcpkgAndCaptureOutput @commonArgs x-download "$TestDownloadsRoot/example3.html" --sha512 d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73b --url https://example.com
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

$actual = Run-VcpkgAndCaptureOutput @commonArgs x-download "$TestDownloadsRoot/example3.html" --sha512 d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73a --url https://example.com
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

    $actual = Run-VcpkgAndCaptureOutput @commonArgs x-download "$TestDownloadsRoot/example3.html" --sha512 d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73a --url https://nonexistent.example.com --url https://example.com
    Throw-IfFailed
    if (-not ($actual -match $expected)) {
        throw "Failure: azurl (no), x-block-origin (no), asset-cache (n/a), download (succeed)"
    }
}

# Force curl with --header
Refresh-TestRoot
$expected = @(
"^Downloading example3\.html, trying https://nonexistent\.example\.com",
"warning: (Problem : timeout\.|Transient problem: timeout) Will retry in 1 seconds?\. 3 retries left\.",
"warning: (Problem : timeout\.|Transient problem: timeout) Will retry in 2 seconds\. 2 retries left\.",
"warning: (Problem : timeout\.|Transient problem: timeout) Will retry in 4 seconds\. 1 (retries|retry) left\.",
"Trying https://example\.com",
"Successfully downloaded example3\.html",
"$"
) -join "`n"

$actual = Run-VcpkgAndCaptureOutput @commonArgs x-download "$TestDownloadsRoot/example3.html" --sha512 d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73a --url https://nonexistent.example.com --url https://example.com --header "Cache-Control: no-cache"
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
$actual = Run-VcpkgAndCaptureOutput @commonArgs  x-download "$TestDownloadsRoot/example3.html" --sha512 d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73a --url https://example.com "--x-asset-sources=clear;x-block-origin"
Throw-IfNotFailed
 if (-not ($actual -match $expected)) {
    throw "Failure: azurl (no), x-block-origin (yes), asset-cache (n/a), download (n/a)"
}


# azurl (yes), x-block-origin (no), asset-cache (miss), download (fail)
# Expected: Download failure message, asset cache named, nothing about x-block-origin. Note that the expected SHA is changed to lowercase.
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
"3\. Your proxy's remote server is out of service\.",
"If you believe this is not a temporary download server failure and vcpkg needs to be changed to download this file from a different location, please submit an issue to https://github\.com/Microsoft/vcpkg/issues",
"$"
) -join "`n"

$actual = Run-VcpkgAndCaptureOutput @commonArgs x-download "$TestDownloadsRoot/example3.html" --sha512 D06B93C883F8126A04589937A884032DF031B05518EED9D433EFB6447834DF2596AEBD500D69B8283E5702D988ED49655AE654C1683C7A4AE58BFA6B92F2B73A --url https://localhost:1234/foobar.html "--x-asset-sources=x-azurl,file://$AssetCache,,readwrite"
Throw-IfNotFailed
if (-not ($actual -match $expected)) {
    throw "Failure: azurl (yes), x-block-origin (no), asset-cache (miss), download (fail)"
}

# azurl (yes), x-block-origin (no), asset-cache (hit), download (n/a)
# Expected: Download success message, asset cache named, nothing about x-block-origin
Refresh-TestRoot
Run-Vcpkg @commonArgs x-download "$TestDownloadsRoot/example3.html" --sha512 d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73a --url https://example.com "--x-asset-sources=x-azurl,file://$AssetCache,,readwrite"
Throw-IfFailed
Remove-Item "$TestDownloadsRoot/example3.html"
$expected = @(
"^Trying to download example3\.html using asset cache file://$assetCacheRegex/[0-9a-z]+",
"Download successful! Asset cache hit, did not try authoritative source https://example\.com",
"$"
) -join "`n"

$actual = Run-VcpkgAndCaptureOutput @commonArgs x-download "$TestDownloadsRoot/example3.html" --sha512 d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73a --url https://example.com "--x-asset-sources=x-azurl,file://$AssetCache,,readwrite"
Throw-IfFailed
if (-not ($actual -match $expected)) {
    throw "Success: azurl (yes), x-block-origin (no), asset-cache (hit), download (n/a)"
}

# azurl (yes), x-block-origin (no), asset-cache (hash mismatch), download (success)
# Expected: Asset cache named, nothing about x-block-origin
Remove-Item "$TestDownloadsRoot/example3.html"
Set-Content -Path "$AssetCache/d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73a" -Encoding Ascii -NoNewline -Value "The wrong hash content"
$expected = @(
"^Trying to download example3\.html using asset cache file://$assetCacheRegex/[0-9a-z]+",
"Asset cache miss; trying authoritative source https://example\.com",
"Successfully downloaded example3\.html, storing to file://$assetCacheRegex/[0-9a-f]+",
"$"
) -join "`n"

$actual = Run-VcpkgAndCaptureOutput @commonArgs x-download "$TestDownloadsRoot/example3.html" --sha512 d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73a --url https://example.com "--x-asset-sources=x-azurl,file://$AssetCache,,readwrite"
Throw-IfFailed
if (-not ($actual -match $expected)) {
    throw "Success: azurl (yes), x-block-origin (no), asset-cache (hit), download (n/a)"
}

# azurl (yes), x-block-origin (no), asset-cache (miss), download (sha-mismatch)
# Expected: File read failure from the asset cache, hash check mismatch for the download. Proxy message emitted due to the asset cache miss even though it doesn't apply to the cache miss.
$expected = @(
"^Trying to download example3\.html using asset cache file://$assetCacheRegex/[0-9a-z]+",
"Asset cache miss; trying authoritative source https://example\.com",
"error: curl: \(37\) Couldn't open file [^\n]+",
"note: If you are using a proxy, please ensure your proxy settings are correct\.",
"Possible causes are:",
"1\. You are actually using an HTTP proxy, but setting HTTPS_PROXY variable to ``https//address:port``\.",
"This is not correct, because ``https://`` prefix claims the proxy is an HTTPS proxy, while your proxy \(v2ray, shadowsocksr, etc\.\.\.\) is an HTTP proxy\.",
"Try setting ``http://address:port`` to both HTTP_PROXY and HTTPS_PROXY instead\."
"2\. If you are using Windows, vcpkg will automatically use your Windows IE Proxy Settings set by your proxy software\. See: https://github\.com/microsoft/vcpkg-tool/pull/77",
"The value set by your proxy might be wrong, or have same ``https://`` prefix issue\.",
"3\. Your proxy's remote server is out of service\.",
"If you believe this is not a temporary download server failure and vcpkg needs to be changed to download this file from a different location, please submit an issue to https://github\.com/Microsoft/vcpkg/issues",
"[^\n]+example3\.html\.\d+\.part: error: download from https://example\.com had an unexpected hash",
"note: Expected: d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73b",
"note: Actual  : d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73a",
"$"
) -join "`n"
$actual = Run-VcpkgAndCaptureOutput @commonArgs x-download "$TestDownloadsRoot/example3.html" --sha512 d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73b --url https://example.com "--x-asset-sources=x-azurl,file://$AssetCache,,readwrite"
Throw-IfNotFailed
if (-not ($actual -match $expected)) {
    throw "Failure: azurl (yes), x-block-origin (no), asset-cache (sha-mismatch), download (sha-mismatch)"
}

# azurl (yes), x-block-origin (no), asset-cache (sha-mismatch), download (sha-mismatch)
# Expected: Hash check failed message expected/actual sha
Copy-Item "$AssetCache/d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73a" "$AssetCache/d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73b"
$expected = @(
"^Trying to download example3\.html using asset cache file://$assetCacheRegex/[0-9a-z]+",
"Asset cache miss; trying authoritative source https://example\.com",
"[^\n]+example3\.html\.\d+\.part: error: download from file://$assetCacheRegex/[0-9a-z]+ had an unexpected hash",
"note: Expected: d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73b",
"note: Actual  : d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73a",
"[^\n]+example3\.html\.\d+\.part: error: download from https://example\.com had an unexpected hash",
"note: Expected: d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73b",
"note: Actual  : d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73a",
"$"
) -join "`n"
$actual = Run-VcpkgAndCaptureOutput @commonArgs x-download "$TestDownloadsRoot/example3.html" --sha512 d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73b --url https://example.com "--x-asset-sources=x-azurl,file://$AssetCache,,readwrite"
Throw-IfNotFailed
if (-not ($actual -match $expected)) {
    throw "Failure: azurl (yes), x-block-origin (no), asset-cache (sha-mismatch), download (sha-mismatch)"
}

# azurl (yes), x-block-origin (no), asset-cache (miss), download (succeed)
# Expected: Download success message, asset cache upload, nothing about x-block-origin
Refresh-TestRoot
$expected = @(
"^Trying to download example3\.html using asset cache file://$assetCacheRegex/[0-9a-z]+",
"Asset cache miss; trying authoritative source https://example\.com",
"Successfully downloaded example3\.html, storing to file://$assetCacheRegex/[0-9a-z]+",
"$"
) -join "`n"

$actual = Run-VcpkgAndCaptureOutput @commonArgs x-download "$TestDownloadsRoot/example3.html" --sha512 d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73a --url https://example.com "--x-asset-sources=x-azurl,file://$AssetCache,,readwrite"
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
"3\. Your proxy's remote server is out of service\.",
"If you believe this is not a temporary download server failure and vcpkg needs to be changed to download this file from a different location, please submit an issue to https://github\.com/Microsoft/vcpkg/issues",
"$"
) -join "`n"

$actual = Run-VcpkgAndCaptureOutput @commonArgs x-download "$TestDownloadsRoot/example3.html" --sha512 d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73a --url https://example.com --url https://alternate.example.com "--x-asset-sources=x-azurl,file://$AssetCache,,readwrite;x-block-origin"
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
Run-Vcpkg @commonArgs x-download "$TestDownloadsRoot/example3.html" --sha512 d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73a --url https://example.com "--x-asset-sources=x-azurl,file://$AssetCache,,readwrite"
Throw-IfFailed
Remove-Item "$TestDownloadsRoot/example3.html"
$actual = Run-VcpkgAndCaptureOutput @commonArgs x-download "$TestDownloadsRoot/example3.html" --sha512 d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73a --url https://example.com --url https://alternate.example.com "--x-asset-sources=x-azurl,file://$AssetCache,,readwrite;x-block-origin"
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
$actual = Run-VcpkgAndCaptureOutput @commonArgs x-download "$TestDownloadsRoot/example3.html" --url https://example.com --sha512 d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73a
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
$actual = Run-VcpkgAndCaptureOutput @commonArgs x-download "$TestDownloadsRoot/example3.html" --url https://example.com --sha512 d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73a
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
"error: there were no asset cache hits, and x-block-origin blocks trying the authoritative source https://example\.com",
"$"
) -join "`n"
$actual = Run-VcpkgAndCaptureOutput @commonArgs x-download "$TestDownloadsRoot/example3.html" --url https://example.com --sha512 d06b93c883f8126a04589937a884032df031b05518eed9d433efb6447834df2596aebd500d69b8283e5702d988ed49655ae654c1683c7a4ae58bfa6b92f2b73a
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
$actual = Run-VcpkgAndCaptureOutput @commonArgs x-download "$TestDownloadsRoot/example3.html" --url https://example.com/hello-world.txt --sha512 09e1e2a84c92b56c8280f4a1203c7cffd61b162cfe987278d4d6be9afbf38c0e8934cdadf83751f4e99d111352bffefc958e5a4852c8a7a29c95742ce59288a8
Throw-IfFailed
if (-not ($actual -match $expected)) {
    throw "Success: x-script download success message"
}

# Testing zero SHA does not try subsequent URLs and emits its special message
Remove-Item env:X_VCPKG_ASSET_SOURCES
Refresh-TestRoot
$testFile = Join-Path $TestingRoot 'download-this.txt'
Set-Content -Path $testFile -Value "This is some content to download" -Encoding Ascii -NoNewline
$downloadTargetUrl = "file://" + $testFile.Replace("\", "/")
$downloadTargetUrlRegex = [regex]::Escape($downloadTargetUrl)
$expected = @(
"^Downloading $downloadTargetUrlRegex -> download-result\.txt",
"Successfully downloaded download-result\.txt",
"error: failing download because the expected SHA512 was all zeros, please change the expected SHA512 to: b3b907ef86c0389954e2a4c85a4ac3d9228129cbe52202fc979873e03089bab448d6c9ae48f1a4925df3b496e3f6fdefbd997b925f3fc93f9418f24a3421e97d",
"$"
) -join "`n"

$actual = Run-VcpkgAndCaptureOutput @commonArgs x-download "$TestDownloadsRoot/download-result.txt" --sha512 00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000 --url $downloadTargetUrl
Throw-IfNotFailed
if (-not ($actual -match $expected)) {
    throw "Failure: zero sha"
}

# Testing zero SHA but with x-script that wants SHA
# Note that this prints the 'unknown SHA' message for the x-script even though a SHA was supplied; we don't want to ever ask the script for all zeroes
$env:X_VCPKG_ASSET_SOURCES = "clear;x-script,some-script.ps1 {sha512};x-block-origin"
$expected = @(
"^Trying to download download-result\.txt using asset cache script",
"error: the script template some-script\.ps1 {sha512} requires a SHA, but no SHA is known for attempted download of $downloadTargetUrlRegex",
"error: there were no asset cache hits, and x-block-origin blocks trying the authoritative source $downloadTargetUrlRegex",
"$"
) -join "`n"

$actual = Run-VcpkgAndCaptureOutput @commonArgs x-download "$TestDownloadsRoot/download-result.txt" --sha512 00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000 --url $downloadTargetUrl
Throw-IfNotFailed
if (-not ($actual -match $expected)) {
    throw "Failure: zero sha + x-script"
}

# Testing unknown replacement in x-script
$env:X_VCPKG_ASSET_SOURCES = "clear;x-script,some-script.ps1 {sha};x-block-origin"
$expected = @(
"^Trying to download download-result\.txt using asset cache script",
"error: the script template some-script.ps1 {sha} contains unknown replacement sha",
"note: if you want this on the literal command line, use {{sha}}",
"error: there were no asset cache hits, and x-block-origin blocks trying the authoritative source $downloadTargetUrlRegex",
"$"
) -join "`n"

$actual = Run-VcpkgAndCaptureOutput @commonArgs x-download "$TestDownloadsRoot/download-result.txt" --sha512 00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000 --url $downloadTargetUrl
Throw-IfNotFailed
if (-not ($actual -match $expected)) {
    throw "Failure: bad replacement"
}
