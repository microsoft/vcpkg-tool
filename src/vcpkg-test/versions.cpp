#include <vcpkg-test/util.h>

#include <vcpkg/versions.h>

#include <string>

using namespace vcpkg;

TEST_CASE ("parse version", "[versions]")
{
    CHECK(Version::parse("").value_or_exit(VCPKG_LINE_INFO) == Version{});
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

TEST_CASE ("sanitize", "[versions]")
{
    std::string content;
    sanitize_version_string(content);
    CHECK(content.empty());

    content = "some version text";
    sanitize_version_string(content);
    CHECK(content == "some version text");

    content = "some version with missing number port version#";
    sanitize_version_string(content);
    CHECK(content == "some version with missing number port version");

    content = "some version with port version#1";
    sanitize_version_string(content);
    CHECK(content == "some version with port version#1");

    content = "some version with bad version # hashes";
    sanitize_version_string(content);
    CHECK(content == "some version with bad version  hashes");

    content = "some version with bad version # hashes#1";
    sanitize_version_string(content);
    CHECK(content == "some version with bad version  hashes#1");

    content = "1";
    sanitize_version_string(content);
    CHECK(content == "1");

    content = "1234";
    sanitize_version_string(content);
    CHECK(content == "1234");

    content = "#1234";
    sanitize_version_string(content);
    CHECK(content == "1234");

    content = "#1234#1234";
    sanitize_version_string(content);
    CHECK(content == "1234#1234");
}
