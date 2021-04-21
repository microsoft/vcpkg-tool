. $PSScriptRoot/../end-to-end-tests-prelude.ps1

$CurrentTest = "Clean After Build"

$ZlibInstalledHeader = Join-Path $installRoot "$Triplet/include/zlib.h"
$ZlibDownloadTarball = Join-Path $downloadsRoot 'zlib1211.tar.gz'
$ZlibPackageRoot = Join-Path $packagesRoot "zlib_$Triplet"
$ZlibSrc = Join-Path $buildtreesRoot "zlib/src"

Refresh-TestRoot
Run-Vcpkg -TestArgs ($commonArgs + @("install", "zlib", "--no-binarycaching"))
Require-FileExists $ZlibInstalledHeader
Require-FileExists $ZlibDownloadTarball
Require-FileExists $ZlibPackageRoot
Require-FileExists $ZlibSrc

Refresh-TestRoot
Run-Vcpkg -TestArgs ($commonArgs + @("install", "zlib", "--clean-non-downloads-after-build", "--no-binarycaching"))
Require-FileExists $ZlibInstalledHeader
Require-FileExists $ZlibDownloadTarball
Require-FileNotExists $ZlibPackageRoot
Require-FileNotExists $ZlibSrc

Refresh-TestRoot
Run-Vcpkg -TestArgs ($commonArgs + @("install", "zlib", "--clean-after-build", "--no-binarycaching"))
Require-FileExists $ZlibInstalledHeader
Require-FileNotExists $ZlibDownloadTarball
Require-FileNotExists $ZlibPackageRoot
Require-FileNotExists $ZlibSrc
