#include <catch2/catch.hpp>

#include <vcpkg/base/strings.h>

#include <vcpkg/bundlesettings.h>

using namespace vcpkg;

TEST_CASE ("parse-no-fields", "[bundle-settings]")
{
    auto result = try_parse_bundle_settings({"{}", "test"}).value_or_exit(VCPKG_LINE_INFO);
    CHECK(result.read_only == false);
    CHECK(result.use_git_registry == false);
    CHECK(!result.embedded_git_sha.has_value());
}

TEST_CASE ("parse-all-fields", "[bundle-settings]")
{
    auto result = try_parse_bundle_settings({R"json({
    "readonly": true,
    "usegitregistry": true,
    "embeddedsha": "a7a6d5edaff9d850db2d5f1378e5d9af59805e81"
})json",
                                             "test"})
                      .value_or_exit(VCPKG_LINE_INFO);
    CHECK(result.read_only == true);
    CHECK(result.use_git_registry == true);
    CHECK(result.embedded_git_sha.value_or_exit(VCPKG_LINE_INFO) == "a7a6d5edaff9d850db2d5f1378e5d9af59805e81");
}

TEST_CASE ("parse-error", "[bundle-settings]")
{
    auto bad_case = GENERATE("",                          // not an object
                             "[]",                        // not an object
                             R"({"readonly": {}})",       // readonly isn't a bool
                             R"({"usegitregistry": {}})", // usegitregistry isn't a bool
                             R"({"embeddedsha": {}})"     // embeddedsha isn't a string
    );

    auto result = try_parse_bundle_settings({bad_case, "test"});
    REQUIRE(!result.has_value());
    REQUIRE_THAT(result.error().data(), Catch::StartsWith("Invalid bundle definition."));
}
