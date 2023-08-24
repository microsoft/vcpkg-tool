#include <catch2/catch.hpp>

#include <vcpkg/base/json.h>

#include <vcpkg/commands.new.h>

static std::string empty_string;
static std::string example_name = "puppies";
static std::string example_version_relaxed = "1.0";
static std::string example_version_date = "2022-07-05";
static std::string example_version_string = "vista";

using namespace vcpkg;
using namespace vcpkg;

TEST_CASE ("error cases", "[new]")
{
    CHECK(build_prototype_manifest(nullptr, nullptr, false, false, false, false).error().extract_data() ==
          "error: Either specify --name and --version to produce a manifest intended for C++ libraries, or specify "
          "--application to indicate that the manifest is not intended to be used as a port.");
    CHECK(build_prototype_manifest(&empty_string, &example_version_relaxed, false, false, false, false)
              .error()
              .extract_data() == "error: --name cannot be empty.");
    CHECK(build_prototype_manifest(&example_name, &empty_string, false, false, false, false).error().extract_data() ==
          "error: --version cannot be empty.");
    CHECK(build_prototype_manifest(&example_name, &example_version_relaxed, false, true, true, false)
              .error()
              .extract_data() ==
          "error: Only one of --version-relaxed, --version-date, or --version-string may be specified.");
}

TEST_CASE ("application does not require name and version", "[new]")
{
    CHECK(build_prototype_manifest(nullptr, nullptr, true, false, false, false).value_or_exit(VCPKG_LINE_INFO) ==
          Json::Object());
}

TEST_CASE ("version examples", "[new]")
{
    SECTION ("guess version")
    {
        Json::Object expected;
        expected.insert("name", example_name);
        expected.insert("version", example_version_relaxed);
        CHECK(build_prototype_manifest(&example_name, &example_version_relaxed, false, false, false, false)
                  .value_or_exit(VCPKG_LINE_INFO) == expected);
    }
    SECTION ("guess date")
    {
        Json::Object expected;
        expected.insert("name", example_name);
        expected.insert("version-date", example_version_date);
        CHECK(build_prototype_manifest(&example_name, &example_version_date, false, false, false, false)
                  .value_or_exit(VCPKG_LINE_INFO) == expected);
    }
    SECTION ("guess string")
    {
        Json::Object expected;
        expected.insert("name", example_name);
        expected.insert("version-string", example_version_string);
        CHECK(build_prototype_manifest(&example_name, &example_version_string, false, false, false, false)
                  .value_or_exit(VCPKG_LINE_INFO) == expected);
    }
    SECTION ("force version - version")
    {
        Json::Object expected;
        expected.insert("name", example_name);
        expected.insert("version", example_version_relaxed);
        CHECK(build_prototype_manifest(&example_name, &example_version_relaxed, false, true, false, false)
                  .value_or_exit(VCPKG_LINE_INFO) == expected);
    }
    SECTION ("force version - date")
    {
        Json::Object expected;
        expected.insert("name", example_name);
        expected.insert("version", example_version_date);
        CHECK(build_prototype_manifest(&example_name, &example_version_date, false, true, false, false)
                  .value_or_exit(VCPKG_LINE_INFO) == expected);
    }
    SECTION ("force version - string")
    {
        CHECK(!build_prototype_manifest(&example_name, &example_version_string, false, true, false, false).has_value());
    }
    SECTION ("force date - version")
    {
        CHECK(
            !build_prototype_manifest(&example_name, &example_version_relaxed, false, false, true, false).has_value());
    }
    SECTION ("force date - date")
    {
        Json::Object expected;
        expected.insert("name", example_name);
        expected.insert("version-date", example_version_date);
        CHECK(build_prototype_manifest(&example_name, &example_version_date, false, false, true, false)
                  .value_or_exit(VCPKG_LINE_INFO) == expected);
    }
    SECTION ("force date - string")
    {
        CHECK(!build_prototype_manifest(&example_name, &example_version_string, false, false, true, false).has_value());
    }
    SECTION ("force string - version")
    {
        Json::Object expected;
        expected.insert("name", example_name);
        expected.insert("version-string", example_version_relaxed);
        CHECK(build_prototype_manifest(&example_name, &example_version_relaxed, false, false, false, true)
                  .value_or_exit(VCPKG_LINE_INFO) == expected);
    }
    SECTION ("force string - date")
    {
        Json::Object expected;
        expected.insert("name", example_name);
        expected.insert("version-string", example_version_date);
        CHECK(build_prototype_manifest(&example_name, &example_version_date, false, false, false, true)
                  .value_or_exit(VCPKG_LINE_INFO) == expected);
    }
    SECTION ("force string - string")
    {
        Json::Object expected;
        expected.insert("name", example_name);
        expected.insert("version-string", example_version_string);
        CHECK(build_prototype_manifest(&example_name, &example_version_string, false, false, false, true)
                  .value_or_exit(VCPKG_LINE_INFO) == expected);
    }
}
