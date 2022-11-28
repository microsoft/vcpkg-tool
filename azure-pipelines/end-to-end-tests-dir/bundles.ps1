. $PSScriptRoot/../end-to-end-tests-prelude.ps1

if (Test-Path env:VCPKG_DOWNLOADS) {
    Remove-Item env:VCPKG_DOWNLOADS
}

if ($IsWindows) {
    $cache_home = $env:LOCALAPPDATA
} elseif (Test-Path "env:XDG_CACHE_HOME") {
    $cache_home = $env:XDG_CACHE_HOME
} else {
    $cache_home = "$env:HOME/.cache"
}
$bundle = Join-Path $TestingRoot "bundle"
$manifestdir = Join-Path $TestingRoot "manifest"
$commonArgs = @(
    "--vcpkg-root=$bundle",
    "--overlay-triplets=$env:VCPKG_ROOT/triplets",
    "--overlay-triplets=$env:VCPKG_ROOT/triplets/community",
    "--x-scripts-root=$env:VCPKG_ROOT/scripts"
)

# Test classic bundle
New-Item -ItemType Directory -Force $bundle | Out-Null
New-Item -ItemType File -Force $bundle/.vcpkg-root | Out-Null

$a = Run-VcpkgAndCaptureOutput z-print-config @commonArgs
Throw-IfFailed
$a = $($a | ConvertFrom-JSON -AsHashtable)
$b = @{
    buildtrees = Join-Path $bundle "buildtrees"
    downloads = Join-Path $bundle "downloads"
    packages = Join-Path $bundle "packages"
    installed = Join-Path $bundle "installed"
    versions_output = Join-Path $bundle "buildtrees" "versioning_" "versions"
    tools = Join-Path $bundle "downloads" "tools"
    vcpkg_root = $bundle
}
foreach ($k in $b.keys) {
    if ($a[$k] -ne $b[$k]) {
        throw "Expected key '$k' with value '$($a[$k])' to be '$($b[$k])'"
    }
}

# Test readonly bundle without manifest
Refresh-TestRoot

New-Item -ItemType Directory -Force $bundle | Out-Null
New-Item -ItemType File -Force $bundle/.vcpkg-root | Out-Null
@{
    readonly = $True
} | ConvertTo-JSON | out-file -enc ascii $bundle/vcpkg-bundle.json | Out-Null

$a = Run-VcpkgAndCaptureOutput z-print-config @commonArgs
Throw-IfFailed
$a = $($a | ConvertFrom-JSON -AsHashtable)

$b = @{
    buildtrees = $null
    downloads = Join-Path $cache_home "vcpkg" "downloads"
    packages = $null
    installed = $null
    versions_output = $null
    tools = Join-Path $cache_home "vcpkg" "downloads" "tools"
    vcpkg_root = $bundle
}
foreach ($k in $b.keys) {
    if ($a[$k] -ne $b[$k]) {
        throw "Expected key '$k' with value '$($a[$k])' to be '$($b[$k])'"
    }
}

# Test readonly bundle with manifest
Refresh-TestRoot

New-Item -ItemType Directory -Force $manifestdir | Out-Null
New-Item -ItemType Directory -Force $bundle | Out-Null
New-Item -ItemType File -Force $bundle/.vcpkg-root | Out-Null
@{
    readonly = $True
} | ConvertTo-JSON | out-file -enc ascii $bundle/vcpkg-bundle.json | Out-Null
@{
    name = "manifest"
    version = "0"
} | ConvertTo-JSON | out-file -enc ascii $manifestdir/vcpkg.json | Out-Null

$a = Run-VcpkgAndCaptureOutput z-print-config --x-manifest-root=$manifestdir @commonArgs
Throw-IfFailed
$a = $($a | ConvertFrom-JSON -AsHashtable)

$b = @{
    buildtrees = Join-Path $manifestdir "vcpkg_installed" "vcpkg" "blds"
    downloads = Join-Path $cache_home "vcpkg" "downloads"
    packages = Join-Path $manifestdir "vcpkg_installed" "vcpkg" "pkgs"
    installed = Join-Path $manifestdir "vcpkg_installed"
    versions_output = Join-Path $manifestdir "vcpkg_installed" "vcpkg" "blds" "versioning_" "versions"
    tools = Join-Path $cache_home "vcpkg" "downloads" "tools"
    vcpkg_root = $bundle
    manifest_mode_enabled = $True
}
foreach ($k in $b.keys) {
    if ($a[$k] -ne $b[$k]) {
        throw "Expected key '$k' with value '$($a[$k])' to be '$($b[$k])'"
    }
}

# Test packages and buildtrees redirection
Refresh-TestRoot

$manifestdir = Join-Path $TestingRoot "manifest"

New-Item -ItemType Directory -Force $manifestdir | Out-Null
New-Item -ItemType Directory -Force $bundle | Out-Null
New-Item -ItemType File -Force $bundle/.vcpkg-root | Out-Null
@{
    readonly = $True
} | ConvertTo-JSON | out-file -enc ascii $bundle/vcpkg-bundle.json | Out-Null
@{
    name = "manifest"
    version = "0"
    dependencies = @("rapidjson")
} | ConvertTo-JSON | out-file -enc ascii $manifestdir/vcpkg.json | Out-Null

