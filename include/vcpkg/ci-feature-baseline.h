#pragma once
#include <vcpkg/base/fwd/fmt.h>

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

    enum class CiFeatureBaselineOutcome
    {
        ImplicitPass,
        ExplicitPass,
        PortMarkedFail,
        PortMarkedCascade,
        FeatureFail,
        FeatureCascade,
        ConfigurationFail,
    };

    struct CiFeatureBaselineEntry
    {
        Optional<Located<CiFeatureBaselineState>> state;
        std::set<Located<std::string>, LocatedStringLess> skip_features;
        std::set<Located<std::string>, LocatedStringLess> no_separate_feature_test;
        std::set<Located<std::string>, LocatedStringLess> cascade_features;
        std::set<Located<std::string>, LocatedStringLess> failing_features;
        std::vector<Located<std::vector<std::string>>> fail_configurations;
        // A list of sets of features of which exactly one must be selected
        std::vector<Located<std::vector<std::string>>> options;
    };

    Located<CiFeatureBaselineOutcome> expected_outcome(const CiFeatureBaselineEntry* baseline,
                                                       const InternalFeatureSet& spec_features);

    struct CiFeatureBaseline
    {
        std::unordered_map<std::string, CiFeatureBaselineEntry> ports;
        const CiFeatureBaselineEntry* get_port(const std::string& port_name) const;
    };

    StringLiteral to_string_literal(CiFeatureBaselineState state);

    CiFeatureBaseline parse_ci_feature_baseline(StringView text,
                                                StringView origin,
                                                ParseMessages& messages,
                                                Triplet triplet,
                                                Triplet host_triplet,
                                                CMakeVars::CMakeVarProvider& var_provider);
}

VCPKG_FORMAT_WITH_TO_STRING_LITERAL_NONMEMBER(vcpkg::CiFeatureBaselineState);
