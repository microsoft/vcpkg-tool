#pragma once
#include <vcpkg/fwd/build.h>
#include <vcpkg/fwd/ci-baseline.h>

#include <vcpkg/base/expected.h>
#include <vcpkg/base/sortedvector.h>
#include <vcpkg/base/span.h>

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

        friend bool operator==(const CiBaselineLine& lhs, const CiBaselineLine& rhs)
        {
            return lhs.port_name == rhs.port_name && lhs.triplet == rhs.triplet && lhs.state == rhs.state;
        }

        friend bool operator!=(const CiBaselineLine& lhs, const CiBaselineLine& rhs) { return !(lhs == rhs); }
    };

    struct TripletSkips
    {
        Triplet triplet;
        SortedVector<std::string> skips;

        TripletSkips(const Triplet& triplet);
        TripletSkips(const Triplet& triplet, SortedVector<std::string>&& skips);
    };

    struct SkipsMap
    {
        std::vector<TripletSkips> triplets;

        void insert(Triplet triplet, SortedVector<std::string>&& skips);
        bool is_skipped(const PackageSpec& spec) const;
    };

    std::vector<CiBaselineLine> parse_ci_baseline(StringView text, StringView origin, ParseMessages& messages);

    struct CiBaselineData
    {
        SortedVector<PackageSpec> expected_failures;
        SortedVector<PackageSpec> required_success;
    };

    CiBaselineData parse_and_apply_ci_baseline(View<CiBaselineLine> lines,
                                               SkipsMap& skips_map,
                                               SkipFailures skip_failures);

    LocalizedString format_ci_result(const PackageSpec& spec,
                                     BuildResult result,
                                     const CiBaselineData& cidata,
                                     const std::string* cifile,
                                     bool allow_unexpected_passing);
}
