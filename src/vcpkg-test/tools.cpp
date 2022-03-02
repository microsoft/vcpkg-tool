#include <catch2/catch.hpp>

#include <vcpkg/tools.h>
#include <vcpkg/versions.h>

using namespace vcpkg;

TEST_CASE ("parse git version", "[tools]")
{
    REQUIRE(parse_git_version("git version 2.17.1.windows.2\n").value_or_exit(VCPKG_LINE_INFO) ==
            DotVersion::try_parse_relaxed("2.17.1.2").value_or_exit(VCPKG_LINE_INFO));
    REQUIRE(parse_git_version("git version 2.17.1.2\n").value_or_exit(VCPKG_LINE_INFO) ==
            DotVersion::try_parse_relaxed("2.17.1.2").value_or_exit(VCPKG_LINE_INFO));
    REQUIRE(parse_git_version("git version 2.17.1.2").value_or_exit(VCPKG_LINE_INFO) ==
            DotVersion::try_parse_relaxed("2.17.1.2").value_or_exit(VCPKG_LINE_INFO));

    REQUIRE(!parse_git_version("2.17.1.2").has_value());
}
