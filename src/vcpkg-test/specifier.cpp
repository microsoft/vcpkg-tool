#include <vcpkg-test/util.h>

#include <vcpkg/base/util.h>

#include <vcpkg/documentation.h>
#include <vcpkg/packagespec.h>

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

        constexpr const char* features[SPEC_SIZE] = {"0", "1", "2", "3"};
        PackageSpec* specs[SPEC_SIZE] = {&a_spec, &a_spec, &b_spec, &b_spec};

        for (std::size_t i = 0; i < SPEC_SIZE; ++i)
        {
            REQUIRE(features[i] == fspecs[i].feature());
            REQUIRE(*specs[i] == fspecs[i].spec());
        }
    }
}

TEST_CASE ("specifier parsing", "[specifier]")
{
    SECTION ("parsed specifier from string")
    {
        auto spec = vcpkg::parse_qualified_specifier(
                        "zlib", AllowFeatures::No, ParseExplicitTriplet::Forbid, AllowPlatformSpec::No)
                        .value_or_exit(VCPKG_LINE_INFO);
        REQUIRE(spec.name.value == "zlib");
        REQUIRE(!spec.features);
        REQUIRE(!spec.triplet);
        REQUIRE(!spec.platform);

        auto full_spec_implicit = spec.to_full_spec(Test::X86_WINDOWS, ImplicitDefault::Yes);
        REQUIRE(full_spec_implicit.package_spec.name() == "zlib");
        REQUIRE(full_spec_implicit.package_spec.triplet() == Test::X86_WINDOWS);
        REQUIRE(full_spec_implicit.features == std::vector<std::string>{"core", "default"});

        auto full_spec_explicit = spec.to_full_spec(Test::X86_WINDOWS, ImplicitDefault::No);
        REQUIRE(full_spec_explicit.package_spec.name() == "zlib");
        REQUIRE(full_spec_explicit.package_spec.triplet() == Test::X86_WINDOWS);
        REQUIRE(full_spec_explicit.features == std::vector<std::string>{"core"});

        auto package_spec = spec.to_package_spec(Test::X86_WINDOWS);
        REQUIRE(package_spec.name() == "zlib");
        REQUIRE(package_spec.triplet() == Test::X86_WINDOWS);
    }

    SECTION ("parsed specifier from string with triplet")
    {
        auto spec = vcpkg::parse_qualified_specifier(
                        "zlib:x64-uwp", AllowFeatures::No, ParseExplicitTriplet::Require, AllowPlatformSpec::No)
                        .value_or_exit(VCPKG_LINE_INFO);
        REQUIRE(spec.name.value == "zlib");
        REQUIRE(!spec.features);
        REQUIRE(spec.triplet.value_or_exit(VCPKG_LINE_INFO).value == "x64-uwp");
        REQUIRE(!spec.platform);

        auto full_spec_implicit = spec.to_full_spec(Test::X86_WINDOWS, ImplicitDefault::Yes);
        REQUIRE(full_spec_implicit.package_spec.name() == "zlib");
        REQUIRE(full_spec_implicit.package_spec.triplet() == Test::X64_UWP);
        REQUIRE(full_spec_implicit.features == std::vector<std::string>{"core", "default"});

        auto full_spec_explicit = spec.to_full_spec(Test::X86_WINDOWS, ImplicitDefault::No);
        REQUIRE(full_spec_explicit.package_spec.name() == "zlib");
        REQUIRE(full_spec_explicit.package_spec.triplet() == Test::X64_UWP);
        REQUIRE(full_spec_explicit.features == std::vector<std::string>{"core"});

        auto package_spec = spec.to_package_spec(Test::X86_WINDOWS);
        REQUIRE(package_spec.name() == "zlib");
        REQUIRE(package_spec.triplet() == Test::X64_UWP);
    }

    SECTION ("parsed specifier from string with colons")
    {
        auto s = vcpkg::parse_qualified_specifier(
            "zlib:x86-uwp:", AllowFeatures::Yes, ParseExplicitTriplet::Allow, AllowPlatformSpec::Yes);
        if (s.has_value())
        {
            FAIL();
        }
        else
        {
            REQUIRE(
                s.error() ==
                LocalizedString::from_raw(
                    R"(error: expected the end of input parsing a package spec; this usually means the indicated character is not allowed to be in a package spec. Port, triplet, and feature names are all lowercase alphanumeric+hyphens.
  on expression: zlib:x86-uwp:
                             ^)"));
        }
    }

    SECTION ("parsed specifier from string with illegal character package name special case")
    {
        // we emit a better error message here because without features, triplet, or platform specification, we know the
        // caller only wants a port name
        auto s = vcpkg::parse_qualified_specifier(
            "zlib#", AllowFeatures::No, ParseExplicitTriplet::Forbid, AllowPlatformSpec::No);
        if (s.has_value())
        {
            FAIL();
        }
        else
        {
            REQUIRE(
                s.error() ==
                LocalizedString::from_raw(
                    R"(error: expected the end of input parsing a package name; this usually means the indicated character is not allowed to be in a port name. Port names are all lowercase alphanumeric+hyphens and not reserved (see )" +
                    docs::vcpkg_json_ref_name + R"( for more information).
  on expression: zlib#
                     ^)"));
        }
    }

    SECTION ("parsed specifier from string with illegal character")
    {
        auto s = vcpkg::parse_qualified_specifier(
            "zlib#", AllowFeatures::Yes, ParseExplicitTriplet::Allow, AllowPlatformSpec::Yes);
        if (s.has_value())
        {
            FAIL();
        }
        else
        {
            REQUIRE(
                s.error() ==
                LocalizedString::from_raw(
                    R"(error: expected the end of input parsing a package spec; this usually means the indicated character is not allowed to be in a package spec. Port, triplet, and feature names are all lowercase alphanumeric+hyphens.
  on expression: zlib#
                     ^)"));
        }
    }

    SECTION ("parsed specifier from string with colons")
    {
        auto s = vcpkg::parse_qualified_specifier(
            "zlib:x86-uwp:", AllowFeatures::Yes, ParseExplicitTriplet::Allow, AllowPlatformSpec::Yes);
        if (s.has_value())
        {
            FAIL();
        }
        else
        {
            REQUIRE(
                s.error() ==
                LocalizedString::from_raw(
                    R"(error: expected the end of input parsing a package spec; this usually means the indicated character is not allowed to be in a package spec. Port, triplet, and feature names are all lowercase alphanumeric+hyphens.
  on expression: zlib:x86-uwp:
                             ^)"));
        }
    }

    SECTION ("parsed specifier with feature in the wrong order")
    {
        auto s = vcpkg::parse_qualified_specifier(
            "zlib:x86-uwp[co,  re]", AllowFeatures::Yes, ParseExplicitTriplet::Allow, AllowPlatformSpec::Yes);
        if (s.has_value())
        {
            FAIL();
        }
        else
        {
            REQUIRE(
                s.error() ==
                LocalizedString::from_raw(
                    R"(error: expected the end of input parsing a package spec; did you mean zlib[co,re]:x86-uwp instead?
  on expression: zlib:x86-uwp[co,  re]
                             ^)"));
        }
    }

    SECTION ("parsed specifier with feature in the wrong order but no triplet")
    {
        auto s = vcpkg::parse_qualified_specifier(
            "zlib(windows)[co,  re]", AllowFeatures::Yes, ParseExplicitTriplet::Allow, AllowPlatformSpec::Yes);
        if (s.has_value())
        {
            FAIL();
        }
        else
        {
            REQUIRE(
                s.error() ==
                LocalizedString::from_raw(
                    R"(error: expected the end of input parsing a package spec; this usually means the indicated character is not allowed to be in a package spec. Port, triplet, and feature names are all lowercase alphanumeric+hyphens.
  on expression: zlib(windows)[co,  re]
                              ^)"));
        }
    }

    SECTION ("parsed specifier with feature in the wrong order but also platform expression")
    {
        auto s = vcpkg::parse_qualified_specifier(
            "zlib:x86-uwp (windows)[co,  re]", AllowFeatures::Yes, ParseExplicitTriplet::Allow, AllowPlatformSpec::Yes);
        if (s.has_value())
        {
            FAIL();
        }
        else
        {
            REQUIRE(
                s.error() ==
                LocalizedString::from_raw(
                    R"(error: expected the end of input parsing a package spec; this usually means the indicated character is not allowed to be in a package spec. Port, triplet, and feature names are all lowercase alphanumeric+hyphens.
  on expression: zlib:x86-uwp (windows)[co,  re]
                                       ^)"));
        }
    }

    SECTION ("parsed specifier from string with unclosed feature suffix")
    {
        // even though there is a [, that doesn't parse as a valid feature list so the 'did you mean?' special case
        // should not engage
        auto s = vcpkg::parse_qualified_specifier("zlib:x64-windows[no-ending-square-bracket",
                                                  AllowFeatures::Yes,
                                                  ParseExplicitTriplet::Allow,
                                                  AllowPlatformSpec::Yes);
        if (s.has_value())
        {
            FAIL();
        }
        else
        {
            REQUIRE(
                s.error() ==
                LocalizedString::from_raw(
                    R"(error: expected the end of input parsing a package spec; this usually means the indicated character is not allowed to be in a package spec. Port, triplet, and feature names are all lowercase alphanumeric+hyphens.
  on expression: zlib:x64-windows[no-ending-square-bracket
                                 ^)"));
        }
    }

    SECTION ("parsed specifier from string with malformed feature suffix")
    {
        auto s = vcpkg::parse_qualified_specifier(
            "zlib:x64-windows[feature]suffix", AllowFeatures::Yes, ParseExplicitTriplet::Allow, AllowPlatformSpec::Yes);
        if (s.has_value())
        {
            FAIL();
        }
        else
        {
            REQUIRE(
                s.error() ==
                LocalizedString::from_raw(
                    R"(error: expected the end of input parsing a package spec; this usually means the indicated character is not allowed to be in a package spec. Port, triplet, and feature names are all lowercase alphanumeric+hyphens.
  on expression: zlib:x64-windows[feature]suffix
                                 ^)"));
        }
    }

    SECTION ("parsed specifier from string with feature")
    {
        auto spec =
            vcpkg::parse_qualified_specifier(
                "zlib[feature]:x64-uwp", AllowFeatures::Yes, ParseExplicitTriplet::Allow, AllowPlatformSpec::Yes)
                .value_or_exit(VCPKG_LINE_INFO);
        REQUIRE(spec.name.value == "zlib");
        SourceLoc feature_loc{{}, {}, 0, 6};
        REQUIRE(spec.features.value_or_exit(VCPKG_LINE_INFO) ==
                std::vector<Located<std::string>>{Located<std::string>(feature_loc, "feature")});
        REQUIRE(spec.triplet.value_or_exit(VCPKG_LINE_INFO).value == "x64-uwp");
        REQUIRE(!spec.platform);

        auto full_spec_implicit = spec.to_full_spec(Test::X86_WINDOWS, ImplicitDefault::Yes);
        REQUIRE(full_spec_implicit.package_spec.name() == "zlib");
        REQUIRE(full_spec_implicit.package_spec.triplet() == Test::X64_UWP);
        REQUIRE(full_spec_implicit.features == std::vector<std::string>{"feature", "core", "default"});

        auto full_spec_explicit = spec.to_full_spec(Test::X86_WINDOWS, ImplicitDefault::No);
        REQUIRE(full_spec_explicit.package_spec.name() == "zlib");
        REQUIRE(full_spec_explicit.package_spec.triplet() == Test::X64_UWP);
        REQUIRE(full_spec_explicit.features == std::vector<std::string>{"feature", "core"});

        auto spec_forbidding_features = vcpkg::parse_qualified_specifier(
            "zlib[feature]:x64-uwp", AllowFeatures::No, ParseExplicitTriplet::Allow, AllowPlatformSpec::Yes);
        REQUIRE(!spec_forbidding_features.has_value());
        REQUIRE(spec_forbidding_features.error() ==
                LocalizedString::from_raw(R"(error: List of features is not allowed in this context
  on expression: zlib[feature]:x64-uwp
                     ^)"));
    }

    SECTION ("parsed specifier from string with many features")
    {
        auto spec = vcpkg::parse_qualified_specifier(
                        "zlib[0, 1,2]", AllowFeatures::Yes, ParseExplicitTriplet::Allow, AllowPlatformSpec::Yes)
                        .value_or_exit(VCPKG_LINE_INFO);
        REQUIRE(spec.name.value == "zlib");
        SourceLoc zero_loc{{}, {}, 0, 6};
        SourceLoc one_loc{{}, {}, 0, 9};
        SourceLoc two_loc{{}, {}, 0, 11};
        REQUIRE(spec.features.value_or_exit(VCPKG_LINE_INFO) ==
                std::vector<Located<std::string>>{Located<std::string>{zero_loc, "0"},
                                                  Located<std::string>{one_loc, "1"},
                                                  Located<std::string>{two_loc, "2"}});
        REQUIRE(!spec.triplet);
        REQUIRE(!spec.platform);
    }

    SECTION ("parsed specifier wildcard feature")
    {
        auto spec = vcpkg::parse_qualified_specifier(
                        "zlib[*]", AllowFeatures::Yes, ParseExplicitTriplet::Allow, AllowPlatformSpec::Yes)
                        .value_or_exit(VCPKG_LINE_INFO);
        SourceLoc star_loc{{}, {}, 0, 6};
        REQUIRE(spec.name.value == "zlib");
        REQUIRE(spec.features.value_or_exit(VCPKG_LINE_INFO) ==
                std::vector<Located<std::string>>{Located<std::string>{star_loc, "*"}});
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
        auto spec = vcpkg::parse_qualified_specifier(
                        "zlib (windows)", AllowFeatures::Yes, ParseExplicitTriplet::Allow, AllowPlatformSpec::Yes)
                        .value_or_exit(VCPKG_LINE_INFO);
        REQUIRE(spec.name.value == "zlib");
        REQUIRE(!spec.features);
        REQUIRE(!spec.triplet);
        REQUIRE(to_string(spec.platform.value_or_exit(VCPKG_LINE_INFO).value) == "windows");

        auto forbidden_spec = vcpkg::parse_qualified_specifier(
            "zlib (windows)", AllowFeatures::Yes, ParseExplicitTriplet::Allow, AllowPlatformSpec::No);
        REQUIRE(!forbidden_spec.has_value());
        REQUIRE(forbidden_spec.error() ==
                LocalizedString::from_raw(R"(error: Platform qualifier is not allowed in this context
  on expression: zlib (windows)
                      ^)"));
    }
}
