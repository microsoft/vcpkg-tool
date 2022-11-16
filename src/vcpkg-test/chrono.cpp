#include <catch2/catch.hpp>

#include <vcpkg/base/chrono.h>

using namespace vcpkg;

TEST_CASE ("parse time", "[chrono]")
{
    static constexpr StringLiteral timestring = "1990-02-03T04:05:06.0Z";
    auto maybe_time = CTime::parse(timestring);

    REQUIRE(maybe_time.has_value());
    REQUIRE(maybe_time.get()->to_string() == "1990-02-03T04:05:06Z"); // note dropped .0
}

TEST_CASE ("parse blank time", "[chrono]")
{
    auto maybe_time = CTime::parse("");

    REQUIRE_FALSE(maybe_time.has_value());
}

TEST_CASE ("difference of times", "[chrono]")
{
    auto maybe_time1 = CTime::parse("1990-02-03T04:05:06Z");
    auto maybe_time2 = CTime::parse("1990-02-10T04:05:06Z");

    REQUIRE(maybe_time1.has_value());
    REQUIRE(maybe_time2.has_value());

    auto delta = maybe_time2.get()->to_time_point() - maybe_time1.get()->to_time_point();

    REQUIRE(std::chrono::duration_cast<std::chrono::hours>(delta).count() == 24 * 7);
}