$a = Run-VcpkgAndCaptureOutput z-print-config `
    @commonArgs `
    --x-manifest-root=$manifestdir `
    --x-buildtrees-root=$buildtreesRoot `
    --x-install-root=$installRoot `
    --x-packages-root=$packagesRoot
Throw-IfFailed
$a = $($a | ConvertFrom-JSON -AsHashtable)

$b = @{
    buildtrees = $buildtreesRoot
    downloads = Join-Path $cache_home "vcpkg" "downloads"
    packages = $packagesRoot
    installed = $installRoot
    versions_output = Join-Path $buildtreesRoot "versioning_" "versions"
    tools = Join-Path $cache_home "vcpkg" "downloads" "tools"
    vcpkg_root = $bundle
    manifest_mode_enabled = $True
}
foreach ($k in $b.keys) {
    if ($a[$k] -ne $b[$k]) {
        throw "Expected key '$k' with value '$($a[$k])' to be '$($b[$k])'"
    }
}

Run-Vcpkg install zlib --dry-run @commonArgs `
    --x-buildtrees-root=$buildtreesRoot `
    --x-builtin-ports-root=$env:VCPKG_ROOT/ports `
    --x-install-root=$installRoot `
    --x-packages-root=$packagesRoot
Throw-IfNotFailed

# Testing "usegitregistry"
$CurrentTest = "Testing bundle.usegitregistry"
@{
    readonly = $True
    usegitregistry = $True
} | ConvertTo-JSON | out-file -enc ascii $bundle/vcpkg-bundle.json | Out-Null
Run-Vcpkg install --dry-run @commonArgs `
    --x-manifest-root=$manifestdir `
    --x-buildtrees-root=$buildtreesRoot `
    --x-builtin-ports-root=$env:VCPKG_ROOT/ports `
    --x-install-root=$installRoot `
    --x-packages-root=$packagesRoot
Throw-IfNotFailed

$manifestdir2 = Join-Path $TestingRoot "manifest2"
New-Item -ItemType Directory -Force $manifestdir2 | Out-Null
@{
    name = "manifest"
    version = "0"
    dependencies = @("rapidjson")
    "builtin-baseline" = "897ff9372f15c032f1e6cd1b97a59b57d66ee5b2"
} | ConvertTo-JSON | out-file -enc ascii $manifestdir2/vcpkg.json | Out-Null

Run-Vcpkg install @commonArgs `
    --x-manifest-root=$manifestdir2 `
    --x-buildtrees-root=$buildtreesRoot `
    --x-builtin-ports-root=$env:VCPKG_ROOT/ports `
    --x-install-root=$installRoot `
    --x-packages-root=$packagesRoot
Throw-IfFailed

Run-Vcpkg search zlib @commonArgs `
    --x-manifest-root=$manifestdir2 `
    --x-buildtrees-root=$buildtreesRoot `
    --x-builtin-ports-root=$env:VCPKG_ROOT/ports `
    --x-install-root=$installRoot `
    --x-packages-root=$packagesRoot
Throw-IfFailed

# Test CI environment detection
$detected_ci_key = 'detected_ci_environment'
$known_ci_vars = (
    "env:VCPKG_NO_CI",
    "env:TF_BUILD",
    "env:APPVEYOR",
    "env:CODEBUILD_BUILD_ID",
    "env:CIRCLECI",
    "env:GITHUB_ACTIONS",
    "env:GITLAB_CI",
    "env:HEROKU_TEST_RUN_ID",
    "env:JENKINS_URL",
    "env:TRAVIS",
    "env:CI",
    "env:BUILD_ID"
)

foreach ($var in $known_ci_vars) {
    if (Test-Path $var) {
        Remove-Item $var
    }
}

$env:VCPKG_NO_CI="1"
$env:TF_BUILD="1"
$env:CI="1"

$a = Run-VcpkgAndCaptureOutput z-print-config @commonArgs
Throw-IfFailed
$a = $($a | ConvertFrom-JSON -AsHashtable)
if ($a[$detected_ci_key] -ne 'VCPKG_NO_CI') {
    throw "Expected VCPKG_NO_CI as detected CI environment but found: $($a[$detected_ci_key])"
}

Remove-Item env:VCPKG_NO_CI
$a = Run-VcpkgAndCaptureOutput z-print-config @commonArgs
Throw-IfFailed
$a = $($a | ConvertFrom-JSON -AsHashtable)
if ($a[$detected_ci_key] -ne 'Azure_Pipelines') {
    throw "Expected Azure_Pipelines as detected CI environment but found: $($a[$detected_ci_key])"
}

Remove-Item env:TF_BUILD
$a = Run-VcpkgAndCaptureOutput z-print-config @commonArgs
Throw-IfFailed
$a = $($a | ConvertFrom-JSON -AsHashtable)
if ($a[$detected_ci_key] -ne 'Generic') {
    throw "Expected Generic as detected CI environment but found: $($a[$detected_ci_key])"
}

Remove-Item env:CI
$a = Run-VcpkgAndCaptureOutput z-print-config @commonArgs
Throw-IfFailed
$a = $($a | ConvertFrom-JSON -AsHashtable)
if ($a[$detected_ci_key] -ne $null) {
    throw "Expected no CI environment but found $($a[$detected_ci_key]))"
}
