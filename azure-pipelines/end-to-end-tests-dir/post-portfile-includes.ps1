. $PSScriptRoot/../end-to-end-tests-prelude.ps1

Copy-Item -LiteralPath "$PSScriptRoot/../e2e-ports/post-portfile-includes" $TestingRoot -Recurse

$hostTripletContent = Get-Content $tripletFile -Raw
$tripletContent = "$hostTripletContent`n"
$tripletContent += @"
set(VCPKG_POST_PORTFILE_INCLUDES "`${CMAKE_CURRENT_LIST_DIR}/test1.cmake;`${CMAKE_CURRENT_LIST_DIR}/test2.cmake")
"@
$tripletContent += "`n"
Set-Content -Path "$TestingRoot/post-portfile-includes/post-portfile-includes.cmake" -Value $tripletContent -Encoding utf8NoBOM

$tripletContent = "$hostTripletContent`n"
$tripletContent += @"
set(VCPKG_POST_PORTFILE_INCLUDES "`${CMAKE_CURRENT_LIST_DIR}/invalid.extension")
"@
$tripletContent += "`n"
Set-Content -Path "$TestingRoot/post-portfile-includes/post-portfile-includes-fail.cmake" -Value $tripletContent -Encoding utf8NoBOM


Run-Vcpkg @directoryArgs `
  "--overlay-triplets=$TestingRoot/post-portfile-includes" `
  "--overlay-ports=$TestingRoot/post-portfile-includes" `
  x-set-installed `
  vcpkg-post-portfile-includes `
  --host-triplet post-portfile-includes `
  --binarysource=clear
Throw-IfFailed

$output = Run-VcpkgAndCaptureOutput @directoryArgs `
  "--overlay-triplets=$TestingRoot/post-portfile-includes" `
  "--overlay-ports=$PSScriptRoot/../e2e-ports" `
  x-set-installed `
  vcpkg-hello-world-1 `
  --host-triplet post-portfile-includes-fail `
  --binarysource=clear
Throw-IfNotFailed
if ($output -notmatch "Variable VCPKG_POST_PORTFILE_INCLUDES contains invalid file path")
{
    throw "Expected to fail since VCPKG_POST_PORTFILE_INCLUDES is set to a relative path"
}
