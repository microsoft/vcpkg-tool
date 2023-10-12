#include <catch2/catch.hpp>

#include <vcpkg/commands.depend-info.h>
#include <vcpkg/vcpkgcmdarguments.h>

#include <limits.h>

using namespace vcpkg;

TEST_CASE ("determine_depend_info_mode no args", "[depend-info]")
{
    ParsedArguments pa;
    auto result = determine_depend_info_mode(pa).value_or_exit(VCPKG_LINE_INFO);
    REQUIRE(result.sort_mode == DependInfoSortMode::Topological);
    REQUIRE(result.format == DependInfoFormat::List);
    REQUIRE(result.max_depth == INT_MAX);
    REQUIRE(!result.show_depth);
}

TEST_CASE ("determine_depend_info_mode formats", "[depend-info]")
{
    ParsedArguments pa;
    DependInfoFormat expected = DependInfoFormat::List;
    SECTION ("list")
    {
        pa.settings.emplace("format", "list");
    }

    SECTION ("tree")
    {
        pa.settings.emplace("format", "tree");
        expected = DependInfoFormat::Tree;
    }

    SECTION ("tree sort")
    {
        pa.settings.emplace("sort", "x-tree");
        expected = DependInfoFormat::Tree;
    }

    SECTION ("tree tree sort")
    {
        pa.settings.emplace("format", "tree");
        pa.settings.emplace("sort", "x-tree");
        expected = DependInfoFormat::Tree;
    }

    SECTION ("dot")
    {
        pa.switches.insert("dot");
        expected = DependInfoFormat::Dot;
    }

    SECTION ("dot format")
    {
        pa.settings.emplace("format", "dot");
        expected = DependInfoFormat::Dot;
    }

    SECTION ("dot and format")
    {
        pa.switches.insert("dot");
        pa.settings.emplace("format", "dot");
        expected = DependInfoFormat::Dot;
    }

    SECTION ("dgml")
    {
        pa.switches.insert("dgml");
        expected = DependInfoFormat::Dgml;
    }

    SECTION ("dgml format")
    {
        pa.settings.emplace("format", "dgml");
        expected = DependInfoFormat::Dgml;
    }

    SECTION ("dgml and format")
    {
        pa.switches.insert("dgml");
        pa.settings.emplace("format", "dgml");
        expected = DependInfoFormat::Dgml;
    }

    SECTION ("mermaid")
    {
        pa.settings.emplace("format", "mermaid");
        expected = DependInfoFormat::Mermaid;
    }

    auto result = determine_depend_info_mode(pa).value_or_exit(VCPKG_LINE_INFO);
    REQUIRE(result.sort_mode == DependInfoSortMode::Topological);
    REQUIRE(result.format == expected);
    REQUIRE(result.max_depth == INT_MAX);
    REQUIRE(!result.show_depth);
}

TEST_CASE ("determine_depend_info_mode sorts", "[depend-info]")
{
    ParsedArguments pa;
    DependInfoSortMode expected = DependInfoSortMode::Topological;

    SECTION ("topological default")
    {
        // intentionally empty
    }

    SECTION ("topological")
    {
        pa.settings.emplace("sort", "topological");
    }

    SECTION ("reverse topological")
    {
        pa.settings.emplace("sort", "reverse");
        expected = DependInfoSortMode::ReverseTopological;
    }

    SECTION ("lexicographical")
    {
        pa.settings.emplace("sort", "lexicographical");
        expected = DependInfoSortMode::Lexicographical;
    }

    auto result = determine_depend_info_mode(pa).value_or_exit(VCPKG_LINE_INFO);
    REQUIRE(result.sort_mode == expected);
    REQUIRE(result.format == DependInfoFormat::List);
    REQUIRE(result.max_depth == INT_MAX);
    REQUIRE(!result.show_depth);
}

