. $PSScriptRoot/../end-to-end-tests-prelude.ps1

$NativeSlash = '/'
if ($IsWindows) {
    $NativeSlash = '\'
}

# Fail if installed files exist
Refresh-TestRoot
[string]$dupOutput = Run-VcpkgAndCaptureOutput install @commonArgs "--overlay-ports=$PSScriptRoot/../e2e-ports" duplicate-file-a duplicate-file-b
Throw-IfNotFailed
if (-not $dupOutput.Contains('The following files are already installed')) {
    throw ('Incorrect error message for due to duplicate files; output was ' + $dupOutput)
}

# Empty package / disable all checks
Refresh-TestRoot
[string]$buildOutput = Run-VcpkgAndCaptureStderr install @commonArgs --overlay-ports="$PSScriptRoot/../e2e-ports" vcpkg-policy-set-incorrectly
Throw-IfNotFailed
if (-not $buildOutput.EndsWith("error: Unknown setting of VCPKG_POLICY_EMPTY_PACKAGE: ON. Valid policy values are '', 'disabled', and 'enabled'.`n")) {
    throw ('Incorrect error message for incorrect policy value; output was ' + $buildOutput)
}

$buildOutput = Run-VcpkgAndCaptureOutput install @commonArgs --overlay-ports="$PSScriptRoot/../e2e-ports" vcpkg-policy-empty-package --no-binarycaching
Throw-IfFailed
if (-not $buildOutput.Contains('Skipping post-build validation due to VCPKG_POLICY_EMPTY_PACKAGE')) {
    throw ('Didn''t skip post-build checks correctly, output was ' + $buildOutput)
}

$buildOutput = Run-VcpkgAndCaptureOutput install @commonArgs --overlay-ports="$PSScriptRoot/../e2e-ports" vcpkg-policy-skip-all-post-build-checks --no-binarycaching
Throw-IfFailed
if (-not $buildOutput.Contains('Skipping post-build validation due to VCPKG_POLICY_SKIP_ALL_POST_BUILD_CHECKS')) {
    throw ('Didn''t skip post-build checks correctly, output was ' + $buildOutput)
}

# Include folder, restricted headers, and CMake helper port checks
Refresh-TestRoot
$PortfilePath = "$PSScriptRoot/../e2e-ports$($NativeSlash)vcpkg-policy-include-folder$($NativeSlash)portfile.cmake"
$buildOutput = Run-VcpkgAndCaptureOutput @commonArgs install --overlay-ports="$PSScriptRoot/../e2e-ports" vcpkg-policy-include-folder --no-binarycaching --enforce-port-checks
Throw-IfNotFailed
[string]$expected = @"
$($PortfilePath): warning: The folder $`{CURRENT_PACKAGES_DIR}/include is empty or not present. This usually means that headers are not correctly installed. If this is a CMake helper port, add set(VCPKG_POLICY_CMAKE_HELPER_PORT enabled). If this is not a CMake helper port but this is otherwise intentional, add set(VCPKG_POLICY_EMPTY_INCLUDE_FOLDER enabled) to suppress this message.
"@
if (-not $buildOutput.Contains($expected)) {
    throw 'Did not detect empty include folder'
}

$buildOutput = Run-VcpkgAndCaptureOutput @commonArgs install --overlay-ports="$PSScriptRoot/../e2e-ports" 'vcpkg-policy-include-folder[policy-empty-include-folder]' --no-binarycaching --enforce-port-checks
Throw-IfFailed
if ($buildOutput.Contains($expected)) {
    throw 'VCPKG_POLICY_EMPTY_INCLUDE_FOLDER didn''t suppress'
}

Refresh-TestRoot
$buildOutput = Run-VcpkgAndCaptureOutput @commonArgs install --overlay-ports="$PSScriptRoot/../e2e-ports" 'vcpkg-policy-include-folder[do-install,policy-cmake-helper-port]' --no-binarycaching --enforce-port-checks
Throw-IfNotFailed
$expected = @"
$($PortfilePath): warning: The folder $`{CURRENT_PACKAGES_DIR}/include exists in a CMake helper port; this is incorrect, since only CMake files should be installed. To suppress this message, remove set(VCPKG_POLICY_CMAKE_HELPER_PORT enabled).
"@
if (-not $buildOutput.Contains($expected)) {
    throw 'Did not detect nonempty include folder for CMake helper port.'
}

Refresh-TestRoot
$buildOutput = Run-VcpkgAndCaptureOutput @commonArgs install --overlay-ports="$PSScriptRoot/../e2e-ports" 'vcpkg-policy-include-folder[policy-cmake-helper-port]' --no-binarycaching --enforce-port-checks
Throw-IfNotFailed
$expected = @"
$($PortfilePath): warning: The $`{CURRENT_PACKAGES_DIR}/share/`${PORT}/vcpkg-port-config.cmake file does not exist. This file must exist for CMake helper ports. To suppress this message, remove set(VCPKG_POLICY_CMAKE_HELPER_PORT enabled)
"@
if (-not $buildOutput.Contains($expected)) {
    throw 'Did not detect missing vcpkg-port-config.cmake for CMake helper port.'
}

$buildOutput = Run-VcpkgAndCaptureOutput @commonArgs install --overlay-ports="$PSScriptRoot/../e2e-ports" 'vcpkg-policy-include-folder[policy-cmake-helper-port,do-install-vcpkg-port-config]' --no-binarycaching --enforce-port-checks
Throw-IfFailed

Refresh-TestRoot
$buildOutput = Run-VcpkgAndCaptureOutput @commonArgs install --overlay-ports="$PSScriptRoot/../e2e-ports" 'vcpkg-policy-include-folder[do-install-restricted]' --no-binarycaching --enforce-port-checks
Throw-IfNotFailed
$expected = @"
$($PortfilePath): warning: Taking the following restricted headers can prevent the core C++ runtime and other packages from compiling correctly. These should be renamed or stored in a subdirectory instead. In exceptional circumstances, this warning can be suppressed by adding set(VCPKG_POLICY_ALLOW_RESTRICTED_HEADERS enabled)
$($packagesRoot)$($NativeSlash)vcpkg-policy-include-folder_$($Triplet)$($NativeSlash)include: note: the headers are relative to `${CURRENT_PACKAGES_DIR}/include here
note: <json.h>
"@

if (-not $buildOutput.Contains($expected)) {
    throw 'Did not detect restricted header'
}

$buildOutput = Run-VcpkgAndCaptureOutput @commonArgs install --overlay-ports="$PSScriptRoot/../e2e-ports" 'vcpkg-policy-include-folder[do-install-restricted,policy-allow-restricted-headers]' --no-binarycaching --enforce-port-checks
Throw-IfFailed
if ($buildOutput.Contains($expected)) {
    throw 'VCPKG_POLICY_ALLOW_RESTRICTED_HEADERS didn''t allow'
}

Refresh-TestRoot
$buildOutput = Run-VcpkgAndCaptureOutput @commonArgs install --overlay-ports="$PSScriptRoot/../e2e-ports" 'vcpkg-policy-include-folder[do-install,do-install-debug]' --no-binarycaching --enforce-port-checks
Throw-IfNotFailed
$expected = @"
$($PortfilePath): warning: `${CURRENT_PACKAGES_DIR}/debug/include should not exist. To suppress this message, add set(VCPKG_POLICY_ALLOW_DEBUG_INCLUDE enabled)
note: If this directory was created by a build system that does not allow installing headers in debug to be disabled, delete the duplicate directory with file(REMOVE_RECURSE "`${CURRENT_PACKAGES_DIR}/debug/include")
"@
if (-not $buildOutput.Contains($expected)) {
    throw 'Did not detect debug headers'
}

$buildOutput = Run-VcpkgAndCaptureOutput @commonArgs install --overlay-ports="$PSScriptRoot/../e2e-ports" 'vcpkg-policy-include-folder[do-install,do-install-debug,policy-allow-debug-include]' --no-binarycaching --enforce-port-checks
Throw-IfFailed
if ($buildOutput.Contains($expected)) {
    throw 'VCPKG_POLICY_ALLOW_DEBUG_INCLUDE didn''t suppress'
}

Refresh-TestRoot
$buildOutput = Run-VcpkgAndCaptureOutput @commonArgs install --overlay-ports="$PSScriptRoot/../e2e-ports" 'vcpkg-policy-include-folder[do-install,do-install-debug-share]' --no-binarycaching --enforce-port-checks
Throw-IfNotFailed
$expected = @"
$($PortfilePath): warning: `${CURRENT_PACKAGES_DIR}/debug/share should not exist. Please reorganize any important files, then delete any remaining by adding ``file(REMOVE_RECURSE "`${CURRENT_PACKAGES_DIR}/debug/share")``. To suppress this message, add set(VCPKG_POLICY_ALLOW_DEBUG_SHARE enabled)
"@
if (-not $buildOutput.Contains($expected)) {
    throw 'Did not detect debug share'
}

$buildOutput = Run-VcpkgAndCaptureOutput @commonArgs install --overlay-ports="$PSScriptRoot/../e2e-ports" 'vcpkg-policy-include-folder[do-install,do-install-debug-share,policy-allow-debug-share]' --no-binarycaching --enforce-port-checks
Throw-IfFailed
if ($buildOutput.Contains($expected)) {
    throw 'VCPKG_POLICY_ALLOW_DEBUG_SHARE didn''t suppress'
}

# Misplaced CMake Files
Refresh-TestRoot
$PortfilePath = "$PSScriptRoot/../e2e-ports$($NativeSlash)vcpkg-policy-misplaced-cmake-files$($NativeSlash)portfile.cmake"
Run-Vcpkg @commonArgs install --overlay-ports="$PSScriptRoot/../e2e-ports" vcpkg-policy-misplaced-cmake-files --no-binarycaching --enforce-port-checks
Throw-IfFailed

