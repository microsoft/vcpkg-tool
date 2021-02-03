. $PSScriptRoot/../end-to-end-tests-prelude.ps1

$CurrentTest = "Build Test Ports"

Run-Vcpkg --overlay-ports="$PSScriptRoot/../e2e_ports" install vcpkg-find-acquire-program
