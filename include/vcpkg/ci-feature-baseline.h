#pragma once
#include <vcpkg/fwd/cmakevars.h>
#include <vcpkg/fwd/triplet.h>

#include <vcpkg/packagespec.h>

#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace vcpkg
{
    enum class CiFeatureBaselineState
    {
        Skip,
        Fail,
        Cascade,
        Pass,
    };

    struct CiFeatureBaselineEntry
    {
        CiFeatureBaselineState state = CiFeatureBaselineState::Pass;
        std::set<std::string> skip_features;
        std::set<std::string> no_separate_feature_test;
        std::set<std::string> cascade_features;
        std::vector<std::vector<std::string>> fail_configurations;
        bool will_fail(const InternalFeatureSet& internal_feature_set) const;
    };

    struct CiFeatureBaseline
    {
        std::unordered_map<std::string, CiFeatureBaselineEntry> ports;
        const CiFeatureBaselineEntry& get_port(const std::string& port_name) const;
    };

    void to_string(std::string& out, CiFeatureBaselineState state);
    std::string to_string(CiFeatureBaselineState state);

    CiFeatureBaseline parse_ci_feature_baseline(StringView text,
                                                StringView origin,
                                                ParseMessages& messages,
                                                Triplet triplet,
                                                Triplet host_triplet,
                                                CMakeVars::CMakeVarProvider& var_provider);
}
