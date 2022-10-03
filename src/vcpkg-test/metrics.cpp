#include <catch2/catch.hpp>

#include <vcpkg/metrics.h>

#include <set>

using namespace vcpkg;

template<typename MetricEntry, size_t Size>
void validate_enum_values_and_names(const std::array<MetricEntry, Size>& entries)
{
    static_assert(static_cast<size_t>(decltype(entries[0].metric)::COUNT) == Size,
                  "COUNT must be the last enum entry.");

    size_t enum_value = 0;
    std::set<StringView> used_names;
    for (auto&& m : entries)
    {
        // fails when a metric is not in the right order in the entries array
        // - check that there are no duplicate or skipped metric entries
        // - check that the order in Metrics::get_<T>_metrics() and in the <T>Metric enum is the same
        REQUIRE(static_cast<size_t>(m.metric) == enum_value);
        ++enum_value;

        // fails when there's a repeated metric name
        REQUIRE(!m.name.empty());
        auto it_names = used_names.find(m.name);
        REQUIRE(it_names == used_names.end());
        used_names.insert(m.name);
    }
}

TEST_CASE ("Check metric enum types", "[metrics]")
{
    SECTION ("define metrics")
    {
        validate_enum_values_and_names(all_define_metrics);
    }

    SECTION ("string metrics")
    {
        validate_enum_values_and_names(all_string_metrics);
    }

    SECTION ("bool metrics")
    {
        validate_enum_values_and_names(all_bool_metrics);
    }
}

TEST_CASE ("Check string metrics initialization values", "[metrics]")
{
    // check that all init values are complete
    for (auto&& string_metric : all_string_metrics)
    {
        REQUIRE(!string_metric.preregister_value.empty());
    }
}

TEST_CASE ("user config parses empty", "[metrics]")
{
    auto result = try_parse_metrics_user("");
    CHECK(result.user_id == "");
    CHECK(result.user_time == "");
    CHECK(result.user_mac == "");
    CHECK(result.last_completed_survey == "");
}

TEST_CASE ("user config parses partial", "[metrics]")
{
    auto result = try_parse_metrics_user("User-Id: hello");
    CHECK(result.user_id == "hello");
    CHECK(result.user_time == "");
    CHECK(result.user_mac == "");
    CHECK(result.last_completed_survey == "");
}

TEST_CASE ("user config parses multiple paragraphs ", "[metrics]")
{
    auto result = try_parse_metrics_user("User-Id: hello\n\n\n"
                                         "User-Since: there\n"
                                         "Mac-Hash: world\n\n\n"
                                         "Survey-Completed: survey\n");

    CHECK(result.user_id == "hello");
    CHECK(result.user_time == "there");
    CHECK(result.user_mac == "world");
    CHECK(result.last_completed_survey == "survey");
}

TEST_CASE ("user config to string", "[metrics]")
{
    MetricsUserConfig uut;
    CHECK(uut.to_string() == "User-Id: \n"
                             "User-Since: \n"
                             "Mac-Hash: \n"
                             "Survey-Completed: \n");
    uut.user_id = "alpha";
    uut.user_time = "bravo";
    uut.user_mac = "charlie";
    uut.last_completed_survey = "delta";

    CHECK(uut.to_string() == "User-Id: alpha\n"
                             "User-Since: bravo\n"
                             "Mac-Hash: charlie\n"
                             "Survey-Completed: delta\n");
}

static constexpr char example_mac_hash[] = "291b9573f5e31e8e73d6b5c7d5026fcff58606fb04f7c0ac4ed83e37a0adb999";

TEST_CASE ("user config fills in system values", "[metrics]")
{
    const std::string default_user_name = "exampleuser";
    const std::string default_time = "2022-09-20T01:16:50.0Z";
    MetricsUserConfig uut;
    uut.user_mac = example_mac_hash;

    SECTION ("blank")
    {
        uut.user_mac.clear();
        CHECK(uut.fill_in_system_values());
        CHECK(!uut.user_id.empty());
        CHECK(!uut.user_time.empty());
        CHECK(!uut.user_mac.empty());
    }

    SECTION ("user id with no time is replaced")
    {
        uut.user_id = default_user_name;
        CHECK(uut.fill_in_system_values());
        CHECK(uut.user_id != default_user_name);
        CHECK(!uut.user_time.empty());
        CHECK(uut.user_mac == example_mac_hash);
    }

    SECTION ("user time with no id is replaced")
    {
        uut.user_time = default_time;
        CHECK(uut.fill_in_system_values());
        CHECK(!uut.user_id.empty());
        CHECK(uut.user_time != default_time);
        CHECK(uut.user_mac == example_mac_hash);
    }

    SECTION ("0 mac is not replaced")
    {
        // We record 0 if the user ever disabled metrics and we don't want to replace that
        uut.user_id = default_user_name;
        uut.user_time = default_time;
        uut.user_mac = "0";
        CHECK(!uut.fill_in_system_values());
        CHECK(uut.user_id == default_user_name);
        CHECK(uut.user_time == default_time);
        CHECK(uut.user_mac == "0");
    }

    SECTION ("{} mac is not replaced")
    {
        // For a while we had a bug where we always set "{}" without attempting to get a MAC address.
        // We will attempt to get a MAC address and store a "0" if we fail.
        uut.user_id = default_user_name;
        uut.user_time = default_time;
        uut.user_mac = "{}";
        CHECK(uut.fill_in_system_values());
        CHECK(uut.user_id == default_user_name);
        CHECK(uut.user_time == default_time);
        CHECK(!uut.user_mac.empty());
    }

    CHECK(uut.user_mac != "{}");
    CHECK(uut.last_completed_survey == "");
}
