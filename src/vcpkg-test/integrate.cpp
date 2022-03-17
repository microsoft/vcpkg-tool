#include <catch2/catch.hpp>

#include <vcpkg/install.h>

using namespace vcpkg;
namespace Integrate = vcpkg::Commands::Integrate;

TEST_CASE ("find_targets_file_version", "[integrate]")
{
    constexpr static StringLiteral DEFAULT_TARGETS_FILE = R"xml(
<Project ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
    <!-- version 1 -->
    <PropertyGroup>
        <VCLibPackagePath Condition="'$(VCLibPackagePath)' == ''">$(LOCALAPPDATA)\vcpkg\vcpkg.user</VCLibPackagePath>
    </PropertyGroup>
    <Import Condition="'$(VCLibPackagePath)' != '' and Exists('$(VCLibPackagePath).props')" Project="$(VCLibPackagePath).props" />
    <Import Condition="'$(VCLibPackagePath)' != '' and Exists('$(VCLibPackagePath).targets')" Project="$(VCLibPackagePath).targets" />
</Project>
)xml";

    auto res = Integrate::find_targets_file_version(DEFAULT_TARGETS_FILE);
    REQUIRE(res.has_value());
    CHECK(*res.get() == 1);

    res = Integrate::find_targets_file_version("<!-- version 12345 -->");
    REQUIRE(res.has_value());
    CHECK(*res.get() == 12345);

    res = Integrate::find_targets_file_version("<!-- version <!-- version 1 -->");
    REQUIRE(res.has_value());
    CHECK(*res.get() == 1);

    res = Integrate::find_targets_file_version("<!-- version 32 <!-- version 1 -->");
    REQUIRE(res.has_value());
    CHECK(*res.get() == 1);

    res = Integrate::find_targets_file_version("<!-- version 32 --> <!-- version 1 -->");
    REQUIRE(res.has_value());
    CHECK(*res.get() == 32);

    res = Integrate::find_targets_file_version("<!-- version 12345  -->");
    CHECK_FALSE(res.has_value());

    res = Integrate::find_targets_file_version("<!--  version 12345 -->");
    CHECK_FALSE(res.has_value());

    res = Integrate::find_targets_file_version("<!-- version -12345 -->");
    CHECK_FALSE(res.has_value());

    res = Integrate::find_targets_file_version("<!-- version -12345 --> <!-- version 1 -->");
    REQUIRE(res.has_value());
    CHECK(*res.get() == 1);

    res = Integrate::find_targets_file_version("<!-- ver 1 -->");
    CHECK_FALSE(res.has_value());
}

TEST_CASE ("get_bash_source_completion_lines", "[integrate]")
{
}

TEST_CASE ("get_zsh_autocomplete_data", "[integrate]")
{
}
