#include <vcpkg-test/util.h>

#include <vcpkg/base/jsonreader.h>

#include <vcpkg/tools.h>
#include <vcpkg/tools.test.h>

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

    result = parse_tool_version_string(R"(example version 1.35.0
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

TEST_CASE ("extract_prefixed_nonquote", "[tools]")
{
    CHECK(extract_prefixed_nonquote("fooutil version ", "fooutil", "fooutil version 1.2\"", "fooutil.exe")
              .value_or_exit(VCPKG_LINE_INFO) == "1.2");
    CHECK(extract_prefixed_nonquote("fooutil version ", "fooutil", "fooutil version 1.2 \"  ", "fooutil.exe")
              .value_or_exit(VCPKG_LINE_INFO) == "1.2 ");
    auto error_result = extract_prefixed_nonquote("fooutil version ", "fooutil", "malformed output", "fooutil.exe");
    CHECK(!error_result.has_value());
    CHECK(error_result.error() == "error: fooutil (fooutil.exe) produced unexpected output when attempting to "
                                  "determine the version:\nmalformed output");
}

TEST_CASE ("parse_tool_data", "[tools]")
{
    const StringView tool_doc = R"(
{
    "$comment": "This is a comment",
    "schema-version": 1,
    "tools": [
        {
            "$comment": "This is a comment",
            "name": "git",
            "os": "linux",
            "version": "2.7.4",
            "executable": "git"
        },
        {
            "name": "git",
            "os": "linux",
            "arch": "arm64",
            "version": "2.7.4",
            "executable": "git-arm64"
        },
        {
            "name": "nuget",
            "os": "osx",
            "version": "5.11.0",
            "executable": "nuget.exe",
            "url": "https://dist.nuget.org/win-x86-commandline/v5.11.0/nuget.exe",
            "sha512": "06a337c9404dec392709834ef2cdbdce611e104b510ef40201849595d46d242151749aef65bc2d7ce5ade9ebfda83b64c03ce14c8f35ca9957a17a8c02b8c4b7"
        },
        {
            "name": "node",
            "os": "windows",
            "version": "node version 16.12.0.windows.2",
            "executable": "node-v16.12.0-win-x64\\node.exe",
            "url": "https://nodejs.org/dist/v16.12.0/node-v16.12.0-win-x64.7z",
            "sha512": "0bb793fce8140bd59c17f3ac9661b062eac0f611d704117774f5cb2453d717da94b1e8b17d021d47baff598dc023fb7068ed1f8a7678e446260c3db3537fa888",
            "archive": "node-v16.12.0-win-x64.7z"
        }
    ]
})";

    auto maybe_data = parse_tool_data(tool_doc, "vcpkgTools.json");
    REQUIRE(maybe_data.has_value());

    auto data = maybe_data.value_or_exit(VCPKG_LINE_INFO);
    REQUIRE(data.size() == 4);

    auto& git_linux = data[0];
    CHECK(git_linux.tool == "git");
    CHECK(git_linux.os == ToolOs::Linux);
    CHECK_FALSE(git_linux.arch.has_value());
    CHECK(git_linux.version.cooked == std::array<int, 3>{2, 7, 4});
    CHECK(git_linux.version.raw == "2.7.4");
    CHECK(git_linux.exeRelativePath == "git");
    CHECK(git_linux.url == "");
    CHECK(git_linux.sha512 == "");

    auto& git_arm64 = data[1];
    CHECK(git_arm64.tool == "git");
    CHECK(git_arm64.os == ToolOs::Linux);
    CHECK(git_arm64.arch.has_value());
    CHECK(*git_arm64.arch.get() == CPUArchitecture::ARM64);
    CHECK(git_linux.version.cooked == std::array<int, 3>{2, 7, 4});
    CHECK(git_linux.version.raw == "2.7.4");
    CHECK(git_arm64.exeRelativePath == "git-arm64");
    CHECK(git_arm64.url == "");
    CHECK(git_arm64.sha512 == "");

    auto& nuget_osx = data[2];
    CHECK(nuget_osx.tool == "nuget");
    CHECK(nuget_osx.os == ToolOs::Osx);
    CHECK_FALSE(nuget_osx.arch.has_value());
    CHECK(nuget_osx.version.cooked == std::array<int, 3>{5, 11, 0});
    CHECK(nuget_osx.version.raw == "5.11.0");
    CHECK(nuget_osx.exeRelativePath == "nuget.exe");
    CHECK(nuget_osx.url == "https://dist.nuget.org/win-x86-commandline/v5.11.0/nuget.exe");
    CHECK(nuget_osx.sha512 == "06a337c9404dec392709834ef2cdbdce611e104b510ef40201849595d46d242151749aef65bc2d7ce5ade9eb"
                              "fda83b64c03ce14c8f35ca9957a17a8c02b8c4b7");

    auto& node_windows = data[3];
    CHECK(node_windows.tool == "node");
    CHECK(node_windows.os == ToolOs::Windows);
    CHECK_FALSE(node_windows.arch.has_value());
    CHECK(node_windows.version.cooked == std::array<int, 3>{16, 12, 0});
    CHECK(node_windows.version.raw == "node version 16.12.0.windows.2");
    CHECK(node_windows.exeRelativePath == "node-v16.12.0-win-x64\\node.exe");
    CHECK(node_windows.url == "https://nodejs.org/dist/v16.12.0/node-v16.12.0-win-x64.7z");
    CHECK(node_windows.sha512 ==
          "0bb793fce8140bd59c17f3ac9661b062eac0f611d704117774f5cb2453d717da94b1e8b17d021d47baff598dc023"
          "fb7068ed1f8a7678e446260c3db3537fa888");
    CHECK(node_windows.archiveName == "node-v16.12.0-win-x64.7z");

    auto* tool_git_linux = get_raw_tool_data(data, "git", CPUArchitecture::X64, ToolOs::Linux);
    REQUIRE(tool_git_linux != nullptr);
    CHECK(tool_git_linux->tool == "git");
    CHECK(tool_git_linux->os == ToolOs::Linux);
    CHECK_FALSE(tool_git_linux->arch.has_value());
    CHECK(tool_git_linux->version.cooked == std::array<int, 3>{2, 7, 4});
    CHECK(tool_git_linux->version.raw == "2.7.4");
    CHECK(tool_git_linux->exeRelativePath == "git");
    CHECK(tool_git_linux->url == "");
    CHECK(tool_git_linux->sha512 == "");

    auto* tool_git_arm64 = get_raw_tool_data(data, "git", CPUArchitecture::ARM64, ToolOs::Linux);
    REQUIRE(tool_git_arm64 != nullptr);
    CHECK(tool_git_arm64->tool == "git");
    CHECK(tool_git_arm64->os == ToolOs::Linux);
    CHECK(tool_git_arm64->arch.has_value());
    CHECK(*tool_git_arm64->arch.get() == CPUArchitecture::ARM64);
    CHECK(tool_git_arm64->version.cooked == std::array<int, 3>{2, 7, 4});
    CHECK(tool_git_arm64->version.raw == "2.7.4");
    CHECK(tool_git_arm64->exeRelativePath == "git-arm64");
    CHECK(tool_git_arm64->url == "");
    CHECK(tool_git_arm64->sha512 == "");

    auto* tool_nuget_osx = get_raw_tool_data(data, "nuget", CPUArchitecture::X64, ToolOs::Osx);
    REQUIRE(tool_nuget_osx != nullptr);
    CHECK(tool_nuget_osx->tool == "nuget");
    CHECK(tool_nuget_osx->os == ToolOs::Osx);
    CHECK_FALSE(tool_nuget_osx->arch.has_value());
    CHECK(tool_nuget_osx->version.cooked == std::array<int, 3>{5, 11, 0});
    CHECK(tool_nuget_osx->version.raw == "5.11.0");
    CHECK(tool_nuget_osx->exeRelativePath == "nuget.exe");
    CHECK(tool_nuget_osx->url == "https://dist.nuget.org/win-x86-commandline/v5.11.0/nuget.exe");

    auto* tool_node_windows = get_raw_tool_data(data, "node", CPUArchitecture::X64, ToolOs::Windows);
    REQUIRE(tool_node_windows != nullptr);
    CHECK(tool_node_windows->tool == "node");
    CHECK(tool_node_windows->os == ToolOs::Windows);
    CHECK_FALSE(tool_node_windows->arch.has_value());
    CHECK(tool_node_windows->version.cooked == std::array<int, 3>{16, 12, 0});
    CHECK(tool_node_windows->version.raw == "node version 16.12.0.windows.2");
    CHECK(tool_node_windows->exeRelativePath == "node-v16.12.0-win-x64\\node.exe");
    CHECK(tool_node_windows->url == "https://nodejs.org/dist/v16.12.0/node-v16.12.0-win-x64.7z");
    CHECK(tool_node_windows->sha512 ==
          "0bb793fce8140bd59c17f3ac9661b062eac0f611d704117774f5cb2453d717da94b1e8b17d021d47baff598dc023"
          "fb7068ed1f8a7678e446260c3db3537fa888");
    CHECK(tool_node_windows->archiveName == "node-v16.12.0-win-x64.7z");
}

