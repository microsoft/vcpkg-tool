if ($Triplet -ne "x64-linux") {
    return
}

. $PSScriptRoot/../end-to-end-tests-prelude.ps1

$args = $commonArgs + @("--overlay-triplets=$PSScriptRoot/../e2e_ports/compilertracking", "--binarysource=clear;files,$ArchiveRoot,readwrite")

# Test simple installation
Run-Vcpkg -TestArgs ($args + @("install", "tinyxml"))
Throw-IfFailed
if (-Not (select-string "^triplet_abi [0-9a-f]+-[0-9a-f]+-[0-9a-f]+$" "$installRoot/x64-linux/share/tinyxml/vcpkg_abi_info.txt")) {
    throw "Expected tinyxml to perform compiler detection"
}
Remove-Item -Recurse -Force $installRoot

Run-Vcpkg -TestArgs ($args + @("install", "rapidjson"))
Throw-IfFailed
if (-Not (select-string "^triplet_abi [0-9a-f]+-[0-9a-f]+$" "$installRoot/x64-linux/share/rapidjson/vcpkg_abi_info.txt")) {
    throw "Expected rapidjson to not perform compiler detection"
}
Remove-Item -Recurse -Force $installRoot

Run-Vcpkg -TestArgs ($args + @("install", "rapidjson", "tinyxml"))
Throw-IfFailed
if (-Not (select-string "^triplet_abi [0-9a-f]+-[0-9a-f]+-[0-9a-f]+$" "$installRoot/x64-linux/share/tinyxml/vcpkg_abi_info.txt")) {
    throw "Expected tinyxml to perform compiler detection"
}
if (-Not (select-string "^triplet_abi [0-9a-f]+-[0-9a-f]+$" "$installRoot/x64-linux/share/rapidjson/vcpkg_abi_info.txt")) {
    throw "Expected rapidjson to not perform compiler detection"
}