TEST_CASE ("determine_depend_info_mode max_depth", "[depend-info]")
{
    ParsedArguments pa;
    int expected = INT_MAX;
    SECTION ("default")
    {
        // intentionally empty
    }

    SECTION ("zero")
    {
        expected = 0;
        pa.settings.emplace("max-recurse", "0");
    }

    SECTION ("negative one")
    {
        expected = INT_MAX;
        pa.settings.emplace("max-recurse", "-1");
    }

    SECTION ("negative")
    {
        expected = INT_MAX;
        pa.settings.emplace("max-recurse", "-10");
    }

    SECTION ("positive")
    {
        expected = 2;
        pa.settings.emplace("max-recurse", "2");
    }

    auto result = determine_depend_info_mode(pa).value_or_exit(VCPKG_LINE_INFO);
    REQUIRE(result.sort_mode == DependInfoSortMode::Topological);
    REQUIRE(result.format == DependInfoFormat::List);
    REQUIRE(result.max_depth == expected);
    REQUIRE(!result.show_depth);
}

TEST_CASE ("determine_depend_info_mode show_depth", "[depend-info]")
{
    ParsedArguments pa;
    pa.switches.emplace("show-depth");
    auto result = determine_depend_info_mode(pa).value_or_exit(VCPKG_LINE_INFO);
    REQUIRE(result.sort_mode == DependInfoSortMode::Topological);
    REQUIRE(result.format == DependInfoFormat::List);
    REQUIRE(result.max_depth == INT_MAX);
    REQUIRE(result.show_depth);
}

TEST_CASE ("determine_depend_info_mode errors", "[depend-info]")
{
    ParsedArguments pa;
    auto expected = LocalizedString::from_raw("error: ");

    SECTION ("bad format")
    {
        pa.settings.emplace("format", "frobinate");
        expected.append_raw("--format=frobinate is not a recognized format. --format must be one of `list`, `tree`, "
                            "`mermaid`, `dot`, or `dgml`.");
    }

    SECTION ("bad sort")
    {
        pa.settings.emplace("sort", "frobinate");
        expected.append_raw("Value of --sort must be one of 'lexicographical', 'topological', 'reverse'.");
    }

    SECTION ("bad legacy switches")
    {
        pa.settings.emplace("format", "list");
        expected.append_raw("Conflicting formats specified. Only one of --format, --dgml, or --dot are accepted.");

        SECTION ("dot")
        {
            pa.switches.emplace("dot");
        }

        SECTION ("dgml")
        {
            pa.switches.emplace("dot");
        }
    }

    SECTION ("bad format sort tree")
    {
        pa.settings.emplace("format", "list");
        pa.settings.emplace("sort", "x-tree");
        expected.append_raw("--sort=x-tree cannot be used with formats other than tree");
    }

    SECTION ("show depth with graphs")
    {
        pa.switches.emplace("show-depth");
        expected.append_raw("--show-depth can only be used with `list` and `tree` formats.");

        SECTION ("dot")
        {
            pa.settings.emplace("format", "dot");
        }

        SECTION ("dgml")
        {
            pa.settings.emplace("format", "dgml");
        }

        SECTION ("mermaid")
        {
            pa.settings.emplace("format", "mermaid");
        }
    }

    SECTION ("bad max depth non numeric")
    {
        pa.settings.emplace("max-recurse", "frobinate");
        expected.append_raw("Value of --max-recurse must be an integer.");
    }

    SECTION ("bad max depth too low")
    {
        static_assert(-2147483648 == INT_MIN, "integer range assumption");
        pa.settings.emplace("max-recurse", "-2147483649");
        expected.append_raw("Value of --max-recurse must be an integer.");
    }

    SECTION ("bad max depth too high")
    {
        static_assert(2147483647 == INT_MAX, "integer range assumption");
        pa.settings.emplace("max-recurse", "2147483648");
        expected.append_raw("Value of --max-recurse must be an integer.");
    }

    REQUIRE(determine_depend_info_mode(pa).error() == expected);
}