Refresh-TestRoot
$buildOutput = Run-VcpkgAndCaptureOutput @commonArgs install --overlay-ports="$PSScriptRoot/../e2e-ports" 'vcpkg-policy-misplaced-cmake-files[do-install-cmake]' --no-binarycaching --enforce-port-checks
Throw-IfNotFailed
$expected = @"
$($PortfilePath): warning: This port installs the following CMake files in places CMake files are not expected. CMake files should be installed in `${CURRENT_PACKAGES_DIR}/share/`${PORT}. To suppress this message, add set(VCPKG_POLICY_SKIP_MISPLACED_CMAKE_FILES_CHECK enabled)
$($packagesRoot)$($NativeSlash)vcpkg-policy-misplaced-cmake-files_$($Triplet): note: the files are relative to `${CURRENT_PACKAGES_DIR} here
note: cmake/some_cmake.cmake
note: debug/cmake/some_cmake.cmake
"@
if ($buildOutput.Contains("legitimate.cmake")) {
    throw 'Complained about legitimate CMake files'
}
if (-not $buildOutput.Contains($expected)) {
    throw 'Did not detect bad CMake files'
}

Refresh-TestRoot
$buildOutput = Run-VcpkgAndCaptureOutput @commonArgs install --overlay-ports="$PSScriptRoot/../e2e-ports" 'vcpkg-policy-misplaced-cmake-files[do-install-lib]' --no-binarycaching --enforce-port-checks
Throw-IfNotFailed
$expected = @"
$($PortfilePath): warning: This port installs the following CMake files in places CMake files are not expected. CMake files should be installed in `${CURRENT_PACKAGES_DIR}/share/`${PORT}. To suppress this message, add set(VCPKG_POLICY_SKIP_MISPLACED_CMAKE_FILES_CHECK enabled)
$($packagesRoot)$($NativeSlash)vcpkg-policy-misplaced-cmake-files_$($Triplet): note: the files are relative to `${CURRENT_PACKAGES_DIR} here
note: lib/cmake/some_cmake.cmake
note: debug/lib/cmake/some_cmake.cmake
$($PortfilePath): warning: This port creates `${CURRENT_PACKAGES_DIR}/lib/cmake and/or `${CURRENT_PACKAGES_DIR}/debug/lib/cmake, which should be merged and moved to `${CURRENT_PACKAGES_DIR}/share/`${PORT}/cmake. Please use the helper function vcpkg_cmake_config_fixup() from the port vcpkg-cmake-config. To suppress this message, add set(VCPKG_POLICY_SKIP_LIB_CMAKE_MERGE_CHECK enabled)
"@
if ($buildOutput.Contains("legitimate.cmake")) {
    throw 'Complained about legitimate CMake files'
}
if (-not $buildOutput.Contains($expected)) {
    throw 'Did not detect bad CMake files'
}

Refresh-TestRoot
$buildOutput = Run-VcpkgAndCaptureOutput @commonArgs install --overlay-ports="$PSScriptRoot/../e2e-ports" 'vcpkg-policy-misplaced-cmake-files[do-install-cmake,do-install-lib]' --no-binarycaching --enforce-port-checks
Throw-IfNotFailed
$expected = @"
$($PortfilePath): warning: This port installs the following CMake files in places CMake files are not expected. CMake files should be installed in `${CURRENT_PACKAGES_DIR}/share/`${PORT}. To suppress this message, add set(VCPKG_POLICY_SKIP_MISPLACED_CMAKE_FILES_CHECK enabled)
$($packagesRoot)$($NativeSlash)vcpkg-policy-misplaced-cmake-files_$($Triplet): note: the files are relative to `${CURRENT_PACKAGES_DIR} here
note: cmake/some_cmake.cmake
note: debug/cmake/some_cmake.cmake
note: lib/cmake/some_cmake.cmake
note: debug/lib/cmake/some_cmake.cmake
$($PortfilePath): warning: This port creates `${CURRENT_PACKAGES_DIR}/lib/cmake and/or `${CURRENT_PACKAGES_DIR}/debug/lib/cmake, which should be merged and moved to `${CURRENT_PACKAGES_DIR}/share/`${PORT}/cmake. Please use the helper function vcpkg_cmake_config_fixup() from the port vcpkg-cmake-config. To suppress this message, add set(VCPKG_POLICY_SKIP_LIB_CMAKE_MERGE_CHECK enabled)
"@
if ($buildOutput.Contains("legitimate.cmake")) {
    throw 'Complained about legitimate CMake files'
}
if (-not $buildOutput.Contains($expected)) {
    throw 'Did not detect bad CMake files'
}

Refresh-TestRoot
$buildOutput = Run-VcpkgAndCaptureOutput @commonArgs install --overlay-ports="$PSScriptRoot/../e2e-ports" 'vcpkg-policy-misplaced-cmake-files[do-install-cmake,do-install-lib,policy-skip-misplaced-cmake-files-check]' --no-binarycaching --enforce-port-checks
Throw-IfNotFailed
$expected = @"
$($PortfilePath): warning: This port creates `${CURRENT_PACKAGES_DIR}/lib/cmake and/or `${CURRENT_PACKAGES_DIR}/debug/lib/cmake, which should be merged and moved to `${CURRENT_PACKAGES_DIR}/share/`${PORT}/cmake. Please use the helper function vcpkg_cmake_config_fixup() from the port vcpkg-cmake-config. To suppress this message, add set(VCPKG_POLICY_SKIP_LIB_CMAKE_MERGE_CHECK enabled)
"@
if ($buildOutput.Contains("legitimate.cmake")) {
    throw 'Complained about legitimate CMake files'
}
if (-not $buildOutput.Contains($expected)) {
    throw 'Did not detect bad CMake files'
}

Refresh-TestRoot
$buildOutput = Run-VcpkgAndCaptureOutput @commonArgs install --overlay-ports="$PSScriptRoot/../e2e-ports" 'vcpkg-policy-misplaced-cmake-files[do-install-cmake,do-install-lib,policy-skip-lib-cmake-merge-check]' --no-binarycaching --enforce-port-checks
Throw-IfNotFailed
$expected = @"
$($PortfilePath): warning: This port installs the following CMake files in places CMake files are not expected. CMake files should be installed in `${CURRENT_PACKAGES_DIR}/share/`${PORT}. To suppress this message, add set(VCPKG_POLICY_SKIP_MISPLACED_CMAKE_FILES_CHECK enabled)
$($packagesRoot)$($NativeSlash)vcpkg-policy-misplaced-cmake-files_$($Triplet): note: the files are relative to `${CURRENT_PACKAGES_DIR} here
note: cmake/some_cmake.cmake
note: debug/cmake/some_cmake.cmake
note: lib/cmake/some_cmake.cmake
note: debug/lib/cmake/some_cmake.cmake
"@
if ($buildOutput.Contains("legitimate.cmake")) {
    throw 'Complained about legitimate CMake files'
}
if (-not $buildOutput.Contains($expected)) {
    throw 'Did not detect bad CMake files'
}

Refresh-TestRoot
$buildOutput = Run-VcpkgAndCaptureOutput @commonArgs install --overlay-ports="$PSScriptRoot/../e2e-ports" 'vcpkg-policy-misplaced-cmake-files[do-install-cmake,policy-skip-misplaced-cmake-files-check]' --no-binarycaching --enforce-port-checks
Throw-IfFailed

Refresh-TestRoot
$buildOutput = Run-VcpkgAndCaptureOutput @commonArgs install --overlay-ports="$PSScriptRoot/../e2e-ports" 'vcpkg-policy-misplaced-cmake-files[do-install-lib,policy-skip-misplaced-cmake-files-check,policy-skip-lib-cmake-merge-check]' --no-binarycaching --enforce-port-checks
Throw-IfFailed

# Copyright Files
Refresh-TestRoot
$PortfilePath = "$PSScriptRoot/../e2e-ports$($NativeSlash)vcpkg-policy-copyright$($NativeSlash)portfile.cmake"
$expected = @"
$($PortfilePath): warning: this port sets `${CURRENT_PACKAGES_DIR}/share/`${PORT}/copyright to a directory, but it should be a file. Consider combining separate copyright files into one using vcpkg_install_copyright. To suppress this message, add set(VCPKG_POLICY_SKIP_COPYRIGHT_CHECK enabled)
"@
$buildOutput = Run-VcpkgAndCaptureOutput install @commonArgs --overlay-ports="$PSScriptRoot/../e2e-ports" 'vcpkg-policy-copyright[copyright-directory]' --no-binarycaching --enforce-port-checks
Throw-IfNotFailed
if (-not $buildOutput.Contains($expected)) {
    throw 'Did not detect copyright directory'
}

$buildOutput = Run-VcpkgAndCaptureOutput install @commonArgs --overlay-ports="$PSScriptRoot/../e2e-ports" 'vcpkg-policy-copyright[copyright-directory,policy]' --no-binarycaching --enforce-port-checks
Throw-IfFailed
if ($buildOutput.Contains($expected)) {
    throw 'VCPKG_POLICY_SKIP_COPYRIGHT_CHECK didn''t suppress copyright problem'
}

Refresh-TestRoot
$expected = @"
$($PortfilePath): warning: the license is not installed to `${CURRENT_PACKAGES_DIR}/share/`${PORT}/copyright . This can be fixed by adding a call to vcpkg_install_copyright. To suppress this message, add set(VCPKG_POLICY_SKIP_COPYRIGHT_CHECK enabled)
"@

$buildOutput = Run-VcpkgAndCaptureOutput install @commonArgs --overlay-ports="$PSScriptRoot/../e2e-ports" 'vcpkg-policy-copyright' --no-binarycaching --enforce-port-checks
Throw-IfNotFailed
if (-not $buildOutput.Contains($expected)) {
    throw 'Did not detect missing copyright no source'
}

Refresh-TestRoot
$expected = @"
$($PortfilePath): warning: the license is not installed to `${CURRENT_PACKAGES_DIR}/share/`${PORT}/copyright . This can be fixed by adding a call to vcpkg_install_copyright. To suppress this message, add set(VCPKG_POLICY_SKIP_COPYRIGHT_CHECK enabled)
$($PortfilePath): note: Consider adding: vcpkg_install_copyright(FILE_LIST "`${SOURCE_PATH}/LICENSE.txt")
"@

$buildOutput = Run-VcpkgAndCaptureOutput install @commonArgs --overlay-ports="$PSScriptRoot/../e2e-ports" 'vcpkg-policy-copyright[source]' --no-binarycaching --enforce-port-checks
Throw-IfNotFailed
if (-not $buildOutput.Contains($expected)) {
    throw 'Did not detect missing copyright source'
}

