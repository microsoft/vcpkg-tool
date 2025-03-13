#include <vcpkg-test/util.h>

#include <vcpkg/commands.build.h>

using namespace vcpkg;

TEST_CASE ("PackagesDirAssigner_generate", "[build]")
{
    Path prefix{"example_prefix"};
    auto triplet = Triplet::from_canonical_name("x86-windows");
    PackagesDirAssigner uut{prefix};
    REQUIRE(uut.generate(PackageSpec{"zlib", triplet}) == prefix / "zlib_x86-windows");
    REQUIRE(uut.generate(PackageSpec{"zlib", triplet}) == prefix / "zlib_x86-windows_1");
    REQUIRE(uut.generate(PackageSpec{"zlib", triplet}) == prefix / "zlib_x86-windows_2");

    REQUIRE(uut.generate(PackageSpec{"other", triplet}) == prefix / "other_x86-windows");
    REQUIRE(uut.generate(PackageSpec{"other", triplet}) == prefix / "other_x86-windows_1");
    REQUIRE(uut.generate(PackageSpec{"other", triplet}) == prefix / "other_x86-windows_2");

    REQUIRE(uut.generate(PackageSpec{"a", triplet}) == prefix / "a_x86-windows");
    REQUIRE(uut.generate(PackageSpec{"b", triplet}) == prefix / "b_x86-windows");
    REQUIRE(uut.generate(PackageSpec{"a", triplet}) == prefix / "a_x86-windows_1");
    REQUIRE(uut.generate(PackageSpec{"b", triplet}) == prefix / "b_x86-windows_1");
    REQUIRE(uut.generate(PackageSpec{"b", triplet}) == prefix / "b_x86-windows_2");
    REQUIRE(uut.generate(PackageSpec{"a", triplet}) == prefix / "a_x86-windows_2");
}

TEST_CASE ("is_package_dir_match", "[build]")
{
    REQUIRE(is_package_dir_match("", ""));
    REQUIRE(is_package_dir_match("abc", "abc"));
    REQUIRE(is_package_dir_match("abc_1", "abc"));
    REQUIRE(is_package_dir_match("abc_123", "abc"));
    REQUIRE(is_package_dir_match("my_package", "my_package"));
    REQUIRE(is_package_dir_match("my_package_1", "my_package"));
    REQUIRE(is_package_dir_match("my_package_42", "my_package"));

    REQUIRE(!is_package_dir_match("", "abc"));
    REQUIRE(!is_package_dir_match("ab", "abc"));
    REQUIRE(!is_package_dir_match("abc_", "abc"));
    REQUIRE(!is_package_dir_match("abc_123x", "abc"));
    REQUIRE(!is_package_dir_match("my_package_", "my_package"));
    REQUIRE(!is_package_dir_match("my_package_a1", "my_package"));
    REQUIRE(!is_package_dir_match("abc123", "abc"));
    REQUIRE(!is_package_dir_match("non_empty", ""));
    REQUIRE(!is_package_dir_match("anotherpackage_123", "another"));
}
