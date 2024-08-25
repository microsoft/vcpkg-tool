. $PSScriptRoot/../end-to-end-tests-prelude.ps1

Run-Vcpkg "--overlay-triplets=$PSScriptRoot/../e2e_ports/hash-additional" "--overlay-ports=$PSScriptRoot/../e2e_ports/hash-additional" install vcpkg-test-hash-additional --triplet hash-additional-e2e 
Throw-IfFailed

$output = Run-VcpkgAndCaptureOutput @directoryArgs "--overlay-triplets=$PSScriptRoot/../e2e-ports/hash-additional-fail" "--overlay-ports=$PSScriptRoot/../e2e-ports/hash-additional-fail" x-set-installed vcpkg-test-hash-additional --triplet hash-additional-e2e --binarysource=clear
Throw-IfNotFailed
if ($output -notmatch "Variable VCPKG_HASH_ADDITIONAL_FILES contains invalid file path")
{
    throw "Expected to fail since VCPKG_HASH_ADDITIONAL_FILES is set to a relative path"
}
