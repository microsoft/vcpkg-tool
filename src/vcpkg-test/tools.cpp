#include <catch2/catch.hpp>

#include <vcpkg/tools.h>
#include <vcpkg/versions.h>

using namespace vcpkg;

TEST_CASE ("tool_version_from_values", "[tools]")
{
    auto result = ToolVersion::from_values({});
    CHECK(result.original_text == "");
    CHECK(result.version == std::vector<uint64_t>{});

    result = ToolVersion::from_values({42});
    CHECK(result.original_text == "42");
    CHECK(result.version == std::vector<uint64_t>{42});

    result = ToolVersion::from_values({42, 1729});
    CHECK(result.original_text == "42.1729");
    CHECK(result.version == std::vector<uint64_t>{42, 1729});
}

TEST_CASE ("parse_tool_version_string", "[tools]")
{
    auto result = ToolVersion::try_parse_numeric("1.2.3");
    REQUIRE(result.has_value());
    CHECK(*result.get() == ToolVersion::from_values({1, 2, 3}));

    result = ToolVersion::try_parse_numeric("3.22.3");
    REQUIRE(result.has_value());
    CHECK(*result.get() == ToolVersion::from_values({3, 22, 3}));

    result = ToolVersion::try_parse_numeric("4.65");
    REQUIRE(result.has_value());
    CHECK(*result.get() == ToolVersion::from_values({4, 65, 0}));

    result = ToolVersion::try_parse_numeric(R"(cmake version 3.22.2
CMake suite maintained and supported by Kitware (kitware.com/cmake).)");
    REQUIRE(result.has_value());
    CHECK(*result.get() == ToolVersion::from_values({3, 22, 2}));

    result = ToolVersion::try_parse_numeric(R"(aria2 version 1.35.0
Copyright (C) 2006, 2019 Tatsuhiro Tsujikawa)");
    REQUIRE(result.has_value());
    CHECK(*result.get() == ToolVersion::from_values({1, 35, 0}));

    result = ToolVersion::try_parse_numeric(R"(git version 2.17.1.windows.2)");
    REQUIRE(result.has_value());
    CHECK(*result.get() == ToolVersion::from_values({2, 17, 1}));

    result = ToolVersion::try_parse_numeric(R"(git version 2.17.windows.2)");
    REQUIRE(result.has_value());
    CHECK(*result.get() == ToolVersion::from_values({2, 17, 0}));

    result = ToolVersion::try_parse_numeric("4");
    CHECK_FALSE(result.has_value());
}

TEST_CASE ("parse git version", "[tools]")
{
    {
        auto result = ToolVersion::try_parse_git("git version 2.17.1.windows.2\n").value_or_exit(VCPKG_LINE_INFO);
        CHECK(result.original_text == "2.17.1.windows.2");
        CHECK(result.version == std::vector<uint64_t>{2, 17, 1, 2});
    }

    {
        auto result = ToolVersion::try_parse_git("git version 2.17.....1.windows.2\n").value_or_exit(VCPKG_LINE_INFO);
        CHECK(result.original_text == "2.17.....1.windows.2");
        CHECK(result.version == std::vector<uint64_t>{2, 17, 1, 2});
    }

    {
        auto result = ToolVersion::try_parse_git("git version 2.17.....1.windows.2..\n").value_or_exit(VCPKG_LINE_INFO);
        CHECK(result.original_text == "2.17.....1.windows.2..");
        CHECK(result.version == std::vector<uint64_t>{2, 17, 1, 2});
    }

    {
        auto result = ToolVersion::try_parse_git("git version 2.17.1.2\n").value_or_exit(VCPKG_LINE_INFO);
        CHECK(result.original_text == "2.17.1.2");
        CHECK(result.version == std::vector<uint64_t>{2, 17, 1, 2});
    }

    {
        auto result = ToolVersion::try_parse_git("git version 2.17.1.2").value_or_exit(VCPKG_LINE_INFO);
        CHECK(result.original_text == "2.17.1.2");
        CHECK(result.version == std::vector<uint64_t>{2, 17, 1, 2});
    }

    {
        auto result = ToolVersion::try_parse_git("git version 2.2\n").value_or_exit(VCPKG_LINE_INFO);
        CHECK(result.original_text == "2.2");
        CHECK(result.version == std::vector<uint64_t>{2, 2});
    }

    CHECK(!ToolVersion::try_parse_git("2.17.1.2").has_value());
}
