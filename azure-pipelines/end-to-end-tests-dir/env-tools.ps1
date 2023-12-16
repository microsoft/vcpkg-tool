. $PSScriptRoot/../end-to-end-tests-prelude.ps1

$env:VCPKG_DOWNLOADS = Join-Path $TestingRoot 'empty downloads'
Run-Vcpkg env --bin --tools --python set
if ($IsWindows) {
    Throw-IfFailed
}
