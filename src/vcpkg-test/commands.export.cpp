#include <catch2/catch.hpp>

#include <vcpkg/export.ifw.h>
#include <vcpkg/export.prefab.h>

using namespace vcpkg;

TEST_CASE ("safe_rich_from_plain_text", "[export]")
{
    CHECK(IFW::safe_rich_from_plain_text("&") == "&amp;");
    CHECK(IFW::safe_rich_from_plain_text("&asdf") == "&amp;asdf");
    CHECK(IFW::safe_rich_from_plain_text("&#123") == "&amp;#123");
    CHECK(IFW::safe_rich_from_plain_text("&#x1AfC") == "&amp;#x1AfC");

    CHECK(IFW::safe_rich_from_plain_text("&;") == "&amp;;");
    CHECK(IFW::safe_rich_from_plain_text("&#;") == "&amp;#;");
    CHECK(IFW::safe_rich_from_plain_text("&#x;") == "&amp;#x;");

    CHECK(IFW::safe_rich_from_plain_text("&asdf ;") == "&amp;asdf ;");
    CHECK(IFW::safe_rich_from_plain_text("&#123a;") == "&amp;#123a;");
    CHECK(IFW::safe_rich_from_plain_text("&#x1AfCx;") == "&amp;#x1AfCx;");
    CHECK(IFW::safe_rich_from_plain_text("&#X123;") == "&amp;#X123;");

    CHECK(IFW::safe_rich_from_plain_text("&asdf;") == "&asdf;");
    CHECK(IFW::safe_rich_from_plain_text("&asdf_asdf123;") == "&asdf_asdf123;");
    CHECK(IFW::safe_rich_from_plain_text("&#123;") == "&#123;");
    CHECK(IFW::safe_rich_from_plain_text("&#x1AfC;") == "&#x1AfC;");
}

TEST_CASE ("find_ndk_version", "[export]")
{
    auto result = Prefab::find_ndk_version(R"(
Pkg.Desc = Android NDK
Pkg.Revision = 23.1.7779620
)");
    REQUIRE(result.has_value());
    CHECK(*result.get() == "23.1.7779620");

    result = Prefab::find_ndk_version(R"(
Pkg.Desc = Android NDK
Pkg.Revision = 23.1.7779620
Pkg.Blah = doopadoopa
Pkg.Revision = foobar
)");
    REQUIRE(result.has_value());
    CHECK(*result.get() == "23.1.7779620");

    result = Prefab::find_ndk_version(R"(
Pkg.Desc = Android NDK
Pkg.Revision = 1.2.3.4.5
)");
    REQUIRE(result.has_value());
    CHECK(*result.get() == "1.2.3.4.5");

    result = Prefab::find_ndk_version(R"(
Pkg.Revision = 1.2
)");
    REQUIRE(result.has_value());
    CHECK(*result.get() == "1.2");

    result = Prefab::find_ndk_version(R"(
Pkg.Revision `=
Pkg.Revision = 1.2.3
)");
    REQUIRE(result.has_value());
    CHECK(*result.get() == "1.2.3");

    result = Prefab::find_ndk_version(R"(
Pkg.Revision = foobar
Pkg.Revision = 1.2.3
)");
    REQUIRE(result.has_value());
    CHECK(*result.get() == "1.2.3");

    result = Prefab::find_ndk_version(R"(
Pkg.Desc = Android NDK
)");
    CHECK_FALSE(result.has_value());

    result = Prefab::find_ndk_version(R"(
Pkg.Desc = Android NDK
Pkg.Revision `=
)");
    CHECK_FALSE(result.has_value());

    result = Prefab::find_ndk_version(R"(
Pkg.Desc = Android NDK
Pkg.Revision = foobar
)");
    CHECK_FALSE(result.has_value());
}

TEST_CASE ("Prefab::to_version", "[export]")
{
    auto result = Prefab::to_version("1.2.3");
    REQUIRE(result.has_value());
    CHECK(*result.get() == Prefab::NdkVersion{1, 2, 3});

    result = Prefab::to_version("20.180.2134324");
    REQUIRE(result.has_value());
    CHECK(*result.get() == Prefab::NdkVersion{20, 180, 2134324});

    result = Prefab::to_version("1.2.3 ");
    CHECK_FALSE(result.has_value());

    result = Prefab::to_version(" 1.2.3");
    CHECK_FALSE(result.has_value());

    result = Prefab::to_version("1.2.3.4");
    CHECK_FALSE(result.has_value());

    result = Prefab::to_version("1.2");
    CHECK_FALSE(result.has_value());

    result = Prefab::to_version("100000000000.2.3");
    CHECK_FALSE(result.has_value());
}
