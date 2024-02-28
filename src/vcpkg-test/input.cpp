#include <vcpkg-test/util.h>

#include <vcpkg/input.h>
#include <vcpkg/triplet.h>

using namespace vcpkg;
using vcpkg::Test::X64_LINUX;
using vcpkg::Test::X64_WINDOWS;

TEST_CASE ("parse_package_spec implicit triplet", "[input][parse_package_spec]")
{
    auto maybe_parsed = parse_package_spec("zlib", X64_WINDOWS);
    if (auto parsed = maybe_parsed.get())
    {
        CHECK(parsed->name() == "zlib");
        CHECK(parsed->triplet() == X64_WINDOWS);
        CHECK(parsed->dir() == "zlib_x64-windows");
        CHECK(parsed->to_string() == "zlib:x64-windows");
    }
    else
    {
        FAIL(maybe_parsed.error());
    }
}

TEST_CASE ("parse_package_spec explicit triplet", "[input][parse_package_spec]")
{
    auto maybe_parsed = parse_package_spec("zlib:x64-linux", X64_WINDOWS);
    if (auto parsed = maybe_parsed.get())
    {
        CHECK(parsed->name() == "zlib");
        CHECK(parsed->triplet() == X64_LINUX);
        CHECK(parsed->dir() == "zlib_x64-linux");
        CHECK(parsed->to_string() == "zlib:x64-linux");
    }
    else
    {
        FAIL(maybe_parsed.error());
    }
}

TEST_CASE ("parse_package_spec forbid features", "[input][parse_package_spec]")
{
    auto maybe_parsed = parse_package_spec("zlib[featurea]", X64_WINDOWS);
    if (auto parsed = maybe_parsed.get())
    {
        FAIL("features should not be accepted here");
    }
    else
    {
        REQUIRE(maybe_parsed.error() == R"(error: List of features is not allowed in this context
  on expression: zlib[featurea]
                     ^)");
    }
}

TEST_CASE ("parse_package_spec forbid platform expression", "[input][parse_package_spec]")
{
    auto maybe_parsed = parse_package_spec("zlib(windows)", X64_WINDOWS);
    if (auto parsed = maybe_parsed.get())
    {
        FAIL("platform expressions should not be accepted here");
    }
    else
    {
        REQUIRE(maybe_parsed.error() == R"(error: Platform qualifier is not allowed in this context
  on expression: zlib(windows)
                     ^)");
    }
}

TEST_CASE ("parse_package_spec forbid illegal characters", "[input][parse_package_spec]")
{
    auto maybe_parsed = parse_package_spec("zlib#notaport", X64_WINDOWS);
    if (auto parsed = maybe_parsed.get())
    {
        FAIL("# should not be accepted here");
    }
    else
    {
        REQUIRE(
            maybe_parsed.error() ==
            R"(error: expected the end of input parsing a package spec; this usually means the indicated character is not allowed to be in a package spec. Port, triplet, and feature names are all lowercase alphanumeric+hypens.
  on expression: zlib#notaport
                     ^)");
    }
}

