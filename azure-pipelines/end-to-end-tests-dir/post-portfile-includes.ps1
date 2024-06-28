. $PSScriptRoot/../end-to-end-tests-prelude.ps1

Run-Vcpkg @directoryArgs `
  "--overlay-triplets=$PSScriptRoot/../e2e-ports/post-portfile-includes" `
  "--overlay-ports=$PSScriptRoot/../e2e-ports/post-portfile-includes" `
  x-set-installed `
  vcpkg-post-portfile-includes `
  --host-triplet post-portfile-includes `
  --binarysource=clear
Throw-IfFailed

$output = Run-VcpkgAndCaptureOutput @directoryArgs `
  "--overlay-triplets=$PSScriptRoot/../e2e-ports/post-portfile-includes-fail" `
  "--overlay-ports=$PSScriptRoot/../e2e-ports/post-portfile-includes-fail" `
  x-set-installed `
  vcpkg-post-portfile-includes `
  --host-triplet post-portfile-includes `
  --binarysource=clear
Throw-IfNotFailed
if ($output -notmatch "Variable VCPKG_POST_PORTFILE_INCLUDES contains invalid file path")
{
    throw "Expected to fail since VCPKG_POST_PORTFILE_INCLUDES is set to a relative path"
}
