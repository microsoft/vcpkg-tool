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
