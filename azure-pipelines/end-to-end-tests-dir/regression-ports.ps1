. $PSScriptRoot/../end-to-end-tests-prelude.ps1

Run-Vcpkg install --overlay-ports="$PSScriptRoot/../e2e-ports" --binarysource clear broken-symlink
Throw-IfFailed
Run-Vcpkg remove broken-symlink
Throw-IfFailed

if ($IsWindows) {
    Run-Vcpkg install --overlay-ports="$PSScriptRoot/../e2e-ports/llvm-lto-lib" llvm-lto-lib:x64-windows-static
    Throw-IfFailed
}