Refresh-TestRoot
$expected = @"
$($PortfilePath): warning: the license is not installed to `${CURRENT_PACKAGES_DIR}/share/`${PORT}/copyright . This can be fixed by adding a call to vcpkg_install_copyright. To suppress this message, add set(VCPKG_POLICY_SKIP_COPYRIGHT_CHECK enabled)
$($PortfilePath): note: Consider adding: vcpkg_install_copyright(FILE_LIST "`${SOURCE_PATH}/COPYING" "`${SOURCE_PATH}/LICENSE.txt")
"@

$buildOutput = Run-VcpkgAndCaptureOutput install @commonArgs --overlay-ports="$PSScriptRoot/../e2e-ports" 'vcpkg-policy-copyright[source2]' --no-binarycaching --enforce-port-checks
Throw-IfNotFailed
if (-not $buildOutput.Contains($expected)) {
    throw 'Did not detect missing copyright source2'
}

Refresh-TestRoot
$expected = @"
$($PortfilePath): warning: the license is not installed to `${CURRENT_PACKAGES_DIR}/share/`${PORT}/copyright . This can be fixed by adding a call to vcpkg_install_copyright. To suppress this message, add set(VCPKG_POLICY_SKIP_COPYRIGHT_CHECK enabled)
$($PortfilePath): note: the following files are potential copyright files
$($buildtreesRoot)$($NativeSlash)vcpkg-policy-copyright: note: the files are relative to the build directory here
note: src/v1.16.3-6f5be3c3eb.clean/LICENSE.txt
note: src/v1.3.1-2e5db616bf.clean/COPYING
note: src/v1.3.1-2e5db616bf.clean/LICENSE.txt
"@

$buildOutput = Run-VcpkgAndCaptureOutput install @commonArgs --overlay-ports="$PSScriptRoot/../e2e-ports" 'vcpkg-policy-copyright[source,source2]' --no-binarycaching --enforce-port-checks
Throw-IfNotFailed
if (-not $buildOutput.Contains($expected)) {
    throw 'Did not detect missing copyright source + source2'
}

$buildOutput = Run-VcpkgAndCaptureOutput install @commonArgs --overlay-ports="$PSScriptRoot/../e2e-ports" 'vcpkg-policy-copyright[source,source2,policy]' --no-binarycaching --enforce-port-checks
Throw-IfFailed
if ($buildOutput.Contains($expected)) {
    throw 'VCPKG_POLICY_SKIP_COPYRIGHT_CHECK didn''t suppress source + source2'
}

# EXEs in bin
if ($IsWindows) {
    Refresh-TestRoot
    mkdir "$TestingRoot/exes-in-bin"
    Copy-Item -Recurse "$PSScriptRoot/../e2e-assets/test-exe-port-template" "$TestingRoot/exes-in-bin/test-exe"
    Run-Vcpkg env "$TestingRoot/exes-in-bin/test-exe/build.cmd" --Triplet x64-windows

    $PortfilePath = "$TestingRoot/exes-in-bin$($NativeSlash)test-exe$($NativeSlash)portfile.cmake"
    $buildOutput = Run-VcpkgAndCaptureOutput @commonArgs install --overlay-ports="$TestingRoot/exes-in-bin" 'test-exe[release-only]' --no-binarycaching --enforce-port-checks
    Throw-IfNotFailed
    $expected = @"
$($PortfilePath): warning: The following executables were found in `${CURRENT_PACKAGES_DIR}/bin or `${CURRENT_PACKAGES_DIR}/debug/bin. Executables are not valid distribution targets. If these executables are build tools, consider using ``vcpkg_copy_tools``. To suppress this message, add set(VCPKG_POLICY_ALLOW_EXES_IN_BIN enabled)
$($packagesRoot)$($NativeSlash)test-exe_x86-windows: note: the executables are relative to `${CURRENT_PACKAGES_DIR} here
note: bin/test_exe.exe
"@

    if (-not $buildOutput.Contains($expected)) {
        throw 'Did not detect EXE in bin.'
    }

    $buildOutput = Run-VcpkgAndCaptureOutput @commonArgs install --overlay-ports="$TestingRoot/exes-in-bin" test-exe --no-binarycaching --enforce-port-checks
    Throw-IfNotFailed
    $expected = @"
$($PortfilePath): warning: The following executables were found in `${CURRENT_PACKAGES_DIR}/bin or `${CURRENT_PACKAGES_DIR}/debug/bin. Executables are not valid distribution targets. If these executables are build tools, consider using ``vcpkg_copy_tools``. To suppress this message, add set(VCPKG_POLICY_ALLOW_EXES_IN_BIN enabled)
$($packagesRoot)$($NativeSlash)test-exe_x86-windows: note: the executables are relative to `${CURRENT_PACKAGES_DIR} here
note: debug/bin/test_exe.exe
note: bin/test_exe.exe
"@

    if (-not $buildOutput.Contains($expected)) {
        throw 'Did not detect EXEs in bin.'
    }

    $buildOutput = Run-VcpkgAndCaptureOutput @commonArgs install --overlay-ports="$TestingRoot/exes-in-bin" "test-exe[policy-allow-exes-in-bin]" --no-binarycaching --enforce-port-checks
    Throw-IfFailed
    if ($buildOutput.Contains($expected)) {
        throw 'VCPKG_POLICY_ALLOW_EXES_IN_BIN didn''t suppress'
    }
} # windows

