. $PSScriptRoot/../end-to-end-tests-prelude.ps1

$commonArgs += @("--x-binarysource=clear", "--overlay-ports=$PSScriptRoot/../e2e-ports", "--overlay-triplets=$PSScriptRoot/../overlay-triplets")

$env:VCPKG_FEATURE_FLAGS="-compilertracking"

# Test native installation and isolation from CLICOLOR_FORCE=1
$env:CLICOLOR_FORCE = 1
Run-Vcpkg ($commonArgs + @("install", "tool-libb"))
Throw-IfFailed
@("tool-control", "tool-manifest", "tool-liba", "tool-libb") | % {
    Require-FileNotExists $installRoot/$HostE2ETriplet/share/$_
    Require-FileExists $installRoot/$Triplet/share/$_
}
Remove-Item env:CLICOLOR_FORCE

Refresh-TestRoot

# Test cross installation
Run-Vcpkg ($commonArgs + @("install", "tool-libb:$HostE2ETriplet"))
Throw-IfFailed
@("tool-control", "tool-manifest", "tool-liba") | % {
    Require-FileNotExists $installRoot/$HostE2ETriplet/share/$_
    Require-FileExists $installRoot/$Triplet/share/$_
}
@("tool-libb") | % {
    Require-FileExists $installRoot/$HostE2ETriplet/share/$_
    Require-FileNotExists $installRoot/$Triplet/share/$_
}

# Test removal of packages in cross installation
Run-Vcpkg ($commonArgs + @("remove", "tool-manifest", "--recurse"))
Throw-IfFailed
@("tool-control", "tool-liba") | % {
    Require-FileNotExists $installRoot/$HostE2ETriplet/share/$_
    Require-FileExists $installRoot/$Triplet/share/$_
}
@("tool-libb", "tool-manifest") | % {
    Require-FileNotExists $installRoot/$HostE2ETriplet/share/$_
    Require-FileNotExists $installRoot/$Triplet/share/$_
}

Refresh-TestRoot

# Test VCPKG_DEFAULT_HOST_TRIPLET
$env:VCPKG_DEFAULT_HOST_TRIPLET = $HostE2ETriplet
Run-Vcpkg ($commonArgs + @("install", "tool-libb:$Triplet"))
Throw-IfFailed
@("tool-control", "tool-manifest", "tool-liba") | % {
    Require-FileExists $installRoot/$HostE2ETriplet/share/$_
    Require-FileNotExists $installRoot/$Triplet/share/$_
}
@("tool-libb") | % {
    Require-FileNotExists $installRoot/$HostE2ETriplet/share/$_
    Require-FileExists $installRoot/$Triplet/share/$_
}

Refresh-TestRoot

Remove-Item env:VCPKG_DEFAULT_HOST_TRIPLET
# Test --host-triplet
Run-Vcpkg ($commonArgs + @("install", "tool-libb:$Triplet", "--host-triplet=$HostE2ETriplet"))
Throw-IfFailed
@("tool-control", "tool-manifest", "tool-liba") | % {
    Require-FileExists $installRoot/$HostE2ETriplet/share/$_
    Require-FileNotExists $installRoot/$Triplet/share/$_
}
@("tool-libb") | % {
    Require-FileNotExists $installRoot/$HostE2ETriplet/share/$_
    Require-FileExists $installRoot/$Triplet/share/$_
}
