#include <vcpkg-test/util.h>

#include <vcpkg/versions.h>

#include <string>

using namespace vcpkg;

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
