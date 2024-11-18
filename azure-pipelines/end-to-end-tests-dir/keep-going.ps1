. $PSScriptRoot/../end-to-end-tests-prelude.ps1

$commonArgs += @("--x-builtin-ports-root=$PSScriptRoot/../e2e-ports")

# Test keep-going to not report an error
Run-Vcpkg -TestArgs ($commonArgs + @("install", "vcpkg-empty-port", "--keep-going"))
Throw-IfFailed

# Test keep-going to report an error
Run-Vcpkg -TestArgs ($commonArgs + @("install", "vcpkg-fail-if-depended-upon", "--keep-going"))
Throw-IfNotFailed