TEST_CASE ("check_triplet validates", "[input][check_triplet]")
{
    TripletDatabase db;
    db.available_triplets.push_back(TripletFile{"x64-windows", "x64-windows.cmake"});
    auto maybe_check = check_triplet("x64-windows", db);
    REQUIRE(maybe_check.has_value());
    maybe_check = check_triplet("x86-windows", db);
    REQUIRE(!maybe_check.has_value());
    REQUIRE(maybe_check.error() == LocalizedString::from_raw(R"(error: Invalid triplet: x86-windows
Built-in Triplets:
Community Triplets:
Overlay Triplets from "x64-windows.cmake":
  x64-windows
See https://learn.microsoft.com/vcpkg/users/triplets for more information.
)"));
}

TEST_CASE ("check_and_get_package_spec validates the triplet", "[input][check_and_get_package_spec]")
{
    TripletDatabase db;
    db.available_triplets.push_back(TripletFile{"x64-windows", "x64-windows.cmake"});
    auto maybe_spec = check_and_get_package_spec("zlib:x64-windows", Triplet::from_canonical_name("x64-windows"), db);
    if (auto spec = maybe_spec.get())
    {
        REQUIRE(spec->name() == "zlib");
        REQUIRE(spec->triplet().to_string() == "x64-windows");
    }
    else
    {
        FAIL();
    }

    maybe_spec = check_and_get_package_spec("zlib:x86-windows", Triplet::from_canonical_name("x64-windows"), db);
    REQUIRE(!maybe_spec.has_value());
    REQUIRE(maybe_spec.error() == LocalizedString::from_raw(R"(error: Invalid triplet: x86-windows
Built-in Triplets:
Community Triplets:
Overlay Triplets from "x64-windows.cmake":
  x64-windows
See https://learn.microsoft.com/vcpkg/users/triplets for more information.
)"));
}

TEST_CASE ("check_and_get_package_spec forbids malformed", "[input][check_and_get_package_spec]")
{
    TripletDatabase db;
    db.available_triplets.push_back(TripletFile{"x64-windows", "x64-windows.cmake"});
    auto maybe_spec = check_and_get_package_spec("zlib:x86-windows#", Triplet::from_canonical_name("x64-windows"), db);
    REQUIRE(!maybe_spec.has_value());
    REQUIRE(
        maybe_spec.error() ==
        LocalizedString::from_raw(
            R"(error: expected the end of input parsing a package spec; this usually means the indicated character is not allowed to be in a package spec. Port, triplet, and feature names are all lowercase alphanumeric+hypens.
  on expression: zlib:x86-windows#
                                 ^)"));
}

TEST_CASE ("check_and_get_package_spec forbids features", "[input][check_and_get_package_spec]")
{
    TripletDatabase db;
    db.available_triplets.push_back(TripletFile{"x64-windows", "x64-windows.cmake"});
    auto maybe_spec =
        check_and_get_package_spec("zlib[core]:x86-windows", Triplet::from_canonical_name("x64-windows"), db);
    REQUIRE(!maybe_spec.has_value());
    REQUIRE(maybe_spec.error() == LocalizedString::from_raw(
                                      R"(error: List of features is not allowed in this context
  on expression: zlib[core]:x86-windows
                     ^)"));
}

TEST_CASE ("check_and_get_package_spec forbids platform specs", "[input][check_and_get_package_spec]")
{
    TripletDatabase db;
    db.available_triplets.push_back(TripletFile{"x64-windows", "x64-windows.cmake"});
    auto maybe_spec = check_and_get_package_spec("zlib (windows)", Triplet::from_canonical_name("x64-windows"), db);
    REQUIRE(!maybe_spec.has_value());
    REQUIRE(maybe_spec.error() == LocalizedString::from_raw(R"(error: Platform qualifier is not allowed in this context
  on expression: zlib (windows)
                      ^)"));
}



TEST_CASE ("check_and_get_full_package_spec validates the triplet", "[input][check_and_get_full_package_spec]")
{
    TripletDatabase db;
    db.available_triplets.push_back(TripletFile{"x64-windows", "x64-windows.cmake"});
    auto maybe_spec =
        check_and_get_full_package_spec("zlib[core]:x64-windows", Triplet::from_canonical_name("x64-windows"), db);
    if (auto spec = maybe_spec.get())
    {
        REQUIRE(spec->package_spec.name() == "zlib");
        REQUIRE(spec->package_spec.triplet().to_string() == "x64-windows");
        REQUIRE(spec->features == std::vector<std::string>{"core"});
    }
    else
    {
        FAIL();
    }

    maybe_spec = check_and_get_full_package_spec("zlib[core]:x86-windows", Triplet::from_canonical_name("x64-windows"), db);
    REQUIRE(!maybe_spec.has_value());
    REQUIRE(maybe_spec.error() == LocalizedString::from_raw(R"(error: Invalid triplet: x86-windows
Built-in Triplets:
Community Triplets:
Overlay Triplets from "x64-windows.cmake":
  x64-windows
See https://learn.microsoft.com/vcpkg/users/triplets for more information.
)"));
}

TEST_CASE ("check_and_get_full_package_spec forbids malformed", "[input][check_and_get_full_package_spec]")
{
    TripletDatabase db;
    db.available_triplets.push_back(TripletFile{"x64-windows", "x64-windows.cmake"});
    auto maybe_spec =
        check_and_get_full_package_spec("zlib[core]:x86-windows#", Triplet::from_canonical_name("x64-windows"), db);
    REQUIRE(!maybe_spec.has_value());
    REQUIRE(
        maybe_spec.error() ==
        LocalizedString::from_raw(
            R"(error: expected the end of input parsing a package spec; this usually means the indicated character is not allowed to be in a package spec. Port, triplet, and feature names are all lowercase alphanumeric+hypens.
  on expression: zlib[core]:x86-windows#
                                       ^)"));
}

TEST_CASE ("check_and_get_full_package_spec forbids platform specs", "[input][check_and_get_full_package_spec]")
{
    TripletDatabase db;
    db.available_triplets.push_back(TripletFile{"x64-windows", "x64-windows.cmake"});
    auto maybe_spec =
        check_and_get_full_package_spec("zlib (windows)", Triplet::from_canonical_name("x64-windows"), db);
    REQUIRE(!maybe_spec.has_value());
    REQUIRE(maybe_spec.error() == LocalizedString::from_raw(R"(error: Platform qualifier is not allowed in this context
  on expression: zlib (windows)
                      ^)"));
}
