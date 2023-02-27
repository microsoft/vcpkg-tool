#include <catch2/catch.hpp>

#include <vcpkg/base/format.h>
#include <vcpkg/base/strings.h>

#include <vcpkg/bundlesettings.h>

using namespace vcpkg;

namespace Catch
{
    template<>
    struct StringMaker<vcpkg::DeploymentKind>
    {
        static const std::string convert(const vcpkg::DeploymentKind& value)
        {
            return fmt::format("DeploymentKind::{}", value);
        }
    };
}

TEST_CASE ("parse-no-fields", "[bundle-settings]")
{
    auto result = try_parse_bundle_settings({"{}", "test"}).value_or_exit(VCPKG_LINE_INFO);
    CHECK(result.read_only == false);
    CHECK(result.use_git_registry == false);
    CHECK(!result.embedded_git_sha.has_value());
    CHECK(result.deployment == DeploymentKind::Git);
}

TEST_CASE ("parse-all-fields", "[bundle-settings]")
{
    auto result = try_parse_bundle_settings({R"json({
    "readonly": true,
    "usegitregistry": true,
    "embeddedsha": "a7a6d5edaff9d850db2d5f1378e5d9af59805e81",
    "deployment": "OneLiner"
})json",
                                             "test"})
                      .value_or_exit(VCPKG_LINE_INFO);
    CHECK(result.read_only == true);
    CHECK(result.use_git_registry == true);
    CHECK(result.embedded_git_sha.value_or_exit(VCPKG_LINE_INFO) == "a7a6d5edaff9d850db2d5f1378e5d9af59805e81");
    CHECK(result.deployment == DeploymentKind::OneLiner);
}

TEST_CASE ("parse-each-deployment", "[bundle-settings]")
{
    CHECK(try_parse_bundle_settings({R"json({"deployment": "Git"})json", "test"})
              .value_or_exit(VCPKG_LINE_INFO)
              .deployment == DeploymentKind::Git);
    CHECK(try_parse_bundle_settings({R"json({"deployment": "OneLiner"})json", "test"})
              .value_or_exit(VCPKG_LINE_INFO)
              .deployment == DeploymentKind::OneLiner);
    CHECK(try_parse_bundle_settings({R"json({"deployment": "VisualStudio"})json", "test"})
              .value_or_exit(VCPKG_LINE_INFO)
              .deployment == DeploymentKind::VisualStudio);
}

TEST_CASE ("parse-error", "[bundle-settings]")
{
    auto bad_case = GENERATE("",                          // not an object
                             "[]",                        // not an object
                             R"({"readonly": {}})",       // readonly isn't a bool
                             R"({"usegitregistry": {}})", // usegitregistry isn't a bool
                             R"({"embeddedsha": {}})",    // embeddedsha isn't a string
                             R"({"deployment": true})",   // deployment isn't a string
                             R"({"deployment": "bogus"})" // deployment isn't one of the expected values
    );

    auto result = try_parse_bundle_settings({bad_case, "test"});
    REQUIRE(!result.has_value());
    REQUIRE_THAT(result.error().data(), Catch::StartsWith("Invalid bundle definition."));
}
