#include <vcpkg-test/util.h>

#include <vcpkg/versions.h>

using namespace vcpkg;

TEST_CASE ("parse version", "[versions]")
{
    CHECK(Version::parse("").value_or_exit(VCPKG_LINE_INFO) == Version{"", 0});
    CHECK(Version::parse("#1").value_or_exit(VCPKG_LINE_INFO) == Version{"", 1});
    CHECK(Version::parse("a#1").value_or_exit(VCPKG_LINE_INFO) == Version{"a", 1});
    CHECK(Version::parse("example").value_or_exit(VCPKG_LINE_INFO) == Version{"example", 0});
    CHECK(Version::parse("example#0").value_or_exit(VCPKG_LINE_INFO) == Version{"example", 0});
    CHECK(Version::parse("example#1").value_or_exit(VCPKG_LINE_INFO) == Version{"example", 1});
    CHECK(Version::parse("example#").error() ==
          LocalizedString::from_raw("error: port-version (after the '#') must be a non-negative integer, but was "));
    CHECK(Version::parse("example#-1").error() ==
          LocalizedString::from_raw("error: port-version (after the '#') must be a non-negative integer, but was -1"));
    CHECK(Version::parse("example#1234#hello").error() ==
          LocalizedString::from_raw(
              "error: port-version (after the '#') must be a non-negative integer, but was 1234#hello"));
}
