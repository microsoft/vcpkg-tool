#include <catch2/catch.hpp>

#include <vcpkg/base/util.h>

#include <vcpkg/packagespec.h>

#include <vcpkg-test/util.h>

using namespace vcpkg;

TEST_CASE ("specifier conversion", "[specifier]")
{
    SECTION ("full package spec to feature specs")
    {
        constexpr std::size_t SPEC_SIZE = 4;

        PackageSpec a_spec("a", Test::X64_WINDOWS);
        PackageSpec b_spec("b", Test::X64_WINDOWS);

        std::vector<FeatureSpec> fspecs;
        FullPackageSpec{a_spec, {"0", "1"}}.expand_fspecs_to(fspecs);
        FullPackageSpec{b_spec, {"2", "3"}}.expand_fspecs_to(fspecs);
        vcpkg::sort(fspecs);
        REQUIRE(fspecs.size() == SPEC_SIZE);

        std::array<const char*, SPEC_SIZE> features = {"0", "1", "2", "3"};
        std::array<PackageSpec*, SPEC_SIZE> specs = {&a_spec, &a_spec, &b_spec, &b_spec};

        for (std::size_t i = 0; i < SPEC_SIZE; ++i)
        {
            REQUIRE(features.at(i) == fspecs.at(i).feature());
            REQUIRE(*specs.at(i) == fspecs.at(i).spec());
        }
    }
}

TEST_CASE ("specifier parsing", "[specifier]")
{
    SECTION ("parsed specifier from string")
    {
        auto maybe_spec = vcpkg::parse_qualified_specifier("zlib");
        REQUIRE(maybe_spec.has_value());

        auto& spec = *maybe_spec.get();
        REQUIRE(spec.name == "zlib");
        REQUIRE(!spec.features);
        REQUIRE(!spec.triplet);
    }

    SECTION ("parsed specifier from string with triplet")
    {
        auto maybe_spec = vcpkg::parse_qualified_specifier("zlib:x64-uwp");
        REQUIRE(maybe_spec);

        auto& spec = *maybe_spec.get();
        REQUIRE(spec.name == "zlib");
        REQUIRE(spec.triplet.value_or("") == "x64-uwp");
    }

    SECTION ("parsed specifier from string with colons")
    {
        auto s = vcpkg::parse_qualified_specifier("zlib:x86-uwp:");
        REQUIRE(!s);
    }

    SECTION ("parsed specifier from string with feature")
    {
        auto maybe_spec = vcpkg::parse_qualified_specifier("zlib[feature]:x64-uwp");
        REQUIRE(maybe_spec);

        auto& spec = *maybe_spec.get();
        REQUIRE(spec.name == "zlib");
        REQUIRE(spec.features.value_or(std::vector<std::string>{}) == std::vector<std::string>{"feature"});
        REQUIRE(spec.triplet.value_or("") == "x64-uwp");
    }

    SECTION ("parsed specifier from string with many features")
    {
        auto maybe_spec = vcpkg::parse_qualified_specifier("zlib[0, 1,2]");
        REQUIRE(maybe_spec);

        auto& spec = *maybe_spec.get();
        REQUIRE(spec.features.value_or(std::vector<std::string>{}) == std::vector<std::string>{"0", "1", "2"});
    }

    SECTION ("parsed specifier wildcard feature")
    {
        auto spec = vcpkg::parse_qualified_specifier("zlib[*]").value_or_exit(VCPKG_LINE_INFO);
        REQUIRE(spec.features.value_or(std::vector<std::string>{}) == std::vector<std::string>{"*"});
    }

    SECTION ("dont expand wildcards")
    {
        std::vector<FeatureSpec> specs;
        const auto fspecs = Test::parse_test_fspecs("zlib[core,0,1]:x86-uwp openssl[*]:x86-uwp");
        for (auto&& fs : fspecs)
            fs.expand_fspecs_to(specs);
        vcpkg::sort(specs);

        std::vector<FeatureSpec> spectargets{
            {{"openssl", Test::X86_UWP}, "core"},
            {{"openssl", Test::X86_UWP}, "default"},
            {{"openssl", Test::X86_UWP}, "*"},
            {{"zlib", Test::X86_UWP}, "core"},
            {{"zlib", Test::X86_UWP}, "0"},
            {{"zlib", Test::X86_UWP}, "1"},
        };
        vcpkg::sort(spectargets);
        Test::check_ranges(specs, spectargets);
    }
}

#if defined(_WIN32)
TEST_CASE ("ascii to utf16", "[utf16]")
{
    SECTION ("ASCII to utf16")
    {
        auto str = vcpkg::Strings::to_utf16("abc");
        REQUIRE(str == L"abc");
    }

    SECTION ("ASCII to utf16 with whitespace")
    {
        auto str = vcpkg::Strings::to_utf16("abc -x86-windows");
        REQUIRE(str == L"abc -x86-windows");
    }
}
#endif
