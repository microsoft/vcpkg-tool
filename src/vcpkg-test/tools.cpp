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
    {
        Optional<std::string> maybe_output = "fooutil version 1.2";
        FullyBufferedDiagnosticContext fbdc;
        extract_prefixed_nonwhitespace(fbdc, "fooutil version ", "fooutil", maybe_output, "fooutil.exe");
        CHECK(fbdc.empty());
        CHECK(maybe_output.value_or_exit(VCPKG_LINE_INFO) == "1.2");
    }

    {
        Optional<std::string> maybe_output = "fooutil version 1.2   ";
        FullyBufferedDiagnosticContext fbdc;
        extract_prefixed_nonwhitespace(fbdc, "fooutil version ", "fooutil", maybe_output, "fooutil.exe");
        CHECK(fbdc.empty());
        CHECK(maybe_output.value_or_exit(VCPKG_LINE_INFO) == "1.2");
    }

    {
        Optional<std::string> maybe_output = "malformed output";
        FullyBufferedDiagnosticContext fbdc;
        extract_prefixed_nonwhitespace(fbdc, "fooutil version ", "fooutil", maybe_output, "fooutil.exe");
        CHECK(!maybe_output);
        CHECK(fbdc.to_string() == "fooutil.exe: error: fooutil produced unexpected output when attempting to determine "
                                  "the version:\nmalformed output");
    }
}

