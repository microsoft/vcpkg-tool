. $PSScriptRoot/../end-to-end-tests-prelude.ps1

Refresh-TestRoot
Run-Vcpkg -TestArgs ($commonArgs + @('fetch', 'cmake'))
Run-Vcpkg -TestArgs ($commonArgs + @("install", "vcpkg-test-x-script", "--x-binarysource=clear", "--overlay-ports=$PSScriptRoot/../e2e-ports", "--x-asset-sources=x-script,$TestScriptAssetCacheExe {url} {sha512} {dst};x-block-origin"))
Throw-IfFailed
