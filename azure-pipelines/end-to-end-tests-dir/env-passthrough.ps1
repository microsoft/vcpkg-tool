if (-not $IsLinux -and -not $IsMacOS) {
    . $PSScriptRoot/../end-to-end-tests-prelude.ps1

    $env:_VCPKG_TEST_TRACKED = "a"
    $env:_VCPKG_TEST_UNTRACKED = "b"

    $x = Run-Vcpkg "--overlay-triplets=$PSScriptRoot/../e2e_ports/env-passthrough" env "echo %_VCPKG_TEST_TRACKED% %_VCPKG_TEST_TRACKED2% %_VCPKG_TEST_UNTRACKED% %_VCPKG_TEST_UNTRACKED2%"
    if ($x -ne "%_VCPKG_TEST_TRACKED% %_VCPKG_TEST_TRACKED2% %_VCPKG_TEST_UNTRACKED% %_VCPKG_TEST_UNTRACKED2%`r`n")
    {
        throw "env should have cleaned the environment ($x)"
    }

    $y = Run-Vcpkg "--overlay-triplets=$PSScriptRoot/../e2e_ports/env-passthrough" env --triplet passthrough "echo %_VCPKG_TEST_TRACKED% %_VCPKG_TEST_TRACKED2% %_VCPKG_TEST_UNTRACKED% %_VCPKG_TEST_UNTRACKED2%"
    if ($y -ne "a %_VCPKG_TEST_TRACKED2% b %_VCPKG_TEST_UNTRACKED2%`r`n")
    {
        throw "env should have kept the environment ($y)"
    }

    rm env:_VCPKG_TEST_TRACKED
    rm env:_VCPKG_TEST_UNTRACKED
}
