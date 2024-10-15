. $PSScriptRoot/../end-to-end-tests-prelude.ps1

$commonArgs += @("--x-binarysource=clear", "--overlay-ports=$PSScriptRoot/../e2e-ports", "--overlay-triplets=$PSScriptRoot/../overlay-triplets")

$hostTriplet = "$Triplet"
$env:VCPKG_DEFAULT_HOST_TRIPLET = "$hostTriplet"
if ($IsMacOS)
{
    $targetTriplet = "x64-osx-e2e"
}
elseif ($ISLinux)
{
    $targetTriplet = "x64-linux-e2e"
}
else
{
    $targetTriplet = "x64-windows-e2e"
}

$env:VCPKG_FEATURE_FLAGS="-compilertracking"

# Test native installation and isolation from CLICOLOR_FORCE=1
$env:CLICOLOR_FORCE = 1
Run-Vcpkg ($commonArgs + @("install", "tool-libb"))
Throw-IfFailed
@("tool-control", "tool-manifest", "tool-liba", "tool-libb") | % {
    Require-FileNotExists $installRoot/$targetTriplet/share/$_
    Require-FileExists $installRoot/$hostTriplet/share/$_
}
Remove-Item env:CLICOLOR_FORCE

Refresh-TestRoot

# Test cross installation
Run-Vcpkg ($commonArgs + @("install", "tool-libb:$targetTriplet"))
Throw-IfFailed
@("tool-control", "tool-manifest", "tool-liba") | % {
    Require-FileNotExists $installRoot/$targetTriplet/share/$_
    Require-FileExists $installRoot/$hostTriplet/share/$_
}
@("tool-libb") | % {
    Require-FileExists $installRoot/$targetTriplet/share/$_
    Require-FileNotExists $installRoot/$hostTriplet/share/$_
}

# Test removal of packages in cross installation
Run-Vcpkg ($commonArgs + @("remove", "tool-manifest", "--recurse"))
Throw-IfFailed
@("tool-control", "tool-liba") | % {
    Require-FileNotExists $installRoot/$targetTriplet/share/$_
    Require-FileExists $installRoot/$hostTriplet/share/$_
}
@("tool-libb", "tool-manifest") | % {
    Require-FileNotExists $installRoot/$targetTriplet/share/$_
    Require-FileNotExists $installRoot/$hostTriplet/share/$_
}

Refresh-TestRoot

# Test VCPKG_DEFAULT_HOST_TRIPLET
$env:VCPKG_DEFAULT_HOST_TRIPLET = $targetTriplet
Run-Vcpkg ($commonArgs + @("install", "tool-libb:$hostTriplet"))
Throw-IfFailed
@("tool-control", "tool-manifest", "tool-liba") | % {
    Require-FileExists $installRoot/$targetTriplet/share/$_
    Require-FileNotExists $installRoot/$hostTriplet/share/$_
}
@("tool-libb") | % {
    Require-FileNotExists $installRoot/$targetTriplet/share/$_
    Require-FileExists $installRoot/$hostTriplet/share/$_
}

Refresh-TestRoot

Remove-Item env:VCPKG_DEFAULT_HOST_TRIPLET
# Test --host-triplet
Run-Vcpkg ($commonArgs + @("install", "tool-libb:$hostTriplet", "--host-triplet=$targetTriplet"))
Throw-IfFailed
@("tool-control", "tool-manifest", "tool-liba") | % {
    Require-FileExists $installRoot/$targetTriplet/share/$_
    Require-FileNotExists $installRoot/$hostTriplet/share/$_
}
@("tool-libb") | % {
    Require-FileNotExists $installRoot/$targetTriplet/share/$_
    Require-FileExists $installRoot/$hostTriplet/share/$_
}
