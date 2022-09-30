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

template<typename MetricEntry, size_t Size>
void validate_unique_names(std::set<StringLiteral>& unique_names, const std::array<MetricEntry, Size>& entries)
{
    for (auto&& entry : entries)
    {
        auto it = unique_names.find(entry.name);
        // fails when a metric name is repeated
        // - check that all metric names are different across all enums
        REQUIRE(it == unique_names.end());
        unique_names.insert(entry.name);
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

    SECTION ("array metrics")
    {
        validate_enum_values_and_names(all_array_metrics);
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

TEST_CASE ("Check all metric names are diferent", "[metrics]")
{
    std::set<StringLiteral> used_names;
    validate_unique_names(used_names, all_define_metrics);
    validate_unique_names(used_names, all_string_metrics);
    validate_unique_names(used_names, all_bool_metrics);
    validate_unique_names(used_names, all_array_metrics);
}
