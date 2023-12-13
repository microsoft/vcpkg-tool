. $PSScriptRoot/../end-to-end-tests-prelude.ps1

$downloadsRoot = Join-Path $TestingRoot 'downloads'

$CurrentTest = "Clean After Build"

$InstalledHeader = Join-Path $installRoot "$Triplet/include/vcpkg-clean-after-build-test-port.h"
$DownloadedFile = Join-Path $downloadsRoot 'clean_after_build_test.txt'
$PackageRoot = Join-Path $packagesRoot "vcpkg-clean-after-build-test-port_$Triplet"
$PackageSrc = Join-Path $buildtreesRoot "vcpkg-clean-after-build-test-port/src"

$installTestPortArgs = `
  @("install", "vcpkg-clean-after-build-test-port", "--no-binarycaching", "--downloads-root=$downloadsRoot")

Refresh-TestRoot
Run-Vcpkg -TestArgs ($commonArgs + $installTestPortArgs)
Require-FileExists $InstalledHeader
Require-FileExists $DownloadedFile
Require-FileExists $PackageRoot
Require-FileExists $PackageSrc

Refresh-TestRoot
Run-Vcpkg -TestArgs ($commonArgs + $installTestPortArgs + @("--clean-packages-after-build"))
Require-FileExists $InstalledHeader
Require-FileExists $DownloadedFile
Require-FileNotExists $PackageRoot
Require-FileExists $PackageSrc

Refresh-TestRoot
Run-Vcpkg -TestArgs ($commonArgs + $installTestPortArgs + @("--clean-buildtrees-after-build"))
Require-FileExists $InstalledHeader
Require-FileExists $DownloadedFile
Require-FileExists $PackageRoot
Require-FileNotExists $PackageSrc

Refresh-TestRoot
Run-Vcpkg -TestArgs ($commonArgs + $installTestPortArgs + @(
    "--clean-packages-after-build",
    "--clean-buildtrees-after-build"
    ))
Require-FileExists $InstalledHeader
Require-FileExists $DownloadedFile
Require-FileNotExists $PackageRoot
Require-FileNotExists $PackageSrc

Refresh-TestRoot
Run-Vcpkg -TestArgs ($commonArgs + $installTestPortArgs + @("--clean-buildtrees-after-build"))
Require-FileExists $InstalledHeader
Require-FileExists $DownloadedFile
Require-FileExists $PackageRoot
Require-FileNotExists $PackageSrc

Refresh-TestRoot
Run-Vcpkg -TestArgs ($commonArgs + $installTestPortArgs + @(
    "--clean-downloads-after-build",
    "--clean-packages-after-build",
    "--clean-buildtrees-after-build"
    ))
Require-FileExists $InstalledHeader
Require-FileNotExists $DownloadedFile
Require-FileNotExists $PackageRoot
Require-FileNotExists $PackageSrc

Refresh-TestRoot
Run-Vcpkg -TestArgs ($commonArgs + $installTestPortArgs + @("--clean-after-build"))
Require-FileExists $InstalledHeader
Require-FileNotExists $DownloadedFile
Require-FileNotExists $PackageRoot
Require-FileNotExists $PackageSrc

Refresh-TestRoot
Run-Vcpkg -TestArgs ($commonArgs + $installTestPortArgs + @(
    "--clean-after-build",
    "--clean-downloads-after-build",
    "--clean-packages-after-build",
    "--clean-buildtrees-after-build"
    ))
Require-FileExists $InstalledHeader
Require-FileNotExists $DownloadedFile
Require-FileNotExists $PackageRoot
Require-FileNotExists $PackageSrc
