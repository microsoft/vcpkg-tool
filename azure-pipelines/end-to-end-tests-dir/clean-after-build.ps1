. $PSScriptRoot/../end-to-end-tests-prelude.ps1

$CurrentTest = "Clean After Build"

$ZlibInstalledHeader = Join-Path $installRoot "$Triplet/include/zlib.h"
$ZlibDownloadTarball = Join-Path $downloadsRoot 'zlib1211.tar.gz'
$ZlibPackageRoot = Join-Path $packagesRoot "zlib_$Triplet"
$ZlibSrc = Join-Path $buildtreesRoot "zlib/src"

$installZlibArgs = @("install", "zlib", "--no-binarycaching")

Refresh-TestRoot
Run-Vcpkg -TestArgs ($commonArgs + $installZlibArgs)
Require-FileExists $ZlibInstalledHeader
Require-FileExists $ZlibDownloadTarball
Require-FileExists $ZlibPackageRoot
Require-FileExists $ZlibSrc

Refresh-TestRoot
Run-Vcpkg -TestArgs ($commonArgs + $installZlibArgs + @("--clean-packages-after-build"))
Require-FileExists $ZlibInstalledHeader
Require-FileExists $ZlibDownloadTarball
Require-FileNotExists $ZlibPackageRoot
Require-FileExists $ZlibSrc

Refresh-TestRoot
Run-Vcpkg -TestArgs ($commonArgs + $installZlibArgs + @("--clean-buildtrees-after-build"))
Require-FileExists $ZlibInstalledHeader
Require-FileExists $ZlibDownloadTarball
Require-FileExists $ZlibPackageRoot
Require-FileNotExists $ZlibSrc

Refresh-TestRoot
Run-Vcpkg -TestArgs ($commonArgs + $installZlibArgs + @("--clean-packages-after-build", "--clean-buildtrees-after-build"))
Require-FileExists $ZlibInstalledHeader
Require-FileExists $ZlibDownloadTarball
Require-FileNotExists $ZlibPackageRoot
Require-FileNotExists $ZlibSrc

Refresh-TestRoot
Run-Vcpkg -TestArgs ($commonArgs + $installZlibArgs + @("--clean-buildtrees-after-build"))
Require-FileExists $ZlibInstalledHeader
Require-FileExists $ZlibDownloadTarball
Require-FileExists $ZlibPackageRoot
Require-FileNotExists $ZlibSrc

Refresh-TestRoot
Run-Vcpkg -TestArgs ($commonArgs + $installZlibArgs + @("--clean-downloads-after-build", "--clean-packages-after-build", "--clean-buildtrees-after-build"))
Require-FileExists $ZlibInstalledHeader
Require-FileNotExists $ZlibDownloadTarball
Require-FileNotExists $ZlibPackageRoot
Require-FileNotExists $ZlibSrc

Refresh-TestRoot
Run-Vcpkg -TestArgs ($commonArgs + $installZlibArgs + @("--clean-after-build"))
Require-FileExists $ZlibInstalledHeader
Require-FileNotExists $ZlibDownloadTarball
Require-FileNotExists $ZlibPackageRoot
Require-FileNotExists $ZlibSrc

Refresh-TestRoot
Run-Vcpkg -TestArgs ($commonArgs + $installZlibArgs + @("--clean-after-build", "--clean-downloads-after-build", "--clean-packages-after-build", "--clean-buildtrees-after-build"))
Require-FileExists $ZlibInstalledHeader
Require-FileNotExists $ZlibDownloadTarball
Require-FileNotExists $ZlibPackageRoot
Require-FileNotExists $ZlibSrc
