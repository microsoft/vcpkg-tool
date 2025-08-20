#include <vcpkg-test/util.h>

#include <vcpkg/base/checks.h>
#include <vcpkg/base/parse.h>

#include <vcpkg/ci-baseline.h>

#include <ostream>
#include <utility>

using namespace vcpkg;

namespace vcpkg
{
    static std::ostream& operator<<(std::ostream& os, const CiBaselineLine& line)
    {
        os << line.port_name << ':' << line.triplet.canonical_name() << '=';
        switch (line.state)
        {
            case CiBaselineState::Fail: os << "fail"; break;
            case CiBaselineState::Skip: os << "skip"; break;
            case CiBaselineState::Pass: os << "pass"; break;
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }

        return os;
    }
}

TEST_CASE ("Parse Empty", "[ci-baseline]")
{
    ParseMessages m;
    auto actual = parse_ci_baseline("", "test", m);
    CHECK(m.good());
    CHECK(actual.empty());
}

TEST_CASE ("With Sample", "[ci-baseline]")
{
    static constexpr StringLiteral example_input =
        R"(###########################################################################
## This file defines the current expected build state of ports in CI.
##
## States
##   pass - (default) the port builds in the CI system.  If a port is
##          missing from this file then it is assumed to build.
##   fail - the port does not build in the CI system.
##          This is not necessarily the same as if a port is expected to build
##          on a developers machine because it may fail due to the machine
##          configuration.  When set to fail the CI system will still attempt
##          to build the port and will report a CI failure until this file is updated.
##   skip - Do not build this port in the CI system.
##          This is added to ports that may be flaky or conflict with other
##          ports.  Please comment for why a port is skipped so it can be
##          removed when the issue is resolved.
##
##
## CI tested triplets:
##    arm64-windows
##    arm-uwp
##    x64-linux
##    x64-osx
##    x64-uwp
##    x64-windows
##    x64-windows-static
##    x64-windows-static-md
##    x86-windows
##

# Add new items alphabetically

# script ports
#vcpkg-cmake:arm64-windows=fail
#vcpkg-cmake:arm-uwp=fail
#vcpkg-cmake:x64-uwp=fail
#vcpkg-cmake:x64-windows-static=fail
#vcpkg-cmake:x64-windows-static-md=fail
#vcpkg-cmake:x86-windows=fail

#vcpkg-cmake-config:arm64-windows=fail
#vcpkg-cmake-config:arm-uwp=fail
#vcpkg-cmake-config:x64-uwp=fail
#vcpkg-cmake-config:x64-windows-static=fail
#vcpkg-cmake-config:x64-windows-static-md=fail
#vcpkg-cmake-config:x86-windows=fail

# other ports
# Cross compiling CI machine cannot run gen_test_char to generate apr_escape_test_char.h
apr:arm64-windows=fail
# Requires ATL for ARM64 to be installed in CI
azure-storage-cpp:arm64-windows=fail

# Require that test ports do not cascade
vcpkg-ci-opencv:arm64-windows=pass

aubio:arm-uwp=fail
aubio:x64-uwp=fail
# broken when `python` is python3, https://github.com/microsoft/vcpkg/issues/18937
bde:x64-linux=fail
bitserializer:x64-osx=fail
blitz:x64-uwp=fail
blitz:arm64-windows=fail
blitz:arm-uwp=fail
blosc:arm64-windows=fail
blosc:arm-uwp=fail
blosc:x64-uwp=fail
bond:arm-uwp=fail
bond:x64-osx=fail
bond:x64-uwp=fail
botan:x64-uwp=fail
breakpad:arm64-windows=fail
buck-yeh-bux:x64-linux=fail
buck-yeh-bux-mariadb-client:x64-linux=fail
caf:arm-uwp=fail
caf:x64-uwp=fail
caffe2:x86-windows=fail
caffe2:arm64-windows=fail
c-ares:arm-uwp=fail
c-ares:x64-uwp=fail
casclib:arm-uwp=fail
casclib:x64-uwp=fail
catch-classic:arm64-windows      = skip
catch-classic:arm-uwp            = skip
catch-classic:x64-linux          = skip
catch-classic:x64-osx            = skip
catch-classic:x64-uwp            = skip
catch-classic:x64-windows        = skip
catch-classic:x64-windows-static = skip
catch-classic:x64-windows-static-md=skip
catch-classic:x86-windows        = skip
bill-made-up-another-skip:x64-linux=skip)"; // note no trailing newline

    const auto& x86_windows = Test::X86_WINDOWS;
    const auto& x64_windows = Test::X64_WINDOWS;
    const auto& x64_windows_static = Test::X64_WINDOWS_STATIC;
    const auto& x64_windows_static_md = Test::X64_WINDOWS_STATIC_MD;
    const auto& x64_uwp = Test::X64_UWP;
    const auto& arm64_windows = Test::ARM64_WINDOWS;
    const auto& arm_uwp = Test::ARM_UWP;
    const auto& x64_osx = Test::X64_OSX;
    const auto& x64_linux = Test::X64_LINUX;

    std::vector<CiBaselineLine> expected_from_example_input{
        CiBaselineLine{"apr", arm64_windows, CiBaselineState::Fail},
        CiBaselineLine{"azure-storage-cpp", arm64_windows, CiBaselineState::Fail},
        CiBaselineLine{"vcpkg-ci-opencv", arm64_windows, CiBaselineState::Pass},
        CiBaselineLine{"aubio", arm_uwp, CiBaselineState::Fail},
        CiBaselineLine{"aubio", x64_uwp, CiBaselineState::Fail},
        CiBaselineLine{"bde", x64_linux, CiBaselineState::Fail},
        CiBaselineLine{"bitserializer", x64_osx, CiBaselineState::Fail},
        CiBaselineLine{"blitz", x64_uwp, CiBaselineState::Fail},
        CiBaselineLine{"blitz", arm64_windows, CiBaselineState::Fail},
        CiBaselineLine{"blitz", arm_uwp, CiBaselineState::Fail},
        CiBaselineLine{"blosc", arm64_windows, CiBaselineState::Fail},
        CiBaselineLine{"blosc", arm_uwp, CiBaselineState::Fail},
        CiBaselineLine{"blosc", x64_uwp, CiBaselineState::Fail},
        CiBaselineLine{"bond", arm_uwp, CiBaselineState::Fail},
        CiBaselineLine{"bond", x64_osx, CiBaselineState::Fail},
        CiBaselineLine{"bond", x64_uwp, CiBaselineState::Fail},
        CiBaselineLine{"botan", x64_uwp, CiBaselineState::Fail},
        CiBaselineLine{"breakpad", arm64_windows, CiBaselineState::Fail},
        CiBaselineLine{"buck-yeh-bux", x64_linux, CiBaselineState::Fail},
        CiBaselineLine{"buck-yeh-bux-mariadb-client", x64_linux, CiBaselineState::Fail},
        CiBaselineLine{"caf", arm_uwp, CiBaselineState::Fail},
        CiBaselineLine{"caf", x64_uwp, CiBaselineState::Fail},
        CiBaselineLine{"caffe2", x86_windows, CiBaselineState::Fail},
        CiBaselineLine{"caffe2", arm64_windows, CiBaselineState::Fail},
        CiBaselineLine{"c-ares", arm_uwp, CiBaselineState::Fail},
        CiBaselineLine{"c-ares", x64_uwp, CiBaselineState::Fail},
        CiBaselineLine{"casclib", arm_uwp, CiBaselineState::Fail},
        CiBaselineLine{"casclib", x64_uwp, CiBaselineState::Fail},
        CiBaselineLine{"catch-classic", arm64_windows, CiBaselineState::Skip},
        CiBaselineLine{"catch-classic", arm_uwp, CiBaselineState::Skip},
        CiBaselineLine{"catch-classic", x64_linux, CiBaselineState::Skip},
        CiBaselineLine{"catch-classic", x64_osx, CiBaselineState::Skip},
        CiBaselineLine{"catch-classic", x64_uwp, CiBaselineState::Skip},
        CiBaselineLine{"catch-classic", x64_windows, CiBaselineState::Skip},
        CiBaselineLine{"catch-classic", x64_windows_static, CiBaselineState::Skip},
        CiBaselineLine{"catch-classic", x64_windows_static_md, CiBaselineState::Skip},
        CiBaselineLine{"catch-classic", x86_windows, CiBaselineState::Skip},
        CiBaselineLine{"bill-made-up-another-skip", x64_linux, CiBaselineState::Skip}};

    SECTION ("Parse Real Prefix")
    {
        ParseMessages m;
        auto actual = parse_ci_baseline(example_input, "test", m);
        CHECK(m.good());
        CHECK(expected_from_example_input.size() == actual.size());
        size_t n = std::min(expected_from_example_input.size(), actual.size());
        for (size_t i = 0; i < n; ++i)
        {
            CHECK(expected_from_example_input[i] == actual[i]);
        }
    }

    SECTION ("Parse Real Prefix With Trailing Newline")
    {
        ParseMessages m;
        std::string newlined_input(example_input.data(), example_input.size());
        newlined_input.push_back('\n');
        auto actual = parse_ci_baseline(newlined_input, "test", m);
        CHECK(m.good());
        CHECK(expected_from_example_input == actual);
    }

    SECTION ("Applies Skips and Fails")
    {
        ExclusionsMap exclusions;
        exclusions.insert(x64_uwp);   // example triplet
        exclusions.insert(x64_linux); // example host triplet
        auto actual = parse_and_apply_ci_baseline(expected_from_example_input, exclusions, SkipFailures::No);
        const SortedVector<PackageSpec> expected_expected_failures{
            PackageSpec{"aubio", x64_uwp},
            PackageSpec{"bde", x64_linux},
            PackageSpec{"blitz", x64_uwp},
            PackageSpec{"blosc", x64_uwp},
            PackageSpec{"bond", x64_uwp},
            PackageSpec{"botan", x64_uwp},
            PackageSpec{"buck-yeh-bux", x64_linux},
            PackageSpec{"buck-yeh-bux-mariadb-client", x64_linux},
            PackageSpec{"c-ares", x64_uwp},
            PackageSpec{"caf", x64_uwp},
            PackageSpec{"casclib", x64_uwp},
        };

        CHECK(actual.expected_failures == expected_expected_failures);
        CHECK(exclusions.triplets.size() == 2);
        CHECK(exclusions.triplets[0].exclusions == SortedVector<std::string>{"catch-classic"});
        CHECK(exclusions.triplets[1].exclusions ==
              SortedVector<std::string>{"catch-classic", "bill-made-up-another-skip"});

        exclusions.triplets[0].exclusions.clear();
        exclusions.triplets[1].exclusions.clear();

        actual = parse_and_apply_ci_baseline(expected_from_example_input, exclusions, SkipFailures::Yes);
        CHECK(actual.expected_failures == expected_expected_failures);
        CHECK(exclusions.triplets.size() == 2);
        CHECK(exclusions.triplets[0].exclusions ==
              SortedVector<std::string>{
                  "aubio", "blitz", "blosc", "bond", "botan", "c-ares", "caf", "casclib", "catch-classic"});
        CHECK(exclusions.triplets[1].exclusions ==
              SortedVector<std::string>{
                  "bde", "buck-yeh-bux", "buck-yeh-bux-mariadb-client", "catch-classic", "bill-made-up-another-skip"});
    }
}

