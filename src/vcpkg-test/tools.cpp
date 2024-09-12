#include <vcpkg-test/util.h>

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
