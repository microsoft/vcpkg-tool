. $PSScriptRoot/../end-to-end-tests-prelude.ps1

# Test vcpkg create
$Script:CurrentTest = "create zlib"
Write-Host $Script:CurrentTest
$RegistryRoot = Join-Path $TestingRoot 'registry_for_create'
& git init $RegistryRoot
Throw-IfFailed
Run-Vcpkg x-init-registry $RegistryRoot
Throw-IfFailed
Run-Vcpkg --x-registry-root=$RegistryRoot create zlib https://github.com/madler/zlib/archive/v1.2.11.tar.gz zlib-1.2.11.tar.gz
Throw-IfFailed

Require-FileExists "$RegistryRoot/ports/zlib/portfile.cmake"
Require-FileExists "$RegistryRoot/ports/zlib/vcpkg.json"
