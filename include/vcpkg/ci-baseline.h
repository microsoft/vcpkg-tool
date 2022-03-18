#pragma once
#include <vcpkg/fwd/ci-baseline.h>

#include <vcpkg/base/expected.h>
#include <vcpkg/base/sortedvector.h>
#include <vcpkg/base/view.h>

#include <vcpkg/packagespec.h>
#include <vcpkg/triplet.h>

#include <initializer_list>
#include <string>
#include <vector>

namespace vcpkg
{
    struct CiBaselineLine
    {
        std::string port_name;
        Triplet triplet;
        CiBaselineState state;
    };

    struct TripletExclusions
    {
        Triplet triplet;
        SortedVector<std::string> exclusions;

        TripletExclusions(const Triplet& triplet);
        TripletExclusions(const Triplet& triplet, SortedVector<std::string>&& exclusions);
    };

    struct ExclusionsMap
    {
        std::vector<TripletExclusions> triplets;

        void insert(Triplet triplet);
        void insert(Triplet triplet, SortedVector<std::string>&& exclusions);
    };

    struct ExclusionPredicate
    {
        const ExclusionsMap* data;

        bool operator()(const PackageSpec& spec) const;
    };

    std::vector<CiBaselineLine> parse_ci_baseline(View<std::string> lines);

    SortedVector<PackageSpec> parse_and_apply_ci_baseline(View<std::string> lines, ExclusionsMap& exclusions_map);
}