TEST_CASE ("parse_tool_data errors", "[tools]")
{
    auto empty = parse_tool_data("", "empty.json");
    REQUIRE(!empty.has_value());
    CHECK(Strings::starts_with(empty.error(), "empty.json:1:1: error: Unexpected EOF"));

    auto top_level_json = parse_tool_data("[]", "top_level.json");
    REQUIRE(!top_level_json.has_value());
    CHECK("Expected \"top_level.json\" to be an object." == top_level_json.error());

    auto missing_required =
        parse_tool_data(R"({ "schema-version": 1, "tools": [{ "executable": "git.exe" }]})", "missing_required.json");
    REQUIRE(!missing_required.has_value());
    CHECK("missing_required.json: error: $.tools[0] (tool metadata): missing required field 'name' (a string)\n"
          "missing_required.json: error: $.tools[0] (tool metadata): missing required field 'os' (a tool data "
          "operating system)\n"
          "missing_required.json: error: $.tools[0] (tool metadata): missing required field 'version' (a tool data "
          "version)" == missing_required.error());

    auto uexpected_field = parse_tool_data(R"(
{
    "schema-version": 1,
    "tools": [{
        "name": "git",
        "os": "linux",
        "version": "2.7.4",
        "arc": "x64"
    }]
})",
                                           "uexpected_field.json");
    REQUIRE(!uexpected_field.has_value());
    CHECK("uexpected_field.json: error: $.tools[0] (tool metadata): unexpected field 'arc', did you mean 'arch'?" ==
          uexpected_field.error());

    auto invalid_os = parse_tool_data(R"(
{
    "schema-version": 1,
    "tools": [{ 
        "name": "git",
        "os": "notanos",
        "version": "2.7.4"
    }]
})",
                                      "invalid_os.json");
    REQUIRE(!invalid_os.has_value());
    CHECK(
        "invalid_os.json: error: $.tools[0].os (a tool data operating system): Invalid tool operating system: notanos. "
        "Expected one of: windows, osx, linux, freebsd, openbsd, solaris" == invalid_os.error());

    auto invalid_version = parse_tool_data(R"(
{
    "schema-version": 1,
    "tools": [{ 
        "name": "git",
        "os": "windows",
        "version": "abc"
    }]
})",
                                           "invalid_version.json");
    REQUIRE(!invalid_version.has_value());
    CHECK("invalid_version.json: error: $.tools[0].version (a tool data version): Invalid tool version; expected a "
          "string containing a substring of between 1 and 3 numbers separated by dots." == invalid_version.error());

    auto invalid_arch = parse_tool_data(R"(
{
    "schema-version": 1,
    "tools": [{ 
        "name": "git",
        "os": "linux",
        "version": "2.7.4",
        "arch": "notanarchitecture"
    }]
})",
                                        "invalid_arch.json");
    REQUIRE(!invalid_arch.has_value());
    CHECK("invalid_arch.json: error: $.tools[0].arch (a CPU architecture): Invalid architecture: notanarchitecture. "
          "Expected one of: x86, x64, amd64, arm, arm64, arm64ec, s390x, ppc64le, riscv32, riscv64, loongarch32, "
          "loongarch64, mips64" == invalid_arch.error());

    auto invalid_sha512 = parse_tool_data(R"(
{
    "schema-version": 1,
    "tools": [{ 
        "name": "git",
        "os": "linux",
        "version": "2.7.4",
        "executable": "git",
        "sha512": "notasha512"
    }]
})",
                                          "invalid_sha512.json");

    REQUIRE(!invalid_sha512.has_value());
    CHECK("invalid_sha512.json: error: $.tools[0].sha512 (a SHA-512 hash): invalid SHA-512 hash: notasha512\n"
          "SHA-512 hash must be 128 characters long and contain only hexadecimal digits" == invalid_sha512.error());
}
