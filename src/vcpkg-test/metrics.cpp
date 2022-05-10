#include <catch2/catch.hpp>

#include <vcpkg/metrics.h>

using namespace vcpkg;

TEST_CASE ("find_first_nonzero_mac", "[metrics]")
{
    auto result = find_first_nonzero_mac(R"()");
    CHECK_FALSE(result.has_value());

    result = find_first_nonzero_mac(R"(12-34-56-78-90-ab)");
    REQUIRE(result.has_value());
    CHECK(*result.get() == "12-34-56-78-90-ab");

    result = find_first_nonzero_mac(R"(12-34-56-78-90-AB)");
    REQUIRE(result.has_value());
    CHECK(*result.get() == "12-34-56-78-90-AB");

    result = find_first_nonzero_mac(R"(12-34-56-78-90-AB CD-EF-01-23-45-67)");
    REQUIRE(result.has_value());
    CHECK(*result.get() == "12-34-56-78-90-AB");

    result = find_first_nonzero_mac(R"(00-00-00-00-00-00 CD-EF-01-23-45-67)");
    REQUIRE(result.has_value());
    CHECK(*result.get() == "CD-EF-01-23-45-67");

    result = find_first_nonzero_mac(R"(asdfa00-00-00-00-00-00 jiojCD-EF-01-23-45-67-89)");
    REQUIRE(result.has_value());
    CHECK(*result.get() == "CD-EF-01-23-45-67");

    result = find_first_nonzero_mac(R"(afCD-EF-01-23-45-67)");
    REQUIRE(result.has_value());
    CHECK(*result.get() == "CD-EF-01-23-45-67");
}
