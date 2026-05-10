#include <vcpkg/base/parse.h>

#include <vcpkg/ci-baseline.h>
#include <vcpkg/commands.build.h>

#include <tuple>

using namespace vcpkg;

namespace vcpkg
{
    TripletSkips::TripletSkips(const Triplet& triplet) : triplet(triplet), skips() { }

    TripletSkips::TripletSkips(const Triplet& triplet, SortedVector<std::string>&& skips)
        : triplet(triplet), skips(std::move(skips))
    {
    }

    void SkipsMap::insert(Triplet triplet, SortedVector<std::string>&& skips)
    {
        for (auto& triplet_skips : triplets)
        {
            if (triplet_skips.triplet == triplet)
            {
                triplet_skips.skips.append(std::move(skips));
                return;
            }
        }

        triplets.emplace_back(triplet, std::move(skips));
    }

    bool SkipsMap::is_skipped(const PackageSpec& spec) const
    {
        for (const auto& triplet_skips : triplets)
        {
            if (triplet_skips.triplet == spec.triplet())
            {
                return triplet_skips.skips.contains(spec.name());
            }
        }

        return false;
    }

    std::vector<CiBaselineLine> parse_ci_baseline(StringView text, StringView origin, ParseMessages& messages)
    {
        std::vector<CiBaselineLine> result;
        ParserBase parser(text, origin, {1, 1});
        for (;;)
        {
            parser.skip_whitespace();
            if (parser.at_eof())
            {
                // success
                return result;
            }

            if (parser.cur() == '#')
            {
                parser.skip_line();
                continue;
            }

            // port-name:triplet     =    (fail|skip)\b
            auto port = parser.match_while(ParserBase::is_package_name_char);
            if (port.empty())
            {
                parser.add_error(msg::format(msgExpectedPortName));
                break;
            }

            if (parser.require_character(':'))
            {
                break;
            }

            auto triplet = parser.match_while(ParserBase::is_package_name_char);
            if (triplet.empty())
            {
                parser.add_error(msg::format(msgExpectedTripletName));
                break;
            }

            parser.skip_tabs_spaces();
            if (parser.require_character('='))
            {
                break;
            }

            parser.skip_tabs_spaces();

            static constexpr StringLiteral FAIL = "fail";
            static constexpr StringLiteral SKIP = "skip";
            static constexpr StringLiteral PASS = "pass";
            CiBaselineState state;
            if (parser.try_match_keyword(FAIL))
            {
                state = CiBaselineState::Fail;
            }
            else if (parser.try_match_keyword(SKIP))
            {
                state = CiBaselineState::Skip;
            }
            else if (parser.try_match_keyword(PASS))
            {
                state = CiBaselineState::Pass;
            }
            else
            {
                parser.add_error(msg::format(msgExpectedFailSkipOrPass));
                break;
            }

            parser.skip_tabs_spaces();
            auto trailing = parser.cur();
            if (trailing == '#')
            {
                parser.skip_line();
            }
            else if (trailing == '\r' || trailing == '\n')
            {
                parser.skip_newline();
            }
            else if (trailing != Unicode::end_of_file)
            {
                parser.add_error(msg::format(msgUnknownBaselineFileContent));
                break;
            }

            result.emplace_back(
                CiBaselineLine{port.to_string(), Triplet::from_canonical_name(triplet.to_string()), state});
        }

        // failure
        messages = std::move(parser).extract_messages();
        result.clear();
        return result;
    }

    CiBaselineData parse_and_apply_ci_baseline(View<CiBaselineLine> lines, SkipsMap& skips_map)
    {
        std::vector<PackageSpec> expected_fail;
        std::vector<PackageSpec> required_pass;
        std::map<Triplet, std::vector<std::string>> added_skips;
        for (const auto& triplet_entry : skips_map.triplets)
        {
            added_skips.emplace(std::piecewise_construct, std::forward_as_tuple(triplet_entry.triplet), std::tuple<>{});
        }

        for (auto& line : lines)
        {
            auto triplet_match = added_skips.find(line.triplet);
            if (triplet_match != added_skips.end())
            {
                switch (line.state)
                {
                    case CiBaselineState::Pass: required_pass.emplace_back(line.port_name, line.triplet); break;
                    case CiBaselineState::Fail: expected_fail.emplace_back(line.port_name, line.triplet); break;
                    case CiBaselineState::Skip: triplet_match->second.push_back(line.port_name); break;
                    default: Checks::unreachable(VCPKG_LINE_INFO);
                }
            }
        }

        for (auto& triplet_entry : skips_map.triplets)
        {
            triplet_entry.skips.append(
                SortedVector<std::string>(std::move(added_skips.find(triplet_entry.triplet)->second)));
        }

        return CiBaselineData{
            SortedVector<PackageSpec>(std::move(expected_fail)),
            SortedVector<PackageSpec>(std::move(required_pass)),
        };
    }

    LocalizedString format_ci_result(const PackageSpec& spec,
                                     BuildResult result,
                                     const CiBaselineData& ci_data,
                                     const std::string* ci_file,
                                     bool allow_unexpected_passing)
    {
        switch (result)
        {
            case BuildResult::Succeeded:
            case BuildResult::Cached:
                if (!allow_unexpected_passing && ci_data.expected_fail.contains(spec))
                {
                    return msg::format(msgCiBaselineUnexpectedPass, msg::spec = spec, msg::path = *ci_file);
                }
                break;
            case BuildResult::BuildFailed:
            case BuildResult::PostBuildChecksFailed:
            case BuildResult::FileConflicts:
                if (!ci_data.expected_fail.contains(spec))
                {
                    if (ci_file)
                    {
                        return msg::format(msgCiBaselineRegression,
                                           msg::spec = spec,
                                           msg::build_result = to_string_locale_invariant(result),
                                           msg::path = *ci_file);
                    }

                    return msg::format(msgCiBaselineRegressionNoPath,
                                       msg::spec = spec,
                                       msg::build_result = to_string_locale_invariant(result));
                }
                break;
            case BuildResult::CascadedDueToSupports:
                if (ci_data.expected_fail.contains(spec))
                {
                    return msg::format(
                        msgCiBaselineUnexpectedFailCascade, msg::spec = spec, msg::triplet = spec.triplet());
                }

                [[fallthrough]];
            case BuildResult::CascadedDueToMissingDependencies:
            case BuildResult::CascadedDueToBaseline:
                if (ci_data.required_pass.contains(spec))
                {
                    return msg::format(msgCiBaselineDisallowedCascade, msg::spec = spec, msg::path = *ci_file);
                }
                break;
            case BuildResult::Unsupported:
                if (ci_data.expected_fail.contains(spec))
                {
                    return msg::format(msgCiBaselineUnexpectedFail, msg::spec = spec, msg::triplet = spec.triplet());
                }

                if (ci_data.required_pass.contains(spec))
                {
                    return msg::format(
                        msgCiBaselineUnexpectedPassUnsupported, msg::spec = spec, msg::triplet = spec.triplet());
                }
                break;
            case BuildResult::Skipped:
            case BuildResult::SkippedByParentHashes:
            case BuildResult::SkippedByDryRun:
            case BuildResult::SkippedBySkipFailures: break;
            case BuildResult::CacheMissing:
            case BuildResult::Downloaded:
            case BuildResult::Removed:
            default: Checks::unreachable(VCPKG_LINE_INFO); break;
        }
        return {};
    }
}
