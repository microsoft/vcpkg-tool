#include <vcpkg-test/util.h>

#include <vcpkg/tools.h>
#include <vcpkg/tools.test.h>

#include <array>
#include "vcpkg/base/system.h"

using namespace vcpkg;

TEST_CASE ("parse_tool_version_string", "[tools]")
{
    auto result = parse_tool_version_string("1.2.3");
    REQUIRE(result.has_value());
    CHECK(*result.get() == std::array<int, 3>{1, 2, 3});

    result = parse_tool_version_string("3.22.3");
    REQUIRE(result.has_value());
    CHECK(*result.get() == std::array<int, 3>{3, 22, 3});

    result = parse_tool_version_string("4.65");
    REQUIRE(result.has_value());
    CHECK(*result.get() == std::array<int, 3>{4, 65, 0});

    result = parse_tool_version_string(R"(cmake version 3.22.2
CMake suite maintained and supported by Kitware (kitware.com/cmake).)");
    REQUIRE(result.has_value());
    CHECK(*result.get() == std::array<int, 3>{3, 22, 2});

    result = parse_tool_version_string(R"(aria2 version 1.35.0
Copyright (C) 2006, 2019 Tatsuhiro Tsujikawa)");
    REQUIRE(result.has_value());
    CHECK(*result.get() == std::array<int, 3>{1, 35, 0});

    result = parse_tool_version_string(R"(git version 2.17.1.windows.2)");
    REQUIRE(result.has_value());
    CHECK(*result.get() == std::array<int, 3>{2, 17, 1});

    result = parse_tool_version_string(R"(git version 2.17.windows.2)");
    REQUIRE(result.has_value());
    CHECK(*result.get() == std::array<int, 3>{2, 17, 0});

    result = parse_tool_version_string("4");
    CHECK_FALSE(result.has_value());
}

TEST_CASE ("parse_tool_data_from_xml", "[tools]")
{
    const StringView tool_doc = R"(
<?xml version="1.0"?>
<tools version="2">
    <tool name="git" os="linux">
        <version>2.7.4</version>
        <exeRelativePath></exeRelativePath>
        <url></url>
        <sha512></sha512>
    </tool>
    <tool name="nuget" os="osx">
        <version>5.11.0</version>
        <exeRelativePath>nuget.exe</exeRelativePath>
        <url>https://dist.nuget.org/win-x86-commandline/v5.11.0/nuget.exe</url>
        <sha512>06a337c9404dec392709834ef2cdbdce611e104b510ef40201849595d46d242151749aef65bc2d7ce5ade9ebfda83b64c03ce14c8f35ca9957a17a8c02b8c4b7</sha512>
    </tool>
    <tool name="node" os="windows">
        <version>16.12.0</version>
        <exeRelativePath>node-v16.12.0-win-x64\node.exe</exeRelativePath>
        <url>https://nodejs.org/dist/v16.12.0/node-v16.12.0-win-x64.7z</url>
        <sha512>0bb793fce8140bd59c17f3ac9661b062eac0f611d704117774f5cb2453d717da94b1e8b17d021d47baff598dc023fb7068ed1f8a7678e446260c3db3537fa888</sha512>
        <archiveName>node-v16.12.0-win-x64.7z</archiveName>
    </tool>
</tools>
)";

    {
        auto data = parse_tool_data_from_xml(tool_doc, "vcpkgTools.xml", "tool1", "windows", to_zstring_view(get_host_processor()));
        REQUIRE(!data.has_value());
    }
    {
        auto data = parse_tool_data_from_xml(tool_doc, "vcpkgTools.xml", "node", "unknown", to_zstring_view(get_host_processor()));
        REQUIRE(!data.has_value());
    }
    {
        auto data = parse_tool_data_from_xml(tool_doc, "vcpkgTools.xml", "node", "windows", to_zstring_view(get_host_processor()));
        REQUIRE(data.has_value());
        auto& p = *data.get();
        CHECK(p.is_archive);
        CHECK(p.version == decltype(p.version){16, 12, 0});
        CHECK(p.tool_dir_subpath == "node-16.12.0-windows");
        CHECK(p.exe_subpath == "node-v16.12.0-win-x64\\node.exe");
        CHECK(p.download_subpath == "node-v16.12.0-win-x64.7z");
        CHECK(p.sha512 == "0bb793fce8140bd59c17f3ac9661b062eac0f611d704117774f5cb2453d717da94b1e8b17d021d47baff598dc023"
                          "fb7068ed1f8a7678e446260c3db3537fa888");
        CHECK(p.url == "https://nodejs.org/dist/v16.12.0/node-v16.12.0-win-x64.7z");
    }
    {
        auto data = parse_tool_data_from_xml(tool_doc, "vcpkgTools.xml", "nuget", "osx", to_zstring_view(get_host_processor()));
        REQUIRE(data.has_value());
        auto& p = *data.get();
        CHECK_FALSE(p.is_archive);
        CHECK(p.version == decltype(p.version){5, 11, 0});
        CHECK(p.tool_dir_subpath == "nuget-5.11.0-osx");
        CHECK(p.exe_subpath == "nuget.exe");
        CHECK(p.download_subpath == "06a337c9-nuget.exe");
        CHECK(p.sha512 == "06a337c9404dec392709834ef2cdbdce611e104b510ef40201849595d46d242151749aef65bc2d7ce5ade9ebfda8"
                          "3b64c03ce14c8f35ca9957a17a8c02b8c4b7");
        CHECK(p.url == "https://dist.nuget.org/win-x86-commandline/v5.11.0/nuget.exe");
    }
    {
        auto data = parse_tool_data_from_xml(tool_doc, "vcpkgTools.xml", "git", "linux", to_zstring_view(get_host_processor()));
        REQUIRE(data.has_value());
        auto& p = *data.get();
        CHECK_FALSE(p.is_archive);
        CHECK(p.version == decltype(p.version){2, 7, 4});
        CHECK(p.tool_dir_subpath == "git-2.7.4-linux");
        CHECK(p.exe_subpath == "");
        CHECK(p.download_subpath == "");
        CHECK(p.sha512 == "");
        CHECK(p.url == "");
    }
}

TEST_CASE ("extract_prefixed_nonwhitespace", "[tools]")
{
    CHECK(extract_prefixed_nonwhitespace("fooutil version ", "fooutil", "fooutil version 1.2", "fooutil.exe")
              .value_or_exit(VCPKG_LINE_INFO) == "1.2");
    CHECK(extract_prefixed_nonwhitespace("fooutil version ", "fooutil", "fooutil version 1.2   ", "fooutil.exe")
              .value_or_exit(VCPKG_LINE_INFO) == "1.2");
    auto error_result =
        extract_prefixed_nonwhitespace("fooutil version ", "fooutil", "malformed output", "fooutil.exe");
    CHECK(!error_result.has_value());
    CHECK(error_result.error() == "error: fooutil (fooutil.exe) produced unexpected output when attempting to "
                                  "determine the version:\nmalformed output");
}
