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

TEST_CASE ("formatting of time", "[chrono]")
{
    using namespace std::literals;
    REQUIRE(ElapsedTime{100ns}.to_string() == "100 ns");
    REQUIRE(ElapsedTime{1010ns}.to_string() == "1.01 us");
    REQUIRE(ElapsedTime{1500ns}.to_string() == "1.5 us");
    REQUIRE(ElapsedTime{15010ns}.to_string() == "15 us");

    REQUIRE(ElapsedTime{100us}.to_string() == "100 us");
    REQUIRE(ElapsedTime{1010us}.to_string() == "1.01 ms");
    REQUIRE(ElapsedTime{1500us}.to_string() == "1.5 ms");
    REQUIRE(ElapsedTime{15010us}.to_string() == "15 ms");

    REQUIRE(ElapsedTime{100ms}.to_string() == "100 ms");
    REQUIRE(ElapsedTime{1010ms}.to_string() == "1 s");
    REQUIRE(ElapsedTime{1500ms}.to_string() == "1.5 s");
    REQUIRE(ElapsedTime{1501ms}.to_string() == "1.5 s");

    REQUIRE(ElapsedTime{1s}.to_string() == "1 s");
    REQUIRE(ElapsedTime{59s}.to_string() == "59 s");
    REQUIRE(ElapsedTime{61s}.to_string() == "1 min");
    REQUIRE(ElapsedTime{65s}.to_string() == "1.1 min");
    REQUIRE(ElapsedTime{90s}.to_string() == "1.5 min");
    REQUIRE(ElapsedTime{601s}.to_string() == "10 min");

    REQUIRE(ElapsedTime{10min}.to_string() == "10 min");
    REQUIRE(ElapsedTime{61min}.to_string() == "1 h");
    REQUIRE(ElapsedTime{90min}.to_string() == "1.5 h");
    REQUIRE(ElapsedTime{901min}.to_string() == "15 h");
}
