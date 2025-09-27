#include <vcpkg-test/util.h>

#include <vcpkg/base/files.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.export.h>

#include <string>
#include <vector>

using namespace vcpkg;

TEST_CASE ("convert_list_to_proximate_files basic example", "[export]")
{
    std::vector<std::string> lines = {
        "x64-windows/",
        "x64-windows/include/", // directory (trailing slash removed)
        "x64-windows/include/CONFLICT-A-HEADER-ONLY-CAPS.h",
        "x64-windows/include/CONFLICT-a-header-ONLY-mixed.h",
        "x64-windows/include/CONFLICT-a-header-ONLY-mixed2.h",
        "x64-windows/include/conflict-a-header-only-lowercase.h",
        "x64-windows/share/",            // directory (trailing slash removed)
        "x64-windows/share/a-conflict/", // directory (trailing slash removed)
        "x64-windows/share/a-conflict/copyright",
        "x64-windows/share/a-conflict/vcpkg.spdx.json",
        "x64-windows/share/a-conflict/vcpkg_abi_info.txt",
    };

    const auto result = convert_list_to_proximate_files(std::move(lines), "x64-windows");
    const std::vector<std::string> expected = {
        "include",
        "include/CONFLICT-A-HEADER-ONLY-CAPS.h",
        "include/CONFLICT-a-header-ONLY-mixed.h",
        "include/CONFLICT-a-header-ONLY-mixed2.h",
        "include/conflict-a-header-only-lowercase.h",
        "share",
        "share/a-conflict",
        "share/a-conflict/copyright",
        "share/a-conflict/vcpkg.spdx.json",
        "share/a-conflict/vcpkg_abi_info.txt",
    };
    REQUIRE(result == expected);
}

TEST_CASE ("convert_list_to_proximate_files preserves order and trims trailing slashes", "[export]")
{
    std::vector<std::string> lines = {
        "x64-windows/share/",           // -> share
        "x64-windows/share/pkg/",       // -> share/pkg
        "x64-windows/share/pkg/file",   // -> share/pkg/file (unchanged)
        "x64-windows/include/",         // -> include
        "x64-windows/include/file.h",   // -> include/file.h
        "x64-windows/share/other/file2" // -> share/other/file2
    };

    auto result = convert_list_to_proximate_files(std::move(lines), "x64-windows");
    const std::vector<std::string> expected = {
        "share",
        "share/pkg",
        "share/pkg/file",
        "include",
        "include/file.h",
        "share/other/file2",
    };
    REQUIRE(result == expected);
}

TEST_CASE ("convert_list_to_proximate_files empty input", "[export]")
{
    auto result = convert_list_to_proximate_files(std::vector<std::string>{}, "x64-windows");
    REQUIRE(result.empty());
}