# Forgot to install usage
$PortfilePath = "$PSScriptRoot/../e2e-ports$($NativeSlash)vcpkg-policy-forgot-usage$($NativeSlash)portfile.cmake"
Refresh-TestRoot
$buildOutput = Run-VcpkgAndCaptureOutput @commonArgs install --overlay-ports="$PSScriptRoot/../e2e-ports" 'vcpkg-policy-forgot-usage' --no-binarycaching --enforce-port-checks
Throw-IfNotFailed
$expected = @"
$($PortfilePath): warning: this port contains a file named "usage" but didn't install it to `${CURRENT_PACKAGES_DIR}/share/`${PORT}/usage . If this file is not intended to be usage text, consider choosing another name; otherwise, install it. To suppress this message, add set(VCPKG_POLICY_SKIP_USAGE_INSTALL_CHECK enabled)
$PSScriptRoot/../e2e-ports$($NativeSlash)vcpkg-policy-forgot-usage$($NativeSlash)usage: note: the usage file is here
note: you can install the usage file with the following CMake
note: file(INSTALL "`${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "`${CURRENT_PACKAGES_DIR}/share/`${PORT}")
"@
if (-not $buildOutput.Contains($expected)) {
    throw 'Did not detect forgotten usage'
}
$buildOutput = Run-VcpkgAndCaptureOutput @commonArgs install --overlay-ports="$PSScriptRoot/../e2e-ports" 'vcpkg-policy-forgot-usage[policy]' --no-binarycaching --enforce-port-checks
Throw-IfFailed
if ($buildOutput.Contains($expected)) {
    throw 'VCPKG_POLICY_SKIP_USAGE_INSTALL_CHECK didn''t suppress'
}

# Mismatched debug and release
if ($IsWindows) {
    Refresh-TestRoot
    mkdir "$TestingRoot/debug-release-mismatch"
    Copy-Item -Recurse "$PSScriptRoot/../e2e-assets/test-dll-port-template" "$TestingRoot/debug-release-mismatch/test-dll"
    Run-Vcpkg @commonArgs env "$TestingRoot/debug-release-mismatch/test-dll/build.cmd"
    $PortfilePath = "$TestingRoot/debug-release-mismatch$($NativeSlash)test-dll$($NativeSlash)portfile.cmake"

$expected = @"
$($PortfilePath): warning: mismatching number of debug and release binaries. This often indicates incorrect handling of debug or release in portfile.cmake or the build system. If the intent is to only ever produce release components for this triplet, the triplet should have set(VCPKG_BUILD_TYPE release) added to its .cmake file. To suppress this message, add set(VCPKG_POLICY_MISMATCHED_NUMBER_OF_BINARIES enabled)
$($packagesRoot)$($NativeSlash)test-dll_x86-windows: note: the binaries are relative to `${CURRENT_PACKAGES_DIR} here
note: The following are debug binaries:
note: debug/lib/test_dll.lib
note: debug/bin/test_dll.dll
note: Release binaries were not found.
"@
    $buildOutput = Run-VcpkgAndCaptureOutput @commonArgs install --overlay-ports="$TestingRoot/debug-release-mismatch" 'test-dll[debug-only]' --no-binarycaching --enforce-port-checks
    Throw-IfNotFailed
    if (-not $buildOutput.Contains($expected)) {
        throw 'Did not detect debug only mismatch'
    }

$expected = @"
$($PortfilePath): warning: mismatching number of debug and release binaries. This often indicates incorrect handling of debug or release in portfile.cmake or the build system. If the intent is to only ever produce release components for this triplet, the triplet should have set(VCPKG_BUILD_TYPE release) added to its .cmake file. To suppress this message, add set(VCPKG_POLICY_MISMATCHED_NUMBER_OF_BINARIES enabled)
$($packagesRoot)$($NativeSlash)test-dll_x86-windows: note: the binaries are relative to `${CURRENT_PACKAGES_DIR} here
note: Debug binaries were not found.
note: The following are release binaries:
note: lib/test_dll.lib
note: bin/test_dll.dll
"@
    $buildOutput = Run-VcpkgAndCaptureOutput @commonArgs install --overlay-ports="$TestingRoot/debug-release-mismatch" 'test-dll[bad-release-only]' --no-binarycaching --enforce-port-checks
    Throw-IfNotFailed
    if (-not $buildOutput.Contains($expected)) {
        throw 'Did not detect release only mismatch'
    }

$expected = @"
$($PortfilePath): warning: mismatching number of debug and release binaries. This often indicates incorrect handling of debug or release in portfile.cmake or the build system. If the intent is to only ever produce release components for this triplet, the triplet should have set(VCPKG_BUILD_TYPE release) added to its .cmake file. To suppress this message, add set(VCPKG_POLICY_MISMATCHED_NUMBER_OF_BINARIES enabled)
$($packagesRoot)$($NativeSlash)test-dll_x86-windows: note: the binaries are relative to `${CURRENT_PACKAGES_DIR} here
note: The following are debug binaries:
note: debug/lib/test_dll.lib
note: debug/lib/test_dll2.lib
note: debug/bin/test_dll.dll
note: debug/bin/test_dll2.dll
note: The following are release binaries:
note: lib/test_dll.lib
note: bin/test_dll.dll
"@
    $buildOutput = Run-VcpkgAndCaptureOutput @commonArgs install --overlay-ports="$TestingRoot/debug-release-mismatch" 'test-dll[extra-debug]' --no-binarycaching --enforce-port-checks
    Throw-IfNotFailed
    if (-not $buildOutput.Contains($expected)) {
        throw 'Did not detect extra debug mismatch'
    }

$expected = @"
$($PortfilePath): warning: mismatching number of debug and release binaries. This often indicates incorrect handling of debug or release in portfile.cmake or the build system. If the intent is to only ever produce release components for this triplet, the triplet should have set(VCPKG_BUILD_TYPE release) added to its .cmake file. To suppress this message, add set(VCPKG_POLICY_MISMATCHED_NUMBER_OF_BINARIES enabled)
$($packagesRoot)$($NativeSlash)test-dll_x86-windows: note: the binaries are relative to `${CURRENT_PACKAGES_DIR} here
note: The following are debug binaries:
note: debug/lib/test_dll.lib
note: debug/bin/test_dll.dll
note: The following are release binaries:
note: lib/test_dll.lib
note: lib/test_dll2.lib
note: bin/test_dll.dll
note: bin/test_dll2.dll
"@
    $buildOutput = Run-VcpkgAndCaptureOutput @commonArgs install --overlay-ports="$TestingRoot/debug-release-mismatch" 'test-dll[extra-release]' --no-binarycaching --enforce-port-checks
    Throw-IfNotFailed
    if (-not $buildOutput.Contains($expected)) {
        throw 'Did not detect extra release mismatch'
    }

        $buildOutput = Run-VcpkgAndCaptureOutput @commonArgs install --overlay-ports="$TestingRoot/debug-release-mismatch" 'test-dll[extra-release,policy-mismatched-number-of-binaries]' --no-binarycaching --enforce-port-checks
    Throw-IfFailed
    if ($buildOutput.Contains('mismatching number of debug and release binaries')) {
        throw 'VCPKG_POLICY_MISMATCHED_NUMBER_OF_BINARIES didn''t suppress'
    }
}

# Kernel32 from XBox
if ($IsWindows) {
    Refresh-TestRoot
    mkdir "$TestingRoot/kernel32-from-xbox"
    Copy-Item -Recurse "$PSScriptRoot/../e2e-assets/test-dll-port-template" "$TestingRoot/kernel32-from-xbox/test-dll"
    Run-Vcpkg env "$TestingRoot/kernel32-from-xbox/test-dll/build.cmd" --Triplet x64-windows

    $PortfilePath = "$TestingRoot/kernel32-from-xbox$($NativeSlash)test-dll$($NativeSlash)portfile.cmake"
    $buildOutput = Run-VcpkgAndCaptureOutput --triplet x64-xbox-xboxone "--x-buildtrees-root=$buildtreesRoot" "--x-install-root=$installRoot" "--x-packages-root=$packagesRoot" install --overlay-ports="$TestingRoot/kernel32-from-xbox" test-dll --no-binarycaching --enforce-port-checks
    Throw-IfNotFailed
    $expected = @"
$($PortfilePath): warning: The selected triplet targets Xbox, but the following DLLs link with kernel32. These DLLs cannot be loaded on Xbox, where kernel32 is not present. This is typically caused by linking with kernel32.lib rather than a suitable umbrella library, such as onecore_apiset.lib or xgameplatform.lib. You can inspect a DLL's dependencies with ``dumpbin.exe /dependents mylibfile.dll``. To suppress this message, add set(VCPKG_POLICY_ALLOW_KERNEL32_FROM_XBOX enabled)
$($packagesRoot)$($NativeSlash)test-dll_x64-xbox-xboxone: note: the DLLs are relative to `${CURRENT_PACKAGES_DIR} here
note: debug/bin/test_dll.dll
note: bin/test_dll.dll
"@

    if (-not $buildOutput.Contains($expected)) {
        throw 'Did not detect Kernel32 from xbox.'
    }

    $buildOutput = Run-VcpkgAndCaptureOutput --triplet x64-xbox-xboxone "--x-buildtrees-root=$buildtreesRoot" "--x-install-root=$installRoot" "--x-packages-root=$packagesRoot" install --overlay-ports="$TestingRoot/kernel32-from-xbox" 'test-dll[policy-allow-kernel32-from-xbox]' --no-binarycaching --enforce-port-checks
    Throw-IfFailed
    if ($buildOutput.Contains($expected)) {
        throw 'VCPKG_POLICY_ALLOW_KERNEL32_FROM_XBOX didn''t suppress'
    }
} # windows

# DLLs with missing libs
if ($IsWindows) {
    Refresh-TestRoot
    mkdir "$TestingRoot/dlls-no-lib"
    Copy-Item -Recurse "$PSScriptRoot/../e2e-assets/test-dll-port-template" "$TestingRoot/dlls-no-lib/test-dll"
    Run-Vcpkg @commonArgs env "$TestingRoot/dlls-no-lib/test-dll/build.cmd"

    $PortfilePath = "$TestingRoot/dlls-no-lib$($NativeSlash)test-dll$($NativeSlash)portfile.cmake"
    $expected = @"
$($PortfilePath): warning: Import libraries for installed DLLs appear to be missing. If this is intended, add set(VCPKG_POLICY_DLLS_WITHOUT_LIBS enabled)
"@

    $buildOutput = Run-VcpkgAndCaptureOutput @commonArgs install --overlay-ports="$TestingRoot/dlls-no-lib" "test-dll[install-no-lib-debug]" --no-binarycaching --enforce-port-checks
    Throw-IfNotFailed
    if (-not $buildOutput.Contains($expected)) {
        throw 'Did not detect DLLs with no import libraries debug'
    }

    $buildOutput = Run-VcpkgAndCaptureOutput @commonArgs install --overlay-ports="$TestingRoot/dlls-no-lib" "test-dll[install-no-lib-release]" --no-binarycaching --enforce-port-checks
    Throw-IfNotFailed
    if (-not $buildOutput.Contains($expected)) {
        throw 'Did not detect DLLs with no import libraries release'
    }

    $buildOutput = Run-VcpkgAndCaptureOutput @commonArgs install --overlay-ports="$TestingRoot/dlls-no-lib" "test-dll[install-no-lib-debug,install-no-lib-release]" --no-binarycaching --enforce-port-checks
    Throw-IfNotFailed
    $buildOutput = $buildOutput
    $first = $buildOutput.IndexOf($expected)
    if ($first -lt 0) {
        throw 'Did not detect DLLs with no import libraries both'
    }
    if ($buildOutput.IndexOf($expected, $first + 1) -ge 0){
        throw 'Detected duplicate DLLs with no import libraries'
    }

    $buildOutput = Run-VcpkgAndCaptureOutput @commonArgs install --overlay-ports="$TestingRoot/dlls-in-lib" "test-dll[install-no-lib-debug,install-no-lib-release,policy-dlls-without-libs]" --no-binarycaching --enforce-port-checks
    Throw-IfNotFailed
    if ($buildOutput.Contains($expected)) {
        throw 'VCPKG_POLICY_DLLS_WITHOUT_LIBS didn''t suppress'
    }
} # windows

# DLLs in lib
if ($IsWindows) {
    Refresh-TestRoot
    mkdir "$TestingRoot/dlls-in-lib"
    Copy-Item -Recurse "$PSScriptRoot/../e2e-assets/test-dll-port-template" "$TestingRoot/dlls-in-lib/test-dll"
    Run-Vcpkg @commonArgs env "$TestingRoot/dlls-in-lib/test-dll/build.cmd"

    $PortfilePath = "$TestingRoot/dlls-in-lib$($NativeSlash)test-dll$($NativeSlash)portfile.cmake"
    $buildOutput = Run-VcpkgAndCaptureOutput @commonArgs install --overlay-ports="$TestingRoot/dlls-in-lib" "test-dll[install-to-lib]" --no-binarycaching --enforce-port-checks
    Throw-IfNotFailed
    $expected = @"
$($PortfilePath): warning: The following dlls were found in `${CURRENT_PACKAGES_DIR}/lib or `${CURRENT_PACKAGES_DIR}/debug/lib. Please move them to `${CURRENT_PACKAGES_DIR}/bin or `${CURRENT_PACKAGES_DIR}/debug/bin, respectively.
$($packagesRoot)$($NativeSlash)test-dll_$($Triplet): note: the DLLs are relative to `${CURRENT_PACKAGES_DIR} here
note: debug/lib/test_dll.dll
note: lib/test_dll.dll
"@

    if (-not $buildOutput.Contains($expected)) {
        throw 'Did not detect DLL in lib.'
    }

    $buildOutput = Run-VcpkgAndCaptureOutput @commonArgs install --overlay-ports="$TestingRoot/dlls-in-lib" "test-dll[install-to-lib,policy-allow-dlls-in-lib]" --no-binarycaching --enforce-port-checks
    Throw-IfFailed
    if ($buildOutput.Contains($expected)) {
        throw 'VCPKG_POLICY_ALLOW_DLLS_IN_LIB didn''t suppress'
    }
} # windows

# DLLs with no exports
if ($IsWindows) {
    Refresh-TestRoot
    $buildOutput = Run-VcpkgAndCaptureOutput @commonArgs install --overlay-ports="$PSScriptRoot/../e2e-ports" vcpkg-internal-dll-with-no-exports --no-binarycaching --enforce-port-checks
    Throw-IfNotFailed
    $PortfilePath = "$PSScriptRoot/../e2e-ports$($NativeSlash)vcpkg-internal-dll-with-no-exports$($NativeSlash)portfile.cmake"
    $expected = @"
$($PortfilePath): warning: the following DLLs were built without any exports. DLLs without exports are likely bugs in the build script. If this is intended, add set(VCPKG_POLICY_DLLS_WITHOUT_EXPORTS enabled)
$($packagesRoot)$($NativeSlash)vcpkg-internal-dll-with-no-exports_$($Triplet): note: the DLLs are relative to `${CURRENT_PACKAGES_DIR} here
note: debug/bin/no_exports.dll
note: bin/no_exports.dll
"@

    if (-not $buildOutput.Contains($expected)) {
        throw 'Did not detect DLLs with no exports'
    }

    Refresh-TestRoot
    $buildOutput = Run-VcpkgAndCaptureOutput @commonArgs install --overlay-ports="$PSScriptRoot/../e2e-ports" 'vcpkg-internal-dll-with-no-exports[policy]' --no-binarycaching --enforce-port-checks
    Throw-IfFailed
    if ($buildOutput.Contains($expected)) {
        throw 'VCPKG_POLICY_DLLS_WITHOUT_EXPORTS didn''t suppress'
    }
} # windows

# DLLs with wrong architecture
if ($IsWindows) {
    Refresh-TestRoot
    mkdir "$TestingRoot/wrong-architecture"
    Copy-Item -Recurse "$PSScriptRoot/../e2e-assets/test-dll-port-template" "$TestingRoot/wrong-architecture/test-dll"
    Run-Vcpkg env "$TestingRoot/wrong-architecture/test-dll/build.cmd" --Triplet x64-windows
    $PortfilePath = "$TestingRoot/wrong-architecture$($NativeSlash)test-dll$($NativeSlash)portfile.cmake"
    $buildOutput = Run-VcpkgAndCaptureOutput @commonArgs install --overlay-ports="$TestingRoot/wrong-architecture" test-dll --no-binarycaching --enforce-port-checks
    Throw-IfNotFailed
    $expected = @"
$($PortfilePath): warning: The triplet requests that binaries are built for x86, but the following binaries were built for a different architecture. This usually means toolchain information is incorrectly conveyed to the binaries' build system. To suppress this message, add set(VCPKG_POLICY_SKIP_ARCHITECTURE_CHECK enabled)
$($packagesRoot)$($NativeSlash)test-dll_$($Triplet): note: the binaries are relative to `${CURRENT_PACKAGES_DIR} here
note: debug/lib/test_dll.lib is built for x64
note: lib/test_dll.lib is built for x64
note: debug/bin/test_dll.dll is built for x64
note: bin/test_dll.dll is built for x64
"@

    if (-not $buildOutput.Contains($expected)) {
        throw 'Did not detect DLL with wrong architecture.'
    }

    $buildOutput = Run-VcpkgAndCaptureOutput @commonArgs install --overlay-ports="$TestingRoot/wrong-architecture" 'test-dll[policy-skip-architecture-check]' --no-binarycaching --enforce-port-checks
    Throw-IfFailed
    if ($buildOutput.Contains("warning: The following files were built for an incorrect architecture. To suppress this message, add set(VCPKG_POLICY_SKIP_ARCHITECTURE_CHECK enabled) to portfile.cmake.")) {
        throw 'VCPKG_POLICY_SKIP_ARCHITECTURE_CHECK didn''t suppress'
    }
} # windows

# DLLs with no AppContainer bit
if ($IsWindows) {
    Refresh-TestRoot
    mkdir "$TestingRoot/wrong-appcontainer"
    Copy-Item -Recurse "$PSScriptRoot/../e2e-assets/test-dll-port-template" "$TestingRoot/wrong-appcontainer/test-dll"
    Run-Vcpkg env "$TestingRoot/wrong-appcontainer/test-dll/build.cmd" --Triplet x64-windows

    $PortfilePath = "$TestingRoot/wrong-appcontainer$($NativeSlash)test-dll$($NativeSlash)portfile.cmake"
    $buildOutput = Run-VcpkgAndCaptureOutput --triplet x64-uwp "--x-buildtrees-root=$buildtreesRoot" "--x-install-root=$installRoot" "--x-packages-root=$packagesRoot" install --overlay-ports="$TestingRoot/wrong-appcontainer" test-dll --no-binarycaching --enforce-port-checks
    Throw-IfNotFailed
    $expected = @"
$($PortfilePath): warning: The App Container bit must be set for all DLLs in Windows Store apps, and the triplet requests targeting the Windows Store, but the following DLLs were not built with the bit set. This usually means that toolchain linker flags are not being properly propagated, or the linker in use does not support the /APPCONTAINER switch. To suppress this message, add set(VCPKG_POLICY_SKIP_APPCONTAINER_CHECK enabled)
$($packagesRoot)$($NativeSlash)test-dll_x64-uwp: note: the DLLs are relative to `${CURRENT_PACKAGES_DIR} here
note: debug/bin/test_dll.dll
note: bin/test_dll.dll
"@

    if (-not $buildOutput.Contains($expected)) {
        throw 'Did not detect DLL with wrong appcontainer.'
    }

    $buildOutput = Run-VcpkgAndCaptureOutput --triplet x64-uwp "--x-buildtrees-root=$buildtreesRoot" "--x-install-root=$installRoot" "--x-packages-root=$packagesRoot" install --overlay-ports="$TestingRoot/wrong-appcontainer" 'test-dll[policy-skip-appcontainer-check]' --no-binarycaching --enforce-port-checks
    Throw-IfFailed
    if ($buildOutput.Contains("warning: The App Container bit must be set")) {
        throw 'VCPKG_POLICY_SKIP_APPCONTAINER_CHECK didn''t suppress'
    }
} # windows

# Obsolete CRTs
if ($IsWindows) {
    Refresh-TestRoot
    $PortfilePath = "$PSScriptRoot/../e2e-ports$($NativeSlash)vcpkg-msvc-2013$($NativeSlash)portfile.cmake"
    $expected = @"
$($PortfilePath): warning: DLLs that link with obsolete C RunTime ("CRT") DLLs were installed. Installed DLLs should link with an in-support CRT. You can inspect the dependencies of a DLL with ``dumpbin.exe /dependents mylibfile.dll``. If you're using a custom triplet targeting an old CRT, add set(VCPKG_POLICY_ALLOW_OBSOLETE_MSVCRT enabled) to the triplet's .cmake file. To suppress this message for this port, add set(VCPKG_POLICY_ALLOW_OBSOLETE_MSVCRT enabled)
$($packagesRoot)$($NativeSlash)vcpkg-msvc-2013_x86-windows: note: the DLLs are relative to `${CURRENT_PACKAGES_DIR} here
note: debug/bin/test_dll.dll
note: bin/test_dll.dll
"@

    $buildOutput = Run-VcpkgAndCaptureOutput --triplet x86-windows "--x-buildtrees-root=$buildtreesRoot" "--x-install-root=$installRoot" "--x-packages-root=$packagesRoot" install --overlay-ports="$PSScriptRoot/../e2e-ports" 'vcpkg-msvc-2013' --no-binarycaching --enforce-port-checks
    Throw-IfNotFailed
    if (-not $buildOutput.Contains($expected)) {
        throw 'Did not detect obsolete CRT.'
    }

    $buildOutput = Run-VcpkgAndCaptureOutput --triplet x86-windows "--x-buildtrees-root=$buildtreesRoot" "--x-install-root=$installRoot" "--x-packages-root=$packagesRoot" install --overlay-ports="$PSScriptRoot/../e2e-ports" 'vcpkg-msvc-2013[policy]' --no-binarycaching --enforce-port-checks
    Throw-IfFailed
    if ($buildOutput.Contains($expected)) {
        throw 'VCPKG_POLICY_ALLOW_OBSOLETE_MSVCRT did not suppress'
    }

    Refresh-TestRoot
    $expected = @"
$($PortfilePath): warning: DLLs that link with obsolete C RunTime ("CRT") DLLs were installed. Installed DLLs should link with an in-support CRT. You can inspect the dependencies of a DLL with ``dumpbin.exe /dependents mylibfile.dll``. If you're using a custom triplet targeting an old CRT, add set(VCPKG_POLICY_ALLOW_OBSOLETE_MSVCRT enabled) to the triplet's .cmake file. To suppress this message for this port, add set(VCPKG_POLICY_ALLOW_OBSOLETE_MSVCRT enabled)
$($packagesRoot)$($NativeSlash)vcpkg-msvc-2013_x86-windows: note: the DLLs are relative to `${CURRENT_PACKAGES_DIR} here
note: bin/test_dll.dll
"@

    $buildOutput = Run-VcpkgAndCaptureOutput --triplet x86-windows "--x-buildtrees-root=$buildtreesRoot" "--x-install-root=$installRoot" "--x-packages-root=$packagesRoot" install --overlay-ports="$PSScriptRoot/../e2e-ports" 'vcpkg-msvc-2013[release-only]' --no-binarycaching --enforce-port-checks
    Throw-IfNotFailed
    if (-not $buildOutput.Contains($expected)) {
        throw 'Did not detect obsolete CRT.'
    }
} # windows

# DLLs in static mode
if ($IsWindows) {
    Refresh-TestRoot
    mkdir "$TestingRoot/dlls-in-static"
    Copy-Item -Recurse "$PSScriptRoot/../e2e-assets/test-dll-port-template" "$TestingRoot/dlls-in-static/test-dll"
    Run-Vcpkg @directoryArgs env "$TestingRoot/dlls-in-static/test-dll/build.cmd" --triplet x64-windows

    $PortfilePath = "$TestingRoot/dlls-in-static$($NativeSlash)test-dll$($NativeSlash)portfile.cmake"
    $buildOutput = Run-VcpkgAndCaptureOutput @directoryArgs install --triplet x64-windows-static --overlay-ports="$TestingRoot/dlls-in-static" "test-dll[release-only]" --no-binarycaching --enforce-port-checks
    Throw-IfNotFailed
    $expected = @"
$($PortfilePath): warning: DLLs should not be present in a static build, but the following DLLs were found. To suppress this message, add set(VCPKG_POLICY_DLLS_IN_STATIC_LIBRARY enabled)
$($packagesRoot)$($NativeSlash)test-dll_x64-windows-static: note: the DLLs are relative to `${CURRENT_PACKAGES_DIR} here
note: bin/test_dll.dll
$($PortfilePath): warning: `${CURRENT_PACKAGES_DIR}/bin exists but should not in a static build. To suppress this message, add set(VCPKG_POLICY_DLLS_IN_STATIC_LIBRARY enabled)
note: if creation of these directories cannot be disabled, you can add the following in portfile.cmake to remove them
if(VCPKG_LIBRARY_LINKAGE STREQUAL "static")
  file(REMOVE_RECURSE "`${CURRENT_PACKAGES_DIR}/bin")
endif()
"@

    if (-not $buildOutput.Contains($expected)) {
        throw 'Did not detect DLL in static release-only.'
    } 


    $buildOutput = Run-VcpkgAndCaptureOutput @directoryArgs install --triplet x64-windows-static --overlay-ports="$TestingRoot/dlls-in-static" "test-dll" --no-binarycaching --enforce-port-checks
    Throw-IfNotFailed
    $expected = @"
$($PortfilePath): warning: DLLs should not be present in a static build, but the following DLLs were found. To suppress this message, add set(VCPKG_POLICY_DLLS_IN_STATIC_LIBRARY enabled)
$($packagesRoot)$($NativeSlash)test-dll_x64-windows-static: note: the DLLs are relative to `${CURRENT_PACKAGES_DIR} here
note: debug/bin/test_dll.dll
note: bin/test_dll.dll
$($PortfilePath): warning: `${CURRENT_PACKAGES_DIR}/debug/bin exists but should not in a static build. To suppress this message, add set(VCPKG_POLICY_DLLS_IN_STATIC_LIBRARY enabled)
$($PortfilePath): warning: `${CURRENT_PACKAGES_DIR}/bin exists but should not in a static build. To suppress this message, add set(VCPKG_POLICY_DLLS_IN_STATIC_LIBRARY enabled)
note: if creation of these directories cannot be disabled, you can add the following in portfile.cmake to remove them
if(VCPKG_LIBRARY_LINKAGE STREQUAL "static")
  file(REMOVE_RECURSE "`${CURRENT_PACKAGES_DIR}/debug/bin" "`${CURRENT_PACKAGES_DIR}/bin")
endif()
"@

    if (-not $buildOutput.Contains($expected)) {
        throw 'Did not detect DLL in static.'
    }

    $buildOutput = Run-VcpkgAndCaptureOutput @directoryArgs install --triplet x64-windows-static --overlay-ports="$TestingRoot/dlls-in-static" "test-dll[policy-dlls-in-static-library]" --no-binarycaching --enforce-port-checks
    Throw-IfFailed
    if ($buildOutput.Contains($expected)) {
        throw 'VCPKG_POLICY_DLLS_IN_STATIC_LIBRARY didn''t suppress'
    }
} # windows

# Wrong CRT linkage
if ($IsWindows) {
    Refresh-TestRoot
    mkdir "$TestingRoot/wrong-crt"
    Copy-Item -Recurse "$PSScriptRoot/../e2e-assets/test-lib-port-template-dynamic-crt" "$TestingRoot/wrong-crt/test-lib"
    Run-Vcpkg env "$TestingRoot/wrong-crt/test-lib/build.cmd" --Triplet x86-windows-static
    $PortfilePath = "$TestingRoot/wrong-crt$($NativeSlash)test-lib$($NativeSlash)portfile.cmake"
    $buildOutput = Run-VcpkgAndCaptureOutput --triplet x86-windows-static "--x-buildtrees-root=$buildtreesRoot" "--x-install-root=$installRoot" "--x-packages-root=$packagesRoot" install --overlay-ports="$TestingRoot/wrong-crt" test-lib --no-binarycaching --enforce-port-checks
    Throw-IfNotFailed
$expected = @"
$($PortfilePath): warning: binaries built by this port link with C RunTimes ("CRTs") inconsistent with those requested by the triplet and deployment structure. If the triplet is intended to only use the release CRT, you should add set(VCPKG_POLICY_ONLY_RELEASE_CRT enabled) to the triplet .cmake file. To suppress this check entirely, add set(VCPKG_POLICY_SKIP_CRT_LINKAGE_CHECK enabled) to the triplet .cmake if this is triplet-wide, or to portfile.cmake if this is specific to the port. You can inspect the binaries with: dumpbin.exe /directives mylibfile.lib
$packagesRoot\test-lib_x86-windows-static: note: the binaries are relative to `${CURRENT_PACKAGES_DIR} here
note: The following binaries should link with only: Static Debug (/MTd)
note: debug/lib/both_lib.lib links with: Dynamic Debug (/MDd)
note: debug/lib/both_lib.lib links with: Dynamic Release (/MD)
note: debug/lib/test_lib.lib links with: Dynamic Debug (/MDd)
note: The following binaries should link with only: Static Release (/MT)
note: lib/both_lib.lib links with: Dynamic Debug (/MDd)
note: lib/both_lib.lib links with: Dynamic Release (/MD)
note: lib/test_lib.lib links with: Dynamic Release (/MD)
"@

    if (-not $buildOutput.Contains($expected)) {
        throw 'Did not detect lib with wrong CRT linkage.'
    }

    $buildOutput = Run-VcpkgAndCaptureOutput --triplet x86-windows-static "--x-buildtrees-root=$buildtreesRoot" "--x-install-root=$installRoot" "--x-packages-root=$packagesRoot" install --overlay-ports="$TestingRoot/wrong-crt" 'test-lib[policy-skip-crt-linkage-check]' --no-binarycaching --enforce-port-checks
    Throw-IfFailed
    if ($buildOutput.Contains('warning: The following binaries should use the Static Debug (/MTd) CRT')) {
        throw 'VCPKG_POLICY_SKIP_CRT_LINKAGE_CHECK didn''t suppress'
    }

# ... also release only
    Refresh-TestRoot
    mkdir "$TestingRoot/wrong-crt-release-only"
    $PortfilePath = "$TestingRoot/wrong-crt-release-only$($NativeSlash)test-lib$($NativeSlash)portfile.cmake"
    Copy-Item -Recurse "$PSScriptRoot/../e2e-assets/test-lib-port-template-dynamic-crt-release-only" "$TestingRoot/wrong-crt-release-only/test-lib"
    Run-Vcpkg env "$TestingRoot/wrong-crt-release-only/test-lib/build.cmd" --Triplet x86-windows-static

    $buildOutput = Run-VcpkgAndCaptureOutput --triplet x86-windows-static "--x-buildtrees-root=$buildtreesRoot" "--x-install-root=$installRoot" "--x-packages-root=$packagesRoot" install --overlay-ports="$TestingRoot/wrong-crt-release-only" test-lib --no-binarycaching --enforce-port-checks
    Throw-IfNotFailed
$expected = @"
$($PortfilePath): warning: binaries built by this port link with C RunTimes ("CRTs") inconsistent with those requested by the triplet and deployment structure. If the triplet is intended to only use the release CRT, you should add set(VCPKG_POLICY_ONLY_RELEASE_CRT enabled) to the triplet .cmake file. To suppress this check entirely, add set(VCPKG_POLICY_SKIP_CRT_LINKAGE_CHECK enabled) to the triplet .cmake if this is triplet-wide, or to portfile.cmake if this is specific to the port. You can inspect the binaries with: dumpbin.exe /directives mylibfile.lib
$packagesRoot\test-lib_x86-windows-static: note: the binaries are relative to `${CURRENT_PACKAGES_DIR} here
note: The following binaries should link with only: Static Debug (/MTd)
note: debug/lib/test_lib.lib links with: Dynamic Release (/MD)
note: The following binaries should link with only: Static Release (/MT)
note: lib/test_lib.lib links with: Dynamic Release (/MD)
"@

    if (-not $buildOutput.Contains($expected)) {
        throw 'Did not detect lib with wrong CRT linkage release only.'
    }

    $buildOutput = Run-VcpkgAndCaptureOutput --triplet x86-windows-static "--x-buildtrees-root=$buildtreesRoot" "--x-install-root=$installRoot" "--x-packages-root=$packagesRoot" install --overlay-ports="$TestingRoot/wrong-crt-release-only" 'test-lib[policy-only-release-crt]' --no-binarycaching --enforce-port-checks
    Throw-IfNotFailed
$expected = @"
$($PortfilePath): warning: binaries built by this port link with C RunTimes ("CRTs") inconsistent with those requested by the triplet and deployment structure. If the triplet is intended to only use the release CRT, you should add set(VCPKG_POLICY_ONLY_RELEASE_CRT enabled) to the triplet .cmake file. To suppress this check entirely, add set(VCPKG_POLICY_SKIP_CRT_LINKAGE_CHECK enabled) to the triplet .cmake if this is triplet-wide, or to portfile.cmake if this is specific to the port. You can inspect the binaries with: dumpbin.exe /directives mylibfile.lib
$packagesRoot\test-lib_x86-windows-static: note: the binaries are relative to `${CURRENT_PACKAGES_DIR} here
note: The following binaries should link with only: Static Release (/MT)
note: debug/lib/test_lib.lib links with: Dynamic Release (/MD)
note: lib/test_lib.lib links with: Dynamic Release (/MD)
"@

    if (-not $buildOutput.Contains($expected)) {
        throw 'Did not detect lib with wrong CRT linkage release only.'
    }

    if ($buildOutput.Contains('warning: The following binaries should use the Static Debug (/MTd) CRT')) {
        throw 'VCPKG_POLICY_ONLY_RELEASE_CRT didn''t suppress detecting debug CRTs'
    }

    $buildOutput = Run-VcpkgAndCaptureOutput --triplet x86-windows-static "--x-buildtrees-root=$buildtreesRoot" "--x-install-root=$installRoot" "--x-packages-root=$packagesRoot" install --overlay-ports="$TestingRoot/wrong-crt-release-only" 'test-lib[policy-skip-crt-linkage-check]' --no-binarycaching --enforce-port-checks
    Throw-IfFailed
    if ($buildOutput.Contains('warning: The following binaries should use the Static Release (/MT) CRT')) {
        throw 'VCPKG_POLICY_SKIP_CRT_LINKAGE_CHECK didn''t suppress'
    }
} # windows

# Empty folders
Refresh-TestRoot
$PortfilePath = "$PSScriptRoot/../e2e-ports$($NativeSlash)vcpkg-policy-empty-folders$($NativeSlash)portfile.cmake"
$expected = @"
$($PortfilePath): warning: There should be no installed empty directories. Empty directories are not representable to several binary cache providers, git repositories, and are not considered semantic build outputs. You should either create a regular file inside each empty directory, or delete them with the following CMake. To suppress this message, add set(VCPKG_POLICY_ALLOW_EMPTY_FOLDERS enabled)
$($packagesRoot)$($NativeSlash)vcpkg-policy-empty-folders_$($Triplet): note: the directories are relative to `${CURRENT_PACKAGES_DIR} here
note: file(REMOVE_RECURSE "`${CURRENT_PACKAGES_DIR}/empty-directory" "`${CURRENT_PACKAGES_DIR}/root/empty-inner-directory")
"@
$buildOutput = Run-VcpkgAndCaptureOutput @commonArgs install --overlay-ports="$PSScriptRoot/../e2e-ports" 'vcpkg-policy-empty-folders' --no-binarycaching --enforce-port-checks
Throw-IfNotFailed
if (-not $buildOutput.Contains($expected)) {
    throw 'Did not detect empty directories'
}

$buildOutput = Run-VcpkgAndCaptureOutput @commonArgs install --overlay-ports="$PSScriptRoot/../e2e-ports" 'vcpkg-policy-empty-folders[policy]' --no-binarycaching --enforce-port-checks
Throw-IfFailed
if ($buildOutput.Contains($expected)) {
    throw 'VCPKG_POLICY_ALLOW_EMPTY_FOLDERS didn''t suppress'
}

# Misplaced regular files
Refresh-TestRoot
$PortfilePath = "$PSScriptRoot/../e2e-ports$($NativeSlash)vcpkg-policy-misplaced-regular-files$($NativeSlash)portfile.cmake"
$buildOutput = Run-VcpkgAndCaptureOutput @commonArgs install --overlay-ports="$PSScriptRoot/../e2e-ports" 'vcpkg-policy-misplaced-regular-files' --no-binarycaching --enforce-port-checks
Throw-IfNotFailed
$expected = @"
$($PortfilePath): warning: The following regular files are installed to location(s) where regular files may not be installed. These should be installed in a subdirectory. To suppress this message, add set(VCPKG_POLICY_SKIP_MISPLACED_REGULAR_FILES_CHECK enabled)
$($packagesRoot)$($NativeSlash)vcpkg-policy-misplaced-regular-files_$($Triplet): note: the files are relative to `${CURRENT_PACKAGES_DIR} here
note: debug/bad_debug_file.txt
note: bad_file.txt
"@
if (-not $buildOutput.Contains($expected)) {
    throw 'Did not detect bad regular files'
}
$buildOutput = Run-VcpkgAndCaptureOutput @commonArgs install --overlay-ports="$PSScriptRoot/../e2e-ports" 'vcpkg-policy-misplaced-regular-files[policy]' --no-binarycaching --enforce-port-checks
Throw-IfFailed
if ($buildOutput.Contains($expected)) {
    throw 'VCPKG_POLICY_SKIP_MISPLACED_REGULAR_FILES_CHECK didn''t suppress'
}

# Misplaced pkgconfig
Refresh-TestRoot
$PortfilePath = "$PSScriptRoot/../e2e-ports$($NativeSlash)vcpkg-policy-misplaced-pkgconfig$($NativeSlash)portfile.cmake"
$buildOutput = Run-VcpkgAndCaptureOutput @commonArgs install --overlay-ports="$PSScriptRoot/../e2e-ports" 'vcpkg-policy-misplaced-pkgconfig[install-arch-dependent-bad-misplaced]' --no-binarycaching --enforce-port-checks
Throw-IfNotFailed
$expected = @"
$($PortfilePath): warning: The following misplaced pkgconfig directories were installed. Misplaced pkgconfig files will not be found correctly by pkgconf or pkg-config. pkgconfig directories should be `${CURRENT_PACKAGES_DIR}/share/pkgconfig (for architecture agnostic / header only libraries only), `${CURRENT_PACKAGES_DIR}/lib/pkgconfig (for release dependencies), or `${CURRENT_PACKAGES_DIR}/debug/lib/pkgconfig (for debug dependencies). To suppress this message, add set(VCPKG_POLICY_SKIP_PKGCONFIG_CHECK enabled)
$($packagesRoot)$($NativeSlash)vcpkg-policy-misplaced-pkgconfig_$($Triplet): note: the files are relative to `${CURRENT_PACKAGES_DIR} here
note: bin/pkgconfig/zlib.pc
note: debug/bin/pkgconfig/zlib.pc
note: You can move the pkgconfig files with commands similar to:
file(MAKE_DIRECTORY "`${CURRENT_PACKAGES_DIR}/debug/lib/pkgconfig" "`${CURRENT_PACKAGES_DIR}/lib/pkgconfig")
file(RENAME "`${CURRENT_PACKAGES_DIR}/bin/pkgconfig/zlib.pc" "`${CURRENT_PACKAGES_DIR}/lib/pkgconfig/zlib.pc")
file(RENAME "`${CURRENT_PACKAGES_DIR}/debug/bin/pkgconfig/zlib.pc" "`${CURRENT_PACKAGES_DIR}/debug/lib/pkgconfig/zlib.pc")
vcpkg_fixup_pkgconfig()
file(REMOVE_RECURSE empty directories left by the above renames)
"@
if (-not $buildOutput.Contains($expected)) {
    throw 'Did not detect bad pkgconfig misplaced'
}
$buildOutput = Run-VcpkgAndCaptureOutput @commonArgs install --overlay-ports="$PSScriptRoot/../e2e-ports" 'vcpkg-policy-misplaced-pkgconfig[install-arch-agnostic-bad-misplaced,install-arch-agnostic-empty-libs-bad-misplaced,install-arch-dependent-bad-misplaced,install-arch-dependent-bad-share]' --no-binarycaching --enforce-port-checks
Throw-IfNotFailed
$expected = @"
$($PortfilePath): warning: The following misplaced pkgconfig directories were installed. Misplaced pkgconfig files will not be found correctly by pkgconf or pkg-config. pkgconfig directories should be `${CURRENT_PACKAGES_DIR}/share/pkgconfig (for architecture agnostic / header only libraries only), `${CURRENT_PACKAGES_DIR}/lib/pkgconfig (for release dependencies), or `${CURRENT_PACKAGES_DIR}/debug/lib/pkgconfig (for debug dependencies). To suppress this message, add set(VCPKG_POLICY_SKIP_PKGCONFIG_CHECK enabled)
$($packagesRoot)$($NativeSlash)vcpkg-policy-misplaced-pkgconfig_$($Triplet): note: the files are relative to `${CURRENT_PACKAGES_DIR} here
note: bin/pkgconfig/libmorton.pc
note: bin/pkgconfig/zlib-no-libs.pc
note: bin/pkgconfig/zlib.pc
note: debug/bin/pkgconfig/zlib.pc
note: share/pkgconfig/zlib.pc
note: You can move the pkgconfig files with commands similar to:
file(MAKE_DIRECTORY "`${CURRENT_PACKAGES_DIR}/debug/lib/pkgconfig" "`${CURRENT_PACKAGES_DIR}/lib/pkgconfig" "`${CURRENT_PACKAGES_DIR}/share/pkgconfig")
file(RENAME "`${CURRENT_PACKAGES_DIR}/bin/pkgconfig/libmorton.pc" "`${CURRENT_PACKAGES_DIR}/share/pkgconfig/libmorton.pc")
file(RENAME "`${CURRENT_PACKAGES_DIR}/bin/pkgconfig/zlib-no-libs.pc" "`${CURRENT_PACKAGES_DIR}/share/pkgconfig/zlib-no-libs.pc")
file(RENAME "`${CURRENT_PACKAGES_DIR}/bin/pkgconfig/zlib.pc" "`${CURRENT_PACKAGES_DIR}/lib/pkgconfig/zlib.pc")
file(RENAME "`${CURRENT_PACKAGES_DIR}/debug/bin/pkgconfig/zlib.pc" "`${CURRENT_PACKAGES_DIR}/debug/lib/pkgconfig/zlib.pc")
file(RENAME "`${CURRENT_PACKAGES_DIR}/share/pkgconfig/zlib.pc" "`${CURRENT_PACKAGES_DIR}/lib/pkgconfig/zlib.pc")
vcpkg_fixup_pkgconfig()
file(REMOVE_RECURSE empty directories left by the above renames)
"@
if (-not $buildOutput.Contains($expected)) {
    throw 'Did not detect bad pkgconfig all bad'
}
$buildOutput = Run-VcpkgAndCaptureOutput @commonArgs install --overlay-ports="$PSScriptRoot/../e2e-ports" 'vcpkg-policy-misplaced-pkgconfig[install-arch-agnostic-bad-misplaced,install-arch-agnostic-empty-libs-bad-misplaced,install-arch-dependent-bad-misplaced,install-arch-dependent-bad-share,policy]' --no-binarycaching --enforce-port-checks
Throw-IfFailed
if ($buildOutput.Contains($expected)) {
    throw 'VCPKG_POLICY_SKIP_PKGCONFIG_CHECK didn''t suppress'
}

Refresh-TestRoot

$buildOutput = Run-VcpkgAndCaptureOutput @commonArgs install --overlay-ports="$PSScriptRoot/../e2e-ports" 'vcpkg-policy-misplaced-pkgconfig[install-arch-dependent-bad-misplaced-release-only]' --no-binarycaching --enforce-port-checks
Throw-IfNotFailed
$expected = @"
$($PortfilePath): warning: The following misplaced pkgconfig directories were installed. Misplaced pkgconfig files will not be found correctly by pkgconf or pkg-config. pkgconfig directories should be `${CURRENT_PACKAGES_DIR}/share/pkgconfig (for architecture agnostic / header only libraries only), `${CURRENT_PACKAGES_DIR}/lib/pkgconfig (for release dependencies), or `${CURRENT_PACKAGES_DIR}/debug/lib/pkgconfig (for debug dependencies). To suppress this message, add set(VCPKG_POLICY_SKIP_PKGCONFIG_CHECK enabled)
$($packagesRoot)$($NativeSlash)vcpkg-policy-misplaced-pkgconfig_$($Triplet): note: the files are relative to `${CURRENT_PACKAGES_DIR} here
note: bin/pkgconfig/zlib.pc
note: You can move the pkgconfig files with commands similar to:
file(MAKE_DIRECTORY "`${CURRENT_PACKAGES_DIR}/lib/pkgconfig")
file(RENAME "`${CURRENT_PACKAGES_DIR}/bin/pkgconfig/zlib.pc" "`${CURRENT_PACKAGES_DIR}/lib/pkgconfig/zlib.pc")
vcpkg_fixup_pkgconfig()
file(REMOVE_RECURSE empty directories left by the above renames)
"@
if (-not $buildOutput.Contains($expected)) {
    throw 'Did not detect bad pkgconfig release only'
}

$buildOutput = Run-VcpkgAndCaptureOutput @commonArgs install --overlay-ports="$PSScriptRoot/../e2e-ports" 'vcpkg-policy-misplaced-pkgconfig[install-arch-agnostic-bad-misplaced]' --no-binarycaching --enforce-port-checks
Throw-IfNotFailed
$expected = @"
$($PortfilePath): warning: The following misplaced pkgconfig directories were installed. Misplaced pkgconfig files will not be found correctly by pkgconf or pkg-config. pkgconfig directories should be `${CURRENT_PACKAGES_DIR}/share/pkgconfig (for architecture agnostic / header only libraries only), `${CURRENT_PACKAGES_DIR}/lib/pkgconfig (for release dependencies), or `${CURRENT_PACKAGES_DIR}/debug/lib/pkgconfig (for debug dependencies). To suppress this message, add set(VCPKG_POLICY_SKIP_PKGCONFIG_CHECK enabled)
$($packagesRoot)$($NativeSlash)vcpkg-policy-misplaced-pkgconfig_$($Triplet): note: the files are relative to `${CURRENT_PACKAGES_DIR} here
note: bin/pkgconfig/libmorton.pc
note: You can move the pkgconfig files with commands similar to:
file(MAKE_DIRECTORY "`${CURRENT_PACKAGES_DIR}/share/pkgconfig")
file(RENAME "`${CURRENT_PACKAGES_DIR}/bin/pkgconfig/libmorton.pc" "`${CURRENT_PACKAGES_DIR}/share/pkgconfig/libmorton.pc")
vcpkg_fixup_pkgconfig()
file(REMOVE_RECURSE empty directories left by the above renames)
"@
if (-not $buildOutput.Contains($expected)) {
    throw 'Did not detect bad pkgconfig arch agnostic'
}

$buildOutput = Run-VcpkgAndCaptureOutput @commonArgs install --overlay-ports="$PSScriptRoot/../e2e-ports" 'vcpkg-policy-misplaced-pkgconfig[install-arch-agnostic-empty-libs-bad-misplaced]' --no-binarycaching --enforce-port-checks
Throw-IfNotFailed
$expected = @"
$($PortfilePath): warning: The following misplaced pkgconfig directories were installed. Misplaced pkgconfig files will not be found correctly by pkgconf or pkg-config. pkgconfig directories should be `${CURRENT_PACKAGES_DIR}/share/pkgconfig (for architecture agnostic / header only libraries only), `${CURRENT_PACKAGES_DIR}/lib/pkgconfig (for release dependencies), or `${CURRENT_PACKAGES_DIR}/debug/lib/pkgconfig (for debug dependencies). To suppress this message, add set(VCPKG_POLICY_SKIP_PKGCONFIG_CHECK enabled)
$($packagesRoot)$($NativeSlash)vcpkg-policy-misplaced-pkgconfig_$($Triplet): note: the files are relative to `${CURRENT_PACKAGES_DIR} here
note: bin/pkgconfig/zlib-no-libs.pc
note: You can move the pkgconfig files with commands similar to:
file(MAKE_DIRECTORY "`${CURRENT_PACKAGES_DIR}/share/pkgconfig")
file(RENAME "`${CURRENT_PACKAGES_DIR}/bin/pkgconfig/zlib-no-libs.pc" "`${CURRENT_PACKAGES_DIR}/share/pkgconfig/zlib-no-libs.pc")
vcpkg_fixup_pkgconfig()
file(REMOVE_RECURSE empty directories left by the above renames)
"@
if (-not $buildOutput.Contains($expected)) {
    throw 'Did not detect bad pkgconfig arch agnostic empty libs'
}

$buildOutput = Run-VcpkgAndCaptureOutput @commonArgs install --overlay-ports="$PSScriptRoot/../e2e-ports" 'vcpkg-policy-misplaced-pkgconfig[install-arch-dependent-bad-share]' --no-binarycaching --enforce-port-checks
Throw-IfNotFailed
$expected = @"
$($PortfilePath): warning: The following misplaced pkgconfig directories were installed. Misplaced pkgconfig files will not be found correctly by pkgconf or pkg-config. pkgconfig directories should be `${CURRENT_PACKAGES_DIR}/share/pkgconfig (for architecture agnostic / header only libraries only), `${CURRENT_PACKAGES_DIR}/lib/pkgconfig (for release dependencies), or `${CURRENT_PACKAGES_DIR}/debug/lib/pkgconfig (for debug dependencies). To suppress this message, add set(VCPKG_POLICY_SKIP_PKGCONFIG_CHECK enabled)
$($packagesRoot)$($NativeSlash)vcpkg-policy-misplaced-pkgconfig_$($Triplet): note: the files are relative to `${CURRENT_PACKAGES_DIR} here
note: share/pkgconfig/zlib.pc
note: You can move the pkgconfig files with commands similar to:
file(MAKE_DIRECTORY "`${CURRENT_PACKAGES_DIR}/lib/pkgconfig")
file(RENAME "`${CURRENT_PACKAGES_DIR}/share/pkgconfig/zlib.pc" "`${CURRENT_PACKAGES_DIR}/lib/pkgconfig/zlib.pc")
vcpkg_fixup_pkgconfig()
file(REMOVE_RECURSE empty directories left by the above renames)
"@
if (-not $buildOutput.Contains($expected)) {
    throw 'Did not detect bad pkgconfig arch dependent share'
}

# ... and all good places
Refresh-TestRoot
Run-VcpkgAndCaptureOutput @commonArgs install --overlay-ports="$PSScriptRoot/../e2e-ports" 'vcpkg-policy-misplaced-pkgconfig[install-arch-agnostic-empty-libs-good,install-arch-agnostic-empty-libs-good-share,install-arch-agnostic-good-share,install-arch-dependent-good]' --no-binarycaching --enforce-port-checks
Throw-IfFailed

# Absolute paths
Refresh-TestRoot
$PortfilePath = "$PSScriptRoot/../e2e-ports$($NativeSlash)vcpkg-policy-absolute-paths$($NativeSlash)portfile.cmake"
$expectedHeader = @"
$($PortfilePath): warning: There should be no absolute paths, such as the following, in an installed package. To suppress this message, add set(VCPKG_POLICY_SKIP_ABSOLUTE_PATHS_CHECK enabled)
note: $($packagesRoot)
note: $($installRoot)
note: $($buildtreesRoot)
"@
# downloads directory here
$expectedFooter = @"
$($packagesRoot)$($NativeSlash)vcpkg-policy-absolute-paths_$($Triplet)$($NativeSlash)include$($NativeSlash)vcpkg-policy-absolute-paths.h: note: absolute paths found here
"@

foreach ($bad_dir in @('build-dir', 'downloads', 'installed-root', 'package-dir')) {
    $buildOutput = Run-VcpkgAndCaptureOutput @commonArgs install --overlay-ports="$PSScriptRoot/../e2e-ports" "vcpkg-policy-absolute-paths[$bad_dir]" --no-binarycaching --enforce-port-checks
    Throw-IfNotFailed
    if (-not $buildOutput.Contains($expectedHeader)) {
        throw 'Did not detect bad absolute paths header'
    }
    if (-not $buildOutput.Contains($expectedFooter)) {
        throw 'Did not detect bad absolute paths footer'
    }
}

$expectedFooter = @"
$($PortfilePath): note: Adding a call to ``vcpkg_fixup_pkgconfig()`` may fix absolute paths in .pc files
$($packagesRoot)$($NativeSlash)vcpkg-policy-absolute-paths_$($Triplet)$($NativeSlash)include$($NativeSlash)vcpkg-policy-absolute-paths.h: note: absolute paths found here
$($packagesRoot)$($NativeSlash)vcpkg-policy-absolute-paths_$($Triplet)$($NativeSlash)share$($NativeSlash)pkgconfig$($NativeSlash)vcpkg-policy-absolute-paths.pc: note: absolute paths found here
$($packagesRoot)$($NativeSlash)vcpkg-policy-absolute-paths_$($Triplet)$($NativeSlash)tools$($NativeSlash)vcpkg-policy-absolute-paths$($NativeSlash)bin$($NativeSlash)port-config.sh: note: absolute paths found here
"@

foreach ($bad_dir in @('build-dir', 'downloads', 'installed-root', 'package-dir')) {
    $buildOutput = Run-VcpkgAndCaptureOutput @commonArgs install --overlay-ports="$PSScriptRoot/../e2e-ports" "vcpkg-policy-absolute-paths[$bad_dir,pkgconfig]" --no-binarycaching --enforce-port-checks
    Throw-IfNotFailed
    if (-not $buildOutput.Contains($expectedHeader)) {
        throw 'Did not detect bad absolute paths header'
    }
    if (-not $buildOutput.Contains($expectedFooter)) {
        throw 'Did not detect bad absolute paths pkgconfig footer'
    }
}

Run-Vcpkg @commonArgs install --overlay-ports="$PSScriptRoot/../e2e-ports" 'vcpkg-policy-absolute-paths[build-dir,downloads,installed-root,package-dir,pkgconfig,policy]' --no-binarycaching --enforce-port-checks
Throw-IfFailed

# Conflicting files. Note that the test ports here don't actually conflict due to different CaSe on Linux but do on Windows and macOS
# Also note that the case chosen is the case from a-conflict not b-conflict, since we are claiming 'installed by a-conflict'
Refresh-TestRoot
$buildOutput = Run-VcpkgAndCaptureOutput @commonArgs install --overlay-ports="$PSScriptRoot/../e2e-ports" a-conflict b-conflict --no-binarycaching --enforce-port-checks
Throw-IfNotFailed
$forwardInstalled = $installRoot.Replace('\', '/')
$expected = @"
error: The following files are already installed in $($forwardInstalled)/$($Triplet) and are in conflict with b-conflict:$($Triplet)
Installed by a-conflict:$($Triplet):
  include/CONFLICT-a-header-ONLY-mixed.h
  include/CONFLICT-a-header-ONLY-mixed2.h
"@
Throw-IfNonContains -Expected $expected -Actual $buildOutput
