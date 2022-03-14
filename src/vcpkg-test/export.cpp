#include <catch2/catch.hpp>

#include <vcpkg/export.ifw.h>

namespace IFW = vcpkg::Export::IFW;

TEST_CASE("safe_rich_from_plain_text", "[export]")
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
