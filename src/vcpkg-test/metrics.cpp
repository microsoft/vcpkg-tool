#include <catch2/catch.hpp>

#include <vcpkg/metrics.h>

#include <set>

using namespace vcpkg;

template<typename T>
void validate_enum_values_and_names(View<MetricEntry<T>> entries)
{
    size_t enum_value = 0;
    std::set<StringView> used_names;
    for (auto&& m : entries)
    {
        // fails when a metric is not in the right order in the entries array
        // - check that there are no duplicate metric entries
        // - check that the order in Metrics::get_<T>_metrics() and in the <T>Metric enum is the same
        REQUIRE(static_cast<size_t>(m.metric) == enum_value);
        ++enum_value;

        // fails when there's a repeated metric name
        auto it_names = used_names.find(m.name);
        REQUIRE(it_names == used_names.end());
        used_names.insert(m.name);
    }
}

TEST_CASE ("Check metric enum types", "[metrics]")
{
    SECTION ("define metrics")
    {
        auto define_metrics = Metrics::get_define_metrics();
        REQUIRE(define_metrics.size() == static_cast<size_t>(DefineMetric::COUNT));
        validate_enum_values_and_names(define_metrics);
    }

    SECTION ("string metrics")
    {
        auto string_metrics = Metrics::get_string_metrics();
        REQUIRE(string_metrics.size() == static_cast<size_t>(StringMetric::COUNT));
        validate_enum_values_and_names(string_metrics);
    }

    SECTION ("bool metrics")
    {
        auto bool_metrics = Metrics::get_bool_metrics();
        REQUIRE(bool_metrics.size() == static_cast<size_t>(BoolMetric::COUNT));
        validate_enum_values_and_names(bool_metrics);
    }
}