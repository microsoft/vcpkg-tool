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
        Util::sort(fspecs);
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
        auto spec = vcpkg::parse_qualified_specifier("zlib").value_or_exit(VCPKG_LINE_INFO);
        REQUIRE(spec.name == "zlib");
        REQUIRE(!spec.features);
        REQUIRE(!spec.triplet);
        REQUIRE(!spec.platform);

        bool default_triplet_used = false;
        auto full_spec_implicit = spec.to_full_spec(Test::X86_WINDOWS, default_triplet_used, ImplicitDefault::YES)
                                      .value_or_exit(VCPKG_LINE_INFO);
        REQUIRE(default_triplet_used);
        default_triplet_used = false;
        REQUIRE(full_spec_implicit.package_spec.name() == "zlib");
        REQUIRE(full_spec_implicit.package_spec.triplet() == Test::X86_WINDOWS);
        REQUIRE(full_spec_implicit.features == std::vector<std::string>{"core", "default"});

        auto full_spec_explicit = spec.to_full_spec(Test::X86_WINDOWS, default_triplet_used, ImplicitDefault::NO)
                                      .value_or_exit(VCPKG_LINE_INFO);
        REQUIRE(default_triplet_used);
        default_triplet_used = false;
        REQUIRE(full_spec_explicit.package_spec.name() == "zlib");
        REQUIRE(full_spec_explicit.package_spec.triplet() == Test::X86_WINDOWS);
        REQUIRE(full_spec_explicit.features == std::vector<std::string>{"core"});

        auto package_spec =
            spec.to_package_spec(Test::X86_WINDOWS, default_triplet_used).value_or_exit(VCPKG_LINE_INFO);
        REQUIRE(default_triplet_used);
        REQUIRE(package_spec.name() == "zlib");
        REQUIRE(package_spec.triplet() == Test::X86_WINDOWS);
    }

    SECTION ("parsed specifier from string with triplet")
    {
        auto spec = vcpkg::parse_qualified_specifier("zlib:x64-uwp").value_or_exit(VCPKG_LINE_INFO);
        REQUIRE(spec.name == "zlib");
        REQUIRE(!spec.features);
        REQUIRE(spec.triplet.value_or_exit(VCPKG_LINE_INFO) == "x64-uwp");
        REQUIRE(!spec.platform);

        bool default_triplet_used = false;
        auto full_spec_implicit = spec.to_full_spec(Test::X86_WINDOWS, default_triplet_used, ImplicitDefault::YES)
                                      .value_or_exit(VCPKG_LINE_INFO);
        REQUIRE(!default_triplet_used);
        REQUIRE(full_spec_implicit.package_spec.name() == "zlib");
        REQUIRE(full_spec_implicit.package_spec.triplet() == Test::X64_UWP);
        REQUIRE(full_spec_implicit.features == std::vector<std::string>{"core", "default"});

        auto full_spec_explicit = spec.to_full_spec(Test::X86_WINDOWS, default_triplet_used, ImplicitDefault::NO)
                                      .value_or_exit(VCPKG_LINE_INFO);
        REQUIRE(!default_triplet_used);
        REQUIRE(full_spec_explicit.package_spec.name() == "zlib");
        REQUIRE(full_spec_explicit.package_spec.triplet() == Test::X64_UWP);
        REQUIRE(full_spec_explicit.features == std::vector<std::string>{"core"});

        auto package_spec =
            spec.to_package_spec(Test::X86_WINDOWS, default_triplet_used).value_or_exit(VCPKG_LINE_INFO);
        REQUIRE(!default_triplet_used);
        REQUIRE(package_spec.name() == "zlib");
        REQUIRE(package_spec.triplet() == Test::X64_UWP);
    }

    SECTION ("parsed specifier from string with colons")
    {
        auto s = vcpkg::parse_qualified_specifier("zlib:x86-uwp:");
        REQUIRE(!s);
    }

    SECTION ("parsed specifier from string with feature")
    {
        auto spec = vcpkg::parse_qualified_specifier("zlib[feature]:x64-uwp").value_or_exit(VCPKG_LINE_INFO);
        REQUIRE(spec.name == "zlib");
        REQUIRE(spec.features.value_or(std::vector<std::string>{}) == std::vector<std::string>{"feature"});
        REQUIRE(spec.triplet.value_or("") == "x64-uwp");
        REQUIRE(!spec.platform);

        bool default_triplet_used = false;
        auto full_spec_implicit = spec.to_full_spec(Test::X86_WINDOWS, default_triplet_used, ImplicitDefault::YES)
                                      .value_or_exit(VCPKG_LINE_INFO);
        REQUIRE(!default_triplet_used);
        REQUIRE(full_spec_implicit.package_spec.name() == "zlib");
        REQUIRE(full_spec_implicit.package_spec.triplet() == Test::X64_UWP);
        REQUIRE(full_spec_implicit.features == std::vector<std::string>{"feature", "core", "default"});

        auto full_spec_explicit = spec.to_full_spec(Test::X86_WINDOWS, default_triplet_used, ImplicitDefault::NO)
                                      .value_or_exit(VCPKG_LINE_INFO);
        REQUIRE(!default_triplet_used);
        REQUIRE(full_spec_explicit.package_spec.name() == "zlib");
        REQUIRE(full_spec_explicit.package_spec.triplet() == Test::X64_UWP);
        REQUIRE(full_spec_explicit.features == std::vector<std::string>{"feature", "core"});

        auto maybe_package_spec = spec.to_package_spec(Test::X86_WINDOWS, default_triplet_used);
        REQUIRE(!default_triplet_used);
        REQUIRE(!maybe_package_spec.has_value());
        REQUIRE(maybe_package_spec.error() ==
                LocalizedString::from_raw("error: List of features is not allowed in this context"));
    }

    SECTION ("parsed specifier from string with many features")
    {
        auto spec = vcpkg::parse_qualified_specifier("zlib[0, 1,2]").value_or_exit(VCPKG_LINE_INFO);
        REQUIRE(spec.name == "zlib");
        REQUIRE(spec.features.value_or_exit(VCPKG_LINE_INFO) == std::vector<std::string>{"0", "1", "2"});
        REQUIRE(!spec.triplet);
        REQUIRE(!spec.platform);
    }

    SECTION ("parsed specifier wildcard feature")
    {
        auto spec = vcpkg::parse_qualified_specifier("zlib[*]").value_or_exit(VCPKG_LINE_INFO);
        REQUIRE(spec.name == "zlib");
        REQUIRE(spec.features.value_or_exit(VCPKG_LINE_INFO) == std::vector<std::string>{"*"});
        REQUIRE(!spec.triplet);
        REQUIRE(!spec.platform);
    }

    SECTION ("dont expand wildcards")
    {
        std::vector<FeatureSpec> specs;
        const auto fspecs = Test::parse_test_fspecs("zlib[core,0,1]:x86-uwp openssl[*]:x86-uwp");
        for (auto&& fs : fspecs)
        {
            fs.expand_fspecs_to(specs);
        }

        Util::sort(specs);
        std::vector<FeatureSpec> spectargets{
            {{"openssl", Test::X86_UWP}, "core"},
            {{"openssl", Test::X86_UWP}, "default"},
            {{"openssl", Test::X86_UWP}, "*"},
            {{"zlib", Test::X86_UWP}, "core"},
            {{"zlib", Test::X86_UWP}, "0"},
            {{"zlib", Test::X86_UWP}, "1"},
        };

        Util::sort(spectargets);
        Test::check_ranges(specs, spectargets);
    }

    SECTION ("parsed qualifier platform expression")
    {
        // this form was used in CONTROL files
        auto spec = vcpkg::parse_qualified_specifier("zlib (windows)").value_or_exit(VCPKG_LINE_INFO);
        REQUIRE(spec.name == "zlib");
        REQUIRE(!spec.features);
        REQUIRE(!spec.triplet);
        REQUIRE(to_string(spec.platform.value_or_exit(VCPKG_LINE_INFO)) == "windows");

        bool default_triplet_used = false;
        auto maybe_full_spec_implicit =
            spec.to_full_spec(Test::X86_WINDOWS, default_triplet_used, ImplicitDefault::YES);
        REQUIRE(!default_triplet_used);
        REQUIRE(!maybe_full_spec_implicit.has_value());
        REQUIRE(maybe_full_spec_implicit.error() ==
                LocalizedString::from_raw("error: Platform qualifier is not allowed in this context"));

        auto maybe_full_spec_explicit = spec.to_full_spec(Test::X86_WINDOWS, default_triplet_used, ImplicitDefault::NO);
        REQUIRE(!default_triplet_used);
        REQUIRE(!maybe_full_spec_explicit.has_value());
        REQUIRE(maybe_full_spec_explicit.error() ==
                LocalizedString::from_raw("error: Platform qualifier is not allowed in this context"));

        auto maybe_package_spec = spec.to_package_spec(Test::X86_WINDOWS, default_triplet_used);
        REQUIRE(!default_triplet_used);
        REQUIRE(!maybe_package_spec.has_value());
        REQUIRE(maybe_package_spec.error() ==
                LocalizedString::from_raw("error: Platform qualifier is not allowed in this context"));
    }
}
