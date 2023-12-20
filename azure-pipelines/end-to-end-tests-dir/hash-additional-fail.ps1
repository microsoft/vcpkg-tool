. $PSScriptRoot/../end-to-end-tests-prelude.ps1
$output = Run-VcpkgAndCaptureOutput "--overlay-triplets=$PSScriptRoot/../e2e_ports/hash-additional-fail" "--overlay-ports=$PSScriptRoot/../e2e_ports/hash-additional-fail" install vcpkg-test-hash-additional --triplet hash-additional-e2e 
Throw-IfNotFailed
Test-Match $output "Variable VCPKG_HASH_ADDITIONAL_FILES contains invalid file path"
