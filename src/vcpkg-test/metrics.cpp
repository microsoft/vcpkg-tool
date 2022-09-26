#include <catch2/catch.hpp>

#include <vcpkg/metrics.h>

#include <set>

using namespace vcpkg;

template<typename T>
void validate_enum_values_and_names(View<MetricEntry<T>> entries, const size_t expected_size)
{
    REQUIRE(expected_size == entries.size());

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
        validate_enum_values_and_names(Metrics::get_define_metrics(), static_cast<size_t>(DefineMetric::COUNT));
    }

    SECTION ("string metrics")
    {
        validate_enum_values_and_names(Metrics::get_string_metrics(), static_cast<size_t>(StringMetric::COUNT));
    }

    SECTION ("bool metrics")
    {
        validate_enum_values_and_names(Metrics::get_bool_metrics(), static_cast<size_t>(BoolMetric::COUNT));
    }
}

TEST_CASE ("Check string metrics initialization values", "[metrics]")
{
    auto known_metrics = Metrics::get_string_metrics();
    auto init_values = Metrics::get_string_metrics_preregister_values();

    // check that all init values are complete and in order
    size_t enum_value = 0;
    REQUIRE(init_values.size() == known_metrics.size());
    for (auto&& init_value : init_values)
    {
        REQUIRE(enum_value == static_cast<size_t>(init_value.metric));
        ++enum_value;

        // initialization value should not be empty
        REQUIRE(!init_value.name.empty());
    }
}
