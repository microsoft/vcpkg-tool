. $PSScriptRoot/../end-to-end-tests-prelude.ps1
Run-Vcpkg "--overlay-triplets=$PSScriptRoot/../e2e_ports/triplet-abi" "--overlay-ports=$PSScriptRoot/../e2e_ports/triplet-abi" install vcpkg-test-triplet-abi --triplet x64-abi
Throw-IfFailed
