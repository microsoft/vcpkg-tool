#include <vcpkg/base/parse.h>
#include <vcpkg/base/util.h>

#include <vcpkg/ci-baseline.h>

#include <tuple>

using namespace vcpkg;

namespace
{
    struct CiBaselineParser : ParserBase
    {
        CiBaselineParser(StringView text, StringView origin) : ParserBase(text, origin) { }
    };
}

namespace vcpkg
{
    TripletExclusions::TripletExclusions(const Triplet& triplet) : triplet(triplet), exclusions() { }

    TripletExclusions::TripletExclusions(const Triplet& triplet, SortedVector<std::string>&& exclusions)
        : triplet(triplet), exclusions(std::move(exclusions))
    {
    }

    void ExclusionsMap::insert(Triplet triplet)
    {
        for (auto& triplet_exclusions : triplets)
        {
            if (triplet_exclusions.triplet == triplet)
            {
                return;
            }
        }

        triplets.emplace_back(triplet);
    }

    void ExclusionsMap::insert(Triplet triplet, SortedVector<std::string>&& exclusions)
    {
        for (auto& triplet_exclusions : triplets)
        {
            if (triplet_exclusions.triplet == triplet)
            {
                triplet_exclusions.exclusions.append(std::move(exclusions));
                return;
            }
        }

        triplets.emplace_back(triplet, std::move(exclusions));
    }

    bool ExclusionPredicate::operator()(const PackageSpec& spec) const
    {
        for (const auto& triplet_exclusions : data->triplets)
        {
            if (triplet_exclusions.triplet == spec.triplet())
            {
                return triplet_exclusions.exclusions.contains(spec.name());
            }
        }

        return false;
    }

    std::vector<CiBaselineLine> parse_ci_baseline(View<std::string> lines)
    {
        std::vector<CiBaselineLine> result;
        for (auto& line : lines)
        {
            if (line.empty() || line[0] == '#') continue;
            CiBaselineLine parsed_line;

            auto colon_loc = line.find(':');
            Checks::check_exit(VCPKG_LINE_INFO, colon_loc != std::string::npos, "Line '%s' must contain a ':'", line);
            parsed_line.port_name = Strings::trim(StringView{line.data(), line.data() + colon_loc}).to_string();

            auto equal_loc = line.find('=', colon_loc + 1);
            Checks::check_exit(VCPKG_LINE_INFO, equal_loc != std::string::npos, "Line '%s' must contain a '='", line);
            parsed_line.triplet = Triplet::from_canonical_name(
                Strings::trim(StringView{line.data() + colon_loc + 1, line.data() + equal_loc}).to_string());

            auto baseline_value = Strings::trim(StringView{line.data() + equal_loc + 1, line.data() + line.size()});
            if (baseline_value == "fail")
            {
                parsed_line.state = CiBaselineState::Fail;
            }
            else if (baseline_value == "skip")
            {
                parsed_line.state = CiBaselineState::Skip;
            }
            else
            {
                Checks::exit_with_message(VCPKG_LINE_INFO, "Unknown value '%s'", baseline_value);
            }
        }
        return result;
    }

    SortedVector<PackageSpec> parse_and_apply_ci_baseline(View<std::string> lines, ExclusionsMap& exclusions_map)
    {
        std::vector<PackageSpec> expected_failures;
        std::map<Triplet, std::vector<std::string>> added_known_fails;
        for (const auto& triplet_entry : exclusions_map.triplets)
        {
            added_known_fails.emplace(
                std::piecewise_construct, std::forward_as_tuple(triplet_entry.triplet), std::tuple<>{});
        }

        auto baseline_lines = parse_ci_baseline(lines);
        for (auto& baseline_line : baseline_lines)
        {
            auto triplet_match = added_known_fails.find(baseline_line.triplet);
            if (triplet_match != added_known_fails.end())
            {
                if (baseline_line.state == CiBaselineState::Skip)
                {
                    triplet_match->second.push_back(std::move(baseline_line.port_name));
                }
                else if (baseline_line.state == CiBaselineState::Fail)
                {
                    expected_failures.emplace_back(std::move(baseline_line.port_name), baseline_line.triplet);
                }
            }
        }

        for (auto& triplet_entry : exclusions_map.triplets)
        {
            triplet_entry.exclusions.append(
                SortedVector<std::string>(std::move(added_known_fails.find(triplet_entry.triplet)->second)));
        }

        return SortedVector<PackageSpec>{std::move(expected_failures)};
    }
}
