. $PSScriptRoot/../end-to-end-tests-prelude.ps1

[string]$out = Run-VcpkgAndCaptureOutput -TestArgs @("help", "--overlay-triplets=$PSScriptRoot/../e2e_ports/hash-additional", "--overlay-ports=$PSScriptRoot/../e2e_ports/hash-additional", "install", "vcpkg-test-hash-additional", "--triplet", "hash-additional-e2e", "--debug")
Throw-IfFailed
if (-Not ($out.Contains("additional_file_0|61ba0c7fc1f696e28c1b7aa9460980a571025ff8c97bb90a57e990463aa25660")))
{
    throw "Additional file hash not found in output"
}

Run-Vcpkg 
Throw-IfFailed
