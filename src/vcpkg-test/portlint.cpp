#include <catch2/catch.hpp>

#include <vcpkg/portlint.h>

using namespace vcpkg;
using namespace vcpkg::Lint;

TEST_CASE ("Lint::get_recommended_license_expression", "[portlint]")
{
    REQUIRE(get_recommended_license_expression("GPL-1.0") == "GPL-1.0-only");
    REQUIRE(get_recommended_license_expression("GPL-1.0 OR test") == "GPL-1.0-only OR test");
    REQUIRE(get_recommended_license_expression("GPL-1.0 OR GPL-1.0") == "GPL-1.0-only OR GPL-1.0-only");
    REQUIRE(get_recommended_license_expression("GPL-1.0+ OR GPL-1.0") == "GPL-1.0-or-later OR GPL-1.0-only");
}

TEST_CASE ("Lint::get_recommended_version_scheme", "[portlint]")
{
    REQUIRE(get_recommended_version_scheme("1.0.0", VersionScheme::String) == VersionScheme::Relaxed);
    REQUIRE(get_recommended_version_scheme("2020-01-01", VersionScheme::String) == VersionScheme::Date);
    REQUIRE(get_recommended_version_scheme("latest", VersionScheme::String) == VersionScheme::String);
}
