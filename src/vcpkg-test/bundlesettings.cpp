#include <vcpkg-test/util.h>

#include <vcpkg/base/fmt.h>

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
    CHECK(!result.vsversion.has_value());
}

TEST_CASE ("parse-all-fields", "[bundle-settings]")
{
    auto result = try_parse_bundle_settings({R"json({
    "readonly": true,
    "usegitregistry": true,
    "embeddedsha": "a7a6d5edaff9d850db2d5f1378e5d9af59805e81",
    "deployment": "OneLiner",
    "vsversion": "16.0"
})json",
                                             "test"})
                      .value_or_exit(VCPKG_LINE_INFO);
    CHECK(result.read_only == true);
    CHECK(result.use_git_registry == true);
    CHECK(result.embedded_git_sha.value_or_exit(VCPKG_LINE_INFO) == "a7a6d5edaff9d850db2d5f1378e5d9af59805e81");
    CHECK(result.deployment == DeploymentKind::OneLiner);
    CHECK(result.vsversion.value_or_exit(VCPKG_LINE_INFO) == "16.0");
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
    auto bad_case = GENERATE("",                           // not an object
                             "[]",                         // not an object
                             R"({"readonly": {}})",        // readonly isn't a bool
                             R"({"usegitregistry": {}})",  // usegitregistry isn't a bool
                             R"({"embeddedsha": {}})",     // embeddedsha isn't a string
                             R"({"deployment": true})",    // deployment isn't a string
                             R"({"deployment": "bogus"})", // deployment isn't one of the expected values
                             R"({"vsversion": true})"      // vsversion isn't a string
    );

    auto result = try_parse_bundle_settings({bad_case, "test"});
    REQUIRE(!result.has_value());
    REQUIRE_THAT(result.error().data(), Catch::StartsWith("Invalid bundle definition."));
}

TEST_CASE ("to_string", "[bundle-settings]")
{
    BundleSettings uut;
    uut.deployment = GENERATE(DeploymentKind::Git, DeploymentKind::OneLiner, DeploymentKind::VisualStudio);
    std::string expected_git_sha;
    bool has_git_sha = GENERATE(false, true);
    if (has_git_sha)
    {
        uut.embedded_git_sha.emplace("a7a6d5edaff9d850db2d5f1378e5d9af59805e81");
        expected_git_sha = "a7a6d5edaff9d850db2d5f1378e5d9af59805e81";
    }
    else
    {
        expected_git_sha = "nullopt";
    }

    uut.read_only = GENERATE(false, true);
    uut.use_git_registry = GENERATE(false, true);
    std::string expected_vs_version;
    bool has_vsver = GENERATE(false, true);
    if (has_vsver)
    {
        uut.vsversion.emplace("16.0");
        expected_vs_version = "16.0";
    }
    else
    {
        expected_vs_version = "nullopt";
    }

    REQUIRE(uut.to_string() ==
            fmt::format("readonly={}, usegitregistry={}, embeddedsha={}, deployment={}, vsversion={}",
                        uut.read_only,
                        uut.use_git_registry,
                        expected_git_sha,
                        uut.deployment,
                        expected_vs_version));
}