static void check_error(const std::string& input, const std::string& expected_error)
{
    ParseMessages m;
    auto actual = parse_ci_baseline(input, "test", m);
    CHECK(actual.empty());
    CHECK(!m.good());
    CHECK(m.join() == LocalizedString::from_raw(expected_error));
}

TEST_CASE ("Parse Errors", "[ci-baseline]")
{
    check_error("hello", R"(test:1:6: error: expected ':' here
  on expression: hello
                      ^)");

    check_error("hello\n:", R"(test:1:6: error: expected ':' here
  on expression: hello
                      ^)");

    check_error("?example:x64-windows=fail",
                R"(test:1:1: error: expected a port name here (must be lowercase, digits, '-')
  on expression: ?example:x64-windows=fail
                 ^)");

    check_error("x64-windows:", R"(test:1:13: error: expected a triplet name here (must be lowercase, digits, '-')
  on expression: x64-windows:
                             ^)");

    check_error("x64-windows:\nport:x64-windows=skip",
                R"(test:1:13: error: expected a triplet name here (must be lowercase, digits, '-')
  on expression: x64-windows:
                             ^)");

    check_error("x64-windows:#", R"(test:1:13: error: expected a triplet name here (must be lowercase, digits, '-')
  on expression: x64-windows:#
                             ^)");

    // clang-format off
    check_error("   \tx64-windows:", R"(test:1:21: error: expected a triplet name here (must be lowercase, digits, '-')
  on expression:    )" "\t" R"(x64-windows:
                    )" "\t" R"(            ^)");
    // clang-format on

    check_error("port:x64-windows\n=fail", R"(test:1:17: error: expected '=' here
  on expression: port:x64-windows
                                 ^)");

    check_error("example:x64-windows   =  \n  fail", R"(test:1:26: error: expected 'fail', 'skip', or 'pass' here
  on expression: example:x64-windows   =  
                                          ^)");

    // note that there is 'fail' but doesn't end on a word boundary:
    check_error("example:x64-windows   =    fails", R"(test:1:28: error: expected 'fail', 'skip', or 'pass' here
  on expression: example:x64-windows   =    fails
                                            ^)");

    check_error("example:x64-windows   =    fail extra stuff",
                R"(test:1:33: error: unrecognizable baseline entry; expected 'port:triplet=(fail|skip|pass)'
  on expression: example:x64-windows   =    fail extra stuff
                                                 ^)");

    check_error("example:x64-windows   =    fail example:x64-windows   =    fail",
                R"(test:1:33: error: unrecognizable baseline entry; expected 'port:triplet=(fail|skip|pass)'
  on expression: example:x64-windows   =    fail example:x64-windows   =    fail
                                                 ^)");

    check_error("example:x64-windows   =    fail  # extra stuff\n"
                "example:x64-uwp=skip extra stuff\n",
                R"(test:2:22: error: unrecognizable baseline entry; expected 'port:triplet=(fail|skip|pass)'
  on expression: example:x64-uwp=skip extra stuff
                                      ^)");
}

