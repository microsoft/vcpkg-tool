#include <catch2/catch.hpp>

#include <vcpkg/base/util.h>

#include <vcpkg/packagespec.h>
#include <vcpkg/versions.h>

#include <vcpkg-test/util.h>

using namespace vcpkg;

namespace
{
    struct TestMessageSink : MessageSink
    {
        virtual void print(Color, StringView s) override { text.append(s.begin(), s.end()); }
        std::string text;
    };

    void test_version(StringView spec_str,
                      StringView name,
                      StringView version_str,
                      int port_version,
                      Optional<std::string> triplet = nullopt)
    {
        auto maybe_spec = parse_qualified_specifier(spec_str);
        REQUIRE(maybe_spec);
        auto& spec = *maybe_spec.get();
        CHECK(spec.name == name);
        REQUIRE(spec.version);
        auto& version = *spec.version.get();
        CHECK(version.text() == version_str);
        CHECK(version.port_version() == port_version);
        CHECK(spec.triplet == triplet);
    }
}

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
        auto maybe_spec = vcpkg::parse_qualified_specifier("zlib");
        REQUIRE(maybe_spec.has_value());

        auto& spec = *maybe_spec.get();
        REQUIRE(spec.name == "zlib");
        REQUIRE(!spec.features);
        REQUIRE(!spec.triplet);
    }

    SECTION ("parsed specifier from string with version")
    {
        auto maybe_spec = vcpkg::parse_qualified_specifier("zlib[core]@1.2.13#2:x64-uwp");
        REQUIRE(maybe_spec);

        auto& spec = *maybe_spec.get();
        CHECK(spec.name == "zlib");
        REQUIRE(spec.features);
        auto& features = *spec.features.get();
        REQUIRE(features.size() == 1);
        CHECK(features[0] == "core");
        REQUIRE(spec.version);
        auto& version = *spec.version.get();
        CHECK(version.text() == "1.2.13");
        CHECK(version.port_version() == 2);
        CHECK(spec.triplet.value_or("") == "x64-uwp");
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
}

TEST_CASE ("specifier version parsing", "[specifier]")
{
    SECTION ("success cases")
    {
        // dot version
        test_version("a@1.2.13", "a", "1.2.13", 0);

        // date version
        test_version("a@2022-12-09", "a", "2022-12-09", 0);

        // string version
        test_version("a@vista", "a", "vista", 0);

        // with port-version
        test_version("a@1.2.13#2", "a", "1.2.13", 2);
        test_version("a@2022-12-09#9", "a", "2022-12-09", 9);
        test_version("a@vista#20", "a", "vista", 20);

        // with triplet
        test_version("a@1.2.13#2:x64-windows", "a", "1.2.13", 2, "x64-windows");
        test_version("a@2022-12-09#9:x86-windows", "a", "2022-12-09", 9, "x86-windows");
        test_version("a@vista#20:x64-linux-static", "a", "vista", 20, "x64-linux-static");

        // escaped version strings
        test_version(R"(a@with\ space#1)", "a", "with space", 1);
        test_version(R"(a@not\:a-triplet:x64-windows)", "a", "not:a-triplet", 0, "x64-windows");
        test_version(R"(a@https\:\/\/github.com\/Microsoft\/vcpkg\/releases\/1.0.0)",
                     "a",
                     "https://github.com/Microsoft/vcpkg/releases/1.0.0",
                     0);
        test_version(R"==(a@\!\@\$\%\^\&\*\(\)\_\-\+\=\{\}\[\]\|\\\;\:\'\"\,\<\.\>\/\?\`\~)==",
                     "a",
                     R"==(!@$%^&*()_-+={}[]|\;:'",<.>/?`~)==",
                     0);

        // with platform expressions
        test_version(
            R"(a@with\ \(parenthesis\)#2:x86-windows (static & !uwp))", "a", "with (parenthesis)", 2, "x86-windows");
    }

    // error cases
    SECTION ("no version")
    {
        auto maybe_spec = parse_qualified_specifier("a@:x64-windows");
        REQUIRE(!maybe_spec);
        CHECK(maybe_spec.error() == R"(<unknown>:1:3: error: expected a version
    on expression: a@:x64-windows
                     ^)");
    }

    SECTION ("no version 2")
    {
        auto maybe_spec = parse_qualified_specifier("a@#2:x64-windows");
        REQUIRE(!maybe_spec);
        CHECK(maybe_spec.error() == R"(<unknown>:1:3: error: expected a version
    on expression: a@#2:x64-windows
                     ^)");
    }

    SECTION ("unescaped :")
    {
        TestMessageSink test_sink;
        auto maybe_spec = parse_qualified_specifier("a@not:a-triplet:x64-windows", "test", test_sink);
        REQUIRE(!maybe_spec);

        CHECK(test_sink.text ==
              R"(test:1:6: warning: unescaped ':' detected
    on expression: a@not:a-triplet:x64-windows
                        ^
)");

        CHECK(maybe_spec.error() == R"(test:1:16: error: unexpected ':' in triplet
    on expression: a@not:a-triplet:x64-windows
                                  ^)");
    }

    SECTION ("unescaped special character warning")
    {
        TestMessageSink test_sink;
        auto maybe_spec = parse_qualified_specifier("a@hello!:x64-windows", "test", test_sink);
        REQUIRE(!maybe_spec);

        CHECK(test_sink.text ==
              R"(test:1:8: warning: unescaped '!' detected
    on expression: a@hello!:x64-windows
                          ^
)");

        CHECK(maybe_spec.error() == R"(test:1:8: error: expected eof
    on expression: a@hello!:x64-windows
                          ^)");
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
