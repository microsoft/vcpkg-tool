. $PSScriptRoot/../end-to-end-tests-prelude.ps1

if (-not $IsWindows) {
    Write-Host 'Skipping e2e post build checks on non-Windows'
    return
}

# DLLs with no exports
Refresh-TestRoot
[string]$buildOutput = Run-VcpkgAndCaptureOutput @commonArgs install --overlay-ports="$PSScriptRoot/../e2e_ports" vcpkg-internal-dll-with-no-exports --no-binarycaching
if (-not $buildOutput.Contains("$packagesRoot\vcpkg-internal-dll-with-no-exports_x86-windows\debug\bin\no_exports.dll") `
    -or -not $buildOutput.Contains("$packagesRoot\vcpkg-internal-dll-with-no-exports_x86-windows\bin\no_exports.dll") `
    -or -not $buildOutput.Contains('set(VCPKG_POLICY_DLLS_WITHOUT_EXPORTS enabled)')) {
    throw 'Did not detect DLLs with no exports.'
}

# DLLs with wrong architecture
Refresh-TestRoot
if (Test-Path "$WorkingRoot/wrong-architecture") {
    Remove-Item "$WorkingRoot/wrong-architecture" -Recurse -Force
}

mkdir "$WorkingRoot/wrong-architecture"
Copy-Item -Recurse "$PSScriptRoot/../e2e_assets/test-dll-port-template" "$WorkingRoot/wrong-architecture/test-dll"
Run-Vcpkg env "$WorkingRoot/wrong-architecture/test-dll/build.cmd" --Triplet x64-windows
Throw-IfFailed

$buildOutput = Run-VcpkgAndCaptureOutput @commonArgs install --overlay-ports="$WorkingRoot/wrong-architecture" test-dll --no-binarycaching
$expected = "warning: The following files were built for an incorrect architecture:`n" + `
"warning:     $packagesRoot\test-dll_x86-windows\debug\lib\test_dll.lib`n" + `
" Expected: x86, but was x64`n" + `
"warning:     $packagesRoot\test-dll_x86-windows\lib\test_dll.lib`n" + `
" Expected: x86, but was x64`n" + `
"warning: The following files were built for an incorrect architecture:`n" + `
"warning:     $packagesRoot\test-dll_x86-windows\debug\bin\test_dll.dll`n" + `
" Expected: x86, but was x64`n" + `
"warning:     $packagesRoot\test-dll_x86-windows\bin\test_dll.dll`n" + `
" Expected: x86, but was x64`n"

if (-not $buildOutput.Replace("`r`n", "`n").Contains($expected)) {
    throw 'Did not detect DLL with wrong architecture.'
}

# DLLs with no AppContainer bit
Refresh-TestRoot
if (Test-Path "$WorkingRoot/wrong-appcontainer") {
    Remove-Item "$WorkingRoot/wrong-appcontainer" -Recurse -Force
}

mkdir "$WorkingRoot/wrong-appcontainer"
Copy-Item -Recurse "$PSScriptRoot/../e2e_assets/test-dll-port-template" "$WorkingRoot/wrong-appcontainer/test-dll"
Run-Vcpkg env "$WorkingRoot/wrong-appcontainer/test-dll/build.cmd" --Triplet x64-windows
Throw-IfFailed

$buildOutput = Run-VcpkgAndCaptureOutput --triplet x64-uwp "--x-buildtrees-root=$buildtreesRoot" "--x-install-root=$installRoot" "--x-packages-root=$packagesRoot" install --overlay-ports="$WorkingRoot/wrong-appcontainer" test-dll --no-binarycaching
$expected = "warning: The App Container bit must be set for Windows Store apps. The following DLLs do not have the App Container bit set:`n" + `
"`n" + `
"    $packagesRoot\test-dll_x64-uwp\debug\bin\test_dll.dll`n" + `
"    $packagesRoot\test-dll_x64-uwp\bin\test_dll.dll`n"

if (-not $buildOutput.Replace("`r`n", "`n").Contains($expected)) {
    throw 'Did not detect DLL with wrong appcontainer.'
}

# Wrong CRT linkage
Refresh-TestRoot
if (Test-Path "$WorkingRoot/wrong-crt") {
    Remove-Item "$WorkingRoot/wrong-crt" -Recurse -Force
}

mkdir "$WorkingRoot/wrong-crt"
Copy-Item -Recurse "$PSScriptRoot/../e2e_assets/test-lib-port-template-dynamic-crt" "$WorkingRoot/wrong-crt/test-lib"
Run-Vcpkg env "$WorkingRoot/wrong-crt/test-lib/build.cmd" --Triplet x86-windows-static
Throw-IfFailed

$buildOutput = Run-VcpkgAndCaptureOutput --triplet x86-windows-static "--x-buildtrees-root=$buildtreesRoot" "--x-install-root=$installRoot" "--x-packages-root=$packagesRoot" install --overlay-ports="$WorkingRoot/wrong-crt" test-lib --no-binarycaching --debug
$expected = "warning: Invalid crt linkage. Expected Debug,Static, but the following libs had:`n" + `
"    $packagesRoot\test-lib_x86-windows-static\debug\lib\test_lib.lib: Debug,Dynamic`n" + `
"warning: To inspect the lib files, use:`n" + `
"    dumpbin.exe /directives mylibfile.lib`n" + `
"warning: Invalid crt linkage. Expected Release,Static, but the following libs had:`n" + `
"    $packagesRoot\test-lib_x86-windows-static\lib\test_lib.lib: Release,Dynamic`n" + `
"warning: To inspect the lib files, use:`n" + `
"    dumpbin.exe /directives mylibfile.lib`n"
if (-not $buildOutput.Replace("`r`n", "`n").Contains($expected)) {
    throw 'Did not detect lib with wrong CRT linkage.'
}
