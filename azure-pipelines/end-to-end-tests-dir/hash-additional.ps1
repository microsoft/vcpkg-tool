. $PSScriptRoot/../end-to-end-tests-prelude.ps1
Run-Vcpkg "--overlay-triplets=$PSScriptRoot/../e2e_ports/hash-additional" "--overlay-ports=$PSScriptRoot/../e2e_ports/hash-additional" install port-hash-additional --triplet hash-additional-e2e 
Throw-IfFailed
