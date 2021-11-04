. $PSScriptRoot/../end-to-end-tests-prelude.ps1

if ($IsWindows) {
    $cache_home = $env:LOCALAPPDATA
} elseif (Test-Path "env:XDG_CACHE_HOME") {
    $cache_home = $env:XDG_CACHE_HOME
} else {
    $cache_home = "$env:HOME/.cache"
}
$bundle = Join-Path $TestingRoot "bundle"
$manifestdir = Join-Path $TestingRoot "manifest"

# Test classic bundle
New-Item -ItemType Directory -Force $bundle | Out-Null
New-Item -ItemType File -Force $bundle/.vcpkg-root | Out-Null

$a = Run-Vcpkg x-print-config `
    --vcpkg-root=$bundle `
    --overlay-triplets=$env:VCPKG_ROOT/triplets `
    --x-scripts-dir=$env:VCPKG_ROOT/scripts
Throw-IfFailed
$a
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
@{
    readonly = $True
} | ConvertTo-JSON | out-file -enc ascii $bundle/.vcpkg-root | Out-Null

$a = Run-Vcpkg x-print-config `
    --vcpkg-root=$bundle `
    --overlay-triplets=$env:VCPKG_ROOT/triplets `
    --x-scripts-dir=$env:VCPKG_ROOT/scripts
Throw-IfFailed
$a
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
@{
    readonly = $True
} | ConvertTo-JSON | out-file -enc ascii $bundle/.vcpkg-root | Out-Null
@{
    name = "manifest"
    version = "0"
} | ConvertTo-JSON | out-file -enc ascii $manifestdir/vcpkg.json | Out-Null

$a = Run-Vcpkg x-print-config `
    --vcpkg-root=$bundle `
    --x-manifest-root=$manifestdir `
    --overlay-triplets=$env:VCPKG_ROOT/triplets `
    --x-scripts-dir=$env:VCPKG_ROOT/scripts
Throw-IfFailed
$a
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
@{
    readonly = $True
} | ConvertTo-JSON | out-file -enc ascii $bundle/.vcpkg-root | Out-Null
@{
    name = "manifest"
    version = "0"
} | ConvertTo-JSON | out-file -enc ascii $manifestdir/vcpkg.json | Out-Null

$a = Run-Vcpkg x-print-config `
    --vcpkg-root=$bundle `
    --x-manifest-root=$manifestdir `
    --overlay-triplets=$env:VCPKG_ROOT/triplets `
    --x-buildtrees-root=$buildtreesRoot `
    --x-install-root=$installRoot `
    --x-packages-root=$packagesRoot `
    --x-scripts-dir=$env:VCPKG_ROOT/scripts
Throw-IfFailed
$a
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