TEST_CASE ("extract_prefixed_nonquote", "[tools]")
{
    {
        Optional<std::string> maybe_output = "fooutil version 1.2\"";
        FullyBufferedDiagnosticContext fbdc;
        extract_prefixed_nonquote(fbdc, "fooutil version ", "fooutil", maybe_output, "fooutil.exe");
        CHECK(fbdc.empty());
        CHECK(maybe_output.value_or_exit(VCPKG_LINE_INFO) == "1.2");
    }

    {
        Optional<std::string> maybe_output = "fooutil version 1.2 \"";
        FullyBufferedDiagnosticContext fbdc;
        extract_prefixed_nonquote(fbdc, "fooutil version ", "fooutil", maybe_output, "fooutil.exe");
        CHECK(fbdc.empty());
        CHECK(maybe_output.value_or_exit(VCPKG_LINE_INFO) == "1.2 ");
    }

    {
        Optional<std::string> maybe_output = "malformed output";
        FullyBufferedDiagnosticContext fbdc;
        extract_prefixed_nonquote(fbdc, "fooutil version ", "fooutil", maybe_output, "fooutil.exe");
        CHECK(!maybe_output);
        CHECK(fbdc.to_string() == "fooutil.exe: error: fooutil produced unexpected output when attempting to determine "
                                  "the version:\nmalformed output");
    }
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

    FullyBufferedDiagnosticContext fbdc;
    auto maybe_data = parse_tool_data(fbdc, tool_doc, "vcpkgTools.json");
    REQUIRE(fbdc.empty());
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
    {
        FullyBufferedDiagnosticContext fbdc;
        auto empty = parse_tool_data(fbdc, "", "empty.json");
        REQUIRE(!empty.has_value());
        CHECK(fbdc.to_string() == R"(empty.json:1:1: error: Unexpected EOF; expected value
  on expression: 
                 ^)");
    }

    {
        FullyBufferedDiagnosticContext fbdc;
        auto top_level_json = parse_tool_data(fbdc, "[]", "top_level.json");
        REQUIRE(!top_level_json.has_value());
        CHECK(fbdc.to_string() == "top_level.json: error: expected an object");
    }

    {
        FullyBufferedDiagnosticContext fbdc;
        auto missing_required = parse_tool_data(
            fbdc, R"({ "schema-version": 1, "tools": [{ "executable": "git.exe" }]})", "missing_required.json");
        REQUIRE(!missing_required.has_value());
        CHECK(fbdc.to_string() ==
              "missing_required.json: error: $.tools[0] (tool metadata): missing required field 'name' (a string)\n"
              "missing_required.json: error: $.tools[0] (tool metadata): missing required field 'os' (a tool data "
              "operating system)\n"
              "missing_required.json: error: $.tools[0] (tool metadata): missing required field 'version' (a tool data "
              "version)");
    }

    {
        FullyBufferedDiagnosticContext fbdc;
        auto uexpected_field = parse_tool_data(fbdc,
                                               R"(
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
        CHECK(fbdc.to_string() ==
              "uexpected_field.json: error: $.tools[0] (tool metadata): unexpected field 'arc', did you mean 'arch'?");
    }

    {
        FullyBufferedDiagnosticContext fbdc;
        auto invalid_os = parse_tool_data(fbdc,
                                          R"(
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
        CHECK(fbdc.to_string() ==
              "invalid_os.json: error: $.tools[0].os (a tool data operating system): Invalid tool operating system: "
              "notanos. "
              "Expected one of: windows, osx, linux, freebsd, openbsd, netbsd, solaris");
    }

    {
        FullyBufferedDiagnosticContext fbdc;
        auto invalid_version = parse_tool_data(fbdc,
                                               R"(
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
        CHECK(fbdc.to_string() ==
              "invalid_version.json: error: $.tools[0].version (a tool data version): Invalid tool version; expected a "
              "string containing a substring of between 1 and 3 numbers separated by dots.");
    }

    {
        FullyBufferedDiagnosticContext fbdc;
        auto invalid_arch = parse_tool_data(fbdc,
                                            R"(
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
        CHECK(
            fbdc.to_string() ==
            "invalid_arch.json: error: $.tools[0].arch (a CPU architecture): Invalid architecture: notanarchitecture. "
            "Expected one of: x86, x64, amd64, arm, arm64, arm64ec, s390x, ppc64le, riscv32, riscv64, loongarch32, "
            "loongarch64, mips64");
    }

    {
        FullyBufferedDiagnosticContext fbdc;
        auto invalid_sha512 = parse_tool_data(fbdc,
                                              R"(
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
        CHECK(fbdc.to_string() ==
              "invalid_sha512.json: error: $.tools[0].sha512 (a SHA-512 hash): invalid SHA-512 hash: notasha512\n"
              "SHA-512 hash must be 128 characters long and contain only hexadecimal digits");
    }
}

TEST_CASE ("ContextCache", "[tools]")
{
    struct JustOpt
    {
        int value;
        Optional<int> operator()(DiagnosticContext& context) const
        {
            context.statusln(LocalizedString::from_raw(fmt::format("The value is {}", value)));
            return value;
        }
    };

    ContextCache<std::string, int> cache;

    // success miss then hit (middle key)
    FullyBufferedDiagnosticContext fbdc1;
    const auto* first_ptr = cache.get_lazy(fbdc1, "durian", JustOpt{42});
    REQUIRE(first_ptr != nullptr);
    CHECK(*first_ptr == 42);
    CHECK(fbdc1.to_string() == "The value is 42");
    // functor should NOT run, status lines not replayed:
    FullyBufferedDiagnosticContext fbdc1_hit;
    const auto* hit_ptr = cache.get_lazy(fbdc1_hit, "durian", JustOpt{12345});
    REQUIRE(hit_ptr == first_ptr);
    CHECK(fbdc1_hit.empty());

    // insert before existing element
    FullyBufferedDiagnosticContext fbdc2;
    const auto* below_ptr = cache.get_lazy(fbdc2, "apple", JustOpt{1729});
    REQUIRE(below_ptr != nullptr);
    CHECK(*below_ptr == 1729);
    CHECK(fbdc2.to_string() == "The value is 1729");
    FullyBufferedDiagnosticContext fbdc2_hit;
    const auto* below_hit_ptr = cache.get_lazy(fbdc2_hit, "apple", JustOpt{1});
    REQUIRE(below_hit_ptr == below_ptr);
    CHECK(fbdc2_hit.empty());

    // insert above existing element
    FullyBufferedDiagnosticContext fbdc3;
    const auto* above_ptr = cache.get_lazy(fbdc3, "melon", JustOpt{1234});
    REQUIRE(above_ptr != nullptr);
    CHECK(*above_ptr == 1234);
    CHECK(fbdc3.to_string() == "The value is 1234");
    FullyBufferedDiagnosticContext fbdc3_hit;
    const auto* above_hit_ptr = cache.get_lazy(fbdc3_hit, "melon", JustOpt{2});
    REQUIRE(above_hit_ptr == above_ptr);
    CHECK(fbdc3_hit.empty());

    // error case: provider returns empty optional and reports diagnostic; ensure cached error re-reports but provider
    // not re-run
    int error_call_count = 0;
    auto failing_provider = [&error_call_count](DiagnosticContext& ctx) -> Optional<int> {
        ++error_call_count;
        ctx.statusln(LocalizedString::from_raw(fmt::format("The number of calls is {}", error_call_count)));
        ctx.report_error(LocalizedString::from_raw("bad"));
        return nullopt; // failure
    };
    FullyBufferedDiagnosticContext fbdc_err1;
    const auto* err1 = cache.get_lazy(fbdc_err1, "kiwi", failing_provider);
    REQUIRE(err1 == nullptr);
    CHECK(!fbdc_err1.empty());
    CHECK(fbdc_err1.to_string() == "The number of calls is 1\nerror: bad");

    FullyBufferedDiagnosticContext fbdc_err2;
    const auto* err2 = cache.get_lazy(fbdc_err2, "kiwi", failing_provider); // should not invoke provider again
    REQUIRE(err2 == nullptr);
    CHECK(!fbdc_err2.empty());
    CHECK(fbdc_err2.to_string() == "error: bad"); // status line not replayed
    const auto second_error_output = fbdc_err2.to_string();
    CHECK(error_call_count == 1);
}