TEST_CASE ("format_ci_result 1", "[ci-baseline]")
{
    CiBaselineData cidata;
    cidata.expected_failures = {
        PackageSpec{"fail", Test::X64_UWP},
    };
    cidata.required_success = {
        PackageSpec{"pass", Test::X64_UWP},
    };
    constexpr const char failmsg[] = "REGRESSION: {0} failed with BUILD_FAILED. If expected, add {0}=fail to cifile.";
    constexpr const char cascademsg[] = "REGRESSION: {0} cascaded, but it is required to pass. (cifile).";
    constexpr const char passmsg[] = "PASSING, REMOVE FROM FAIL LIST: {0} (cifile).";
    std::string cifile = "cifile";
    SECTION ("SUCCEEDED")
    {
        const auto test = [&](PackageSpec s, bool allow_unexpected_passing) {
            return format_ci_result(s, BuildResult::Succeeded, cidata, &cifile, allow_unexpected_passing, false);
        };
        CHECK(test({"pass", Test::X64_UWP}, true) == "");
        CHECK(test({"pass", Test::X64_UWP}, false) == "");
        CHECK(test({"fail", Test::X64_UWP}, true) == "");
        CHECK(test({"fail", Test::X64_UWP}, false) == fmt::format(passmsg, "fail:x64-uwp"));
        CHECK(test({"fail", Test::ARM_UWP}, false) == "");
        CHECK(test({"neither", Test::X64_UWP}, true) == "");
        CHECK(test({"neither", Test::X64_UWP}, false) == "");
    }

    SECTION ("BUILD_FAILED")
    {
        const auto test = [&](PackageSpec s) {
            return format_ci_result(s, BuildResult::BuildFailed, cidata, &cifile, false, false);
        };
        CHECK(test({"pass", Test::X64_UWP}) == fmt::format(failmsg, "pass:x64-uwp"));
        CHECK(test({"fail", Test::X64_UWP}) == "");
        CHECK(test({"neither", Test::X64_UWP}) == fmt::format(failmsg, "neither:x64-uwp"));
    }

    SECTION ("CASCADED_DUE_TO_MISSING_DEPENDENCIES")
    {
        const auto test = [&](PackageSpec s) {
            return format_ci_result(s, BuildResult::CascadedDueToMissingDependencies, cidata, &cifile, false, false);
        };
        CHECK(test({"pass", Test::X64_UWP}) == fmt::format(cascademsg, "pass:x64-uwp"));
        CHECK(test({"fail", Test::X64_UWP}) == "");
        CHECK(test({"neither", Test::X64_UWP}) == "");
    }
}
