. $PSScriptRoot/../end-to-end-tests-prelude.ps1

# Not a number
Refresh-TestRoot
$out = Run-VcpkgAndCaptureOutput @commonArgs install classic-versions-b
Throw-IfNotFailed
if ($out -notmatch ".*warning:.*dependency classic-versions-a.*at least version 2.*is currently 1.*")
{
    throw "Expected to fail and print warning about mismatched versions"
}

Refresh-TestRoot
Run-Vcpkg @commonArgs install set-detected-head-version
Throw-IfFailed
$listResults = Run-VcpkgAndCaptureOutput @commonArgs list
if (-Not ($listResults.Trim() -match "set-detected-head-version:[\S]+[\s]+1.0.0"))
{
    throw "Expected list to list the declared version"
}

Refresh-TestRoot
Run-Vcpkg @commonArgs install set-detected-head-version --head
Throw-IfFailed
$listResults = Run-VcpkgAndCaptureOutput @commonArgs list
if (-Not ($listResults.Trim() -match "set-detected-head-version:[\S]+[\s]+detected-head"))
{
    throw "Expected list to list the detected version"
}
