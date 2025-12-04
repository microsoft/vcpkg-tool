. $PSScriptRoot/../end-to-end-tests-prelude.ps1

$hostTripletContent = Get-Content $tripletFile -Raw
$tripletContent = "$hostTripletContent`n"
$tripletContent += @"
set(VCPKG_HASH_ADDITIONAL_FILES "`${CMAKE_CURRENT_LIST_DIR}/additional_abi_file.txt")
"@
$tripletContent += "`n"
Set-Content -Path "$TestingRoot/hash-additional-e2e.cmake" -Value $tripletContent -Encoding utf8NoBOM
Copy-Item "$PSScriptRoot/../e2e-ports/hash-additional/additional_abi_file.txt" "$TestingRoot/additional_abi_file.txt" -Force

Run-Vcpkg @directoryArgs "--overlay-triplets=$TestingRoot" "--overlay-ports=$PSScriptRoot/../e2e-ports/hash-additional" x-set-installed vcpkg-test-hash-additional --triplet hash-additional-e2e --binarysource=clear
Throw-IfFailed

$tripletContent = "$hostTripletContent`n"
$tripletContent += @"
set(VCPKG_HASH_ADDITIONAL_FILES "additional_abi_file.txt") # relatives path should fail
"@
$tripletContent += "`n"
Set-Content -Path "$TestingRoot/hash-additional-e2e.cmake" -Value $tripletContent

$output = Run-VcpkgAndCaptureOutput @directoryArgs "--overlay-triplets=$TestingRoot" "--overlay-ports=$PSScriptRoot/../e2e-ports" x-set-installed vcpkg-hello-world-1 --triplet hash-additional-e2e --binarysource=clear
Throw-IfNotFailed
if ($output -notmatch "Variable VCPKG_HASH_ADDITIONAL_FILES contains invalid file path")
{
    throw "Expected to fail since VCPKG_HASH_ADDITIONAL_FILES is set to a relative path"
}
