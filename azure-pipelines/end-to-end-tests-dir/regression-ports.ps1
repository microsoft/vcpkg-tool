. $PSScriptRoot/../end-to-end-tests-prelude.ps1

if ($IsWindows) {
    Run-Vcpkg install --overlay-ports="$PSScriptRoot/../e2e_ports/llvm-lto-lib" llvm-lto-lib:x64-windows-static
    Throw-IfFailed
}
