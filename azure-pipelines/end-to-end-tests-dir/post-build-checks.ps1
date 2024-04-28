. $PSScriptRoot/../end-to-end-tests-prelude.ps1

if (-not $IsWindows) {
    Write-Host 'Skipping e2e post build checks on non-Windows'
    return
}

# DLLs with no exports
Refresh-TestRoot
[string]$buildOutput = Run-VcpkgAndCaptureOutput @commonArgs install --overlay-ports="$PSScriptRoot/../e2e-ports" vcpkg-internal-dll-with-no-exports --no-binarycaching
if (-not $buildOutput.Contains("$packagesRoot\vcpkg-internal-dll-with-no-exports_x86-windows\debug\bin\no_exports.dll") `
    -or -not $buildOutput.Contains("$packagesRoot\vcpkg-internal-dll-with-no-exports_x86-windows\bin\no_exports.dll") `
    -or -not $buildOutput.Contains('set(VCPKG_POLICY_DLLS_WITHOUT_EXPORTS enabled)')) {
    throw 'Did not detect DLLs with no exports.'
}

# DLLs with wrong architecture
Refresh-TestRoot
mkdir "$TestingRoot/wrong-architecture"
Copy-Item -Recurse "$PSScriptRoot/../e2e-assets/test-dll-port-template" "$TestingRoot/wrong-architecture/test-dll"
Run-Vcpkg env "$TestingRoot/wrong-architecture/test-dll/build.cmd" --Triplet x64-windows
Throw-IfFailed

$buildOutput = Run-VcpkgAndCaptureOutput @commonArgs install --overlay-ports="$TestingRoot/wrong-architecture" test-dll --no-binarycaching
$expected = "warning: The following files were built for an incorrect architecture:`n" + `
"warning:   $packagesRoot\test-dll_x86-windows\debug\lib\test_dll.lib`n" + `
" Expected: x86, but was x64`n" + `
"warning: The following files were built for an incorrect architecture:`n" + `
"warning:   $packagesRoot\test-dll_x86-windows\lib\test_dll.lib`n" + `
" Expected: x86, but was x64`n" + `
"warning: The following files were built for an incorrect architecture:`n" + `
"warning:   $packagesRoot\test-dll_x86-windows\debug\bin\test_dll.dll`n" + `
" Expected: x86, but was x64`n" + `
"warning:   $packagesRoot\test-dll_x86-windows\bin\test_dll.dll`n" + `
" Expected: x86, but was x64`n"

if (-not $buildOutput.Replace("`r`n", "`n").Contains($expected)) {
    throw 'Did not detect DLL with wrong architecture.'
}

# DLLs with no AppContainer bit
Refresh-TestRoot
mkdir "$TestingRoot/wrong-appcontainer"
Copy-Item -Recurse "$PSScriptRoot/../e2e-assets/test-dll-port-template" "$TestingRoot/wrong-appcontainer/test-dll"
Run-Vcpkg env "$TestingRoot/wrong-appcontainer/test-dll/build.cmd" --Triplet x64-windows
Throw-IfFailed

$buildOutput = Run-VcpkgAndCaptureOutput --triplet x64-uwp "--x-buildtrees-root=$buildtreesRoot" "--x-install-root=$installRoot" "--x-packages-root=$packagesRoot" install --overlay-ports="$TestingRoot/wrong-appcontainer" test-dll --no-binarycaching
$expected = "warning: The App Container bit must be set for Windows Store apps. The following DLLs do not have the App Container bit set:`n" + `
"`n" + `
"  $packagesRoot\test-dll_x64-uwp\debug\bin\test_dll.dll`n" + `
"  $packagesRoot\test-dll_x64-uwp\bin\test_dll.dll`n"

if (-not $buildOutput.Replace("`r`n", "`n").Contains($expected)) {
    throw 'Did not detect DLL with wrong appcontainer.'
}

# Wrong CRT linkage
Refresh-TestRoot
mkdir "$TestingRoot/wrong-crt"
Copy-Item -Recurse "$PSScriptRoot/../e2e-assets/test-lib-port-template-dynamic-crt" "$TestingRoot/wrong-crt/test-lib"
Run-Vcpkg env "$TestingRoot/wrong-crt/test-lib/build.cmd" --Triplet x86-windows-static
Throw-IfFailed

$buildOutput = Run-VcpkgAndCaptureOutput --triplet x86-windows-static "--x-buildtrees-root=$buildtreesRoot" "--x-install-root=$installRoot" "--x-packages-root=$packagesRoot" install --overlay-ports="$TestingRoot/wrong-crt" test-lib --no-binarycaching
$expected = "warning: The following binaries should use the Static Debug (/MTd) CRT.`n" +
"  $packagesRoot\test-lib_x86-windows-static\debug\lib\both_lib.lib links with:`n" +
"    Dynamic Debug (/MDd)`n" +
"    Dynamic Release (/MD)`n" +
"  $packagesRoot\test-lib_x86-windows-static\debug\lib\test_lib.lib links with: Dynamic Debug (/MDd)`n" +
"To inspect the lib files, use:`n" +
"  dumpbin.exe /directives mylibfile.lib`n" +
"warning: The following binaries should use the Static Release (/MT) CRT.`n" +
"  $packagesRoot\test-lib_x86-windows-static\lib\both_lib.lib links with:`n" +
"    Dynamic Debug (/MDd)`n" +
"    Dynamic Release (/MD)`n" +
"  $packagesRoot\test-lib_x86-windows-static\lib\test_lib.lib links with: Dynamic Release (/MD)`n" +
"To inspect the lib files, use:`n" +
"  dumpbin.exe /directives mylibfile.lib`n"
if (-not $buildOutput.Replace("`r`n", "`n").Contains($expected)) {
    throw 'Did not detect lib with wrong CRT linkage.'
}
