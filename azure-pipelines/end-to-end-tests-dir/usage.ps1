. "$PSScriptRoot/../end-to-end-tests-prelude.ps1"

[string[]]$requiredUsages = @(
@"
vcpkg-cmake-config-many-targets provides CMake targets:

  # this is heuristically generated, and may not be correct
  find_package(cmake-config-many-targets CONFIG REQUIRED)
  # note: 6 additional targets are not displayed.
  target_link_libraries(main PRIVATE cmake-config-many-targets::vcpkg-cmake-config-many-targets-1 cmake-config-many-targets::vcpkg-cmake-config-many-targets-2 cmake-config-many-targets::vcpkg-cmake-config-many-targets-3 cmake-config-many-targets::vcpkg-cmake-config-many-targets-4)
"@,
@"
This is some usage text explicitly set by the port.

This output should end up on the console.

"@,
@"
vcpkg-hello-world-1 provides CMake targets:

  # this is heuristically generated, and may not be correct
  find_package(hello-world-1 CONFIG REQUIRED)
  target_link_libraries(main PRIVATE hello-world-1::hello-world-1)
"@,
@"
vcpkg-hello-world-2 provides CMake targets:

  # this is heuristically generated, and may not be correct
  find_package(hello-world-2 CONFIG REQUIRED)
  target_link_libraries(main PRIVATE hello-world-2::hello-world-2)
"@,
@"
wrong-pkgconfig provides pkg-config modules:

  # Test lib
  wrong-pkgconfig
"@,
@"
vcpkg-header-only is header-only and can be used from CMake via:

  find_path(VCPKG_HEADER_ONLY_INCLUDE_DIRS "vcpkg-header-only.h")
  target_include_directories(main PRIVATE `${VCPKG_HEADER_ONLY_INCLUDE_DIRS})
"@
)

[string[]]$prohibitedUsages = @(
    'vcpkg-empty-port provides CMake targets',
    'vcpkg-header-only provides CMake targets'
)

[string]$usage = Run-VcpkgAndCaptureOutput ($commonArgs + @('install',
    'vcpkg-cmake-config-many-targets',
    'vcpkg-empty-port',
    'vcpkg-explicit-usage',
    'vcpkg-header-only',
    'vcpkg-hello-world-1',
    'vcpkg-hello-world-2',
    'wrong-pkgconfig[header-only-good]'
))

$usage = $usage.Replace("`r`n", "`n")

foreach ($requiredUsage in $requiredUsages) {
    if (-Not $usage.Contains($requiredUsage)) {
        throw "The usage text didn't contain the required entry:`n$requiredUsage"
    }
}

foreach ($prohibitedUsage in $prohibitedUsages) {
    if ($usage.Contains($prohibitedUsage)) {
        throw "The usage text contains the prohibited entry:`n$prohibitedUsage"
    }
}
