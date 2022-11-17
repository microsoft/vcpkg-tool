. $PSScriptRoot/../end-to-end-tests-prelude.ps1

# Not a number
$out = Run-VcpkgAndCaptureOutput @commonArgs install classic-versions-b
Throw-IfNotFailed
if ($out -notmatch ".*warning:.*dependency classic-versions-a.*at least version 2.*is currently 1.*")
{
    throw "Expected to fail and print warning about mismatched versions"
}
