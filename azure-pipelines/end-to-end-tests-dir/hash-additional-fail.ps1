. $PSScriptRoot/../end-to-end-tests-prelude.ps1
Run-Vcpkg "--overlay-triplets=$PSScriptRoot/../e2e_ports/hash-additional-faiil" "--overlay-ports=$PSScriptRoot/../e2e_ports/hash-additional-fail" install vcpkg-test-hash-additional --triplet hash-additional-e2e 
Throw-IfNotFailed
