#include <vcpkg/base/parse.h>
#include <vcpkg/base/util.h>

#include <vcpkg/build.h>
#include <vcpkg/ci-baseline.h>
#include <vcpkg/cmakevars.h>

#include <tuple>

using namespace vcpkg;

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

    std::vector<CiBaselineLine> parse_ci_baseline(StringView text, StringView origin, ParseMessages& messages)
    {
        std::vector<CiBaselineLine> result;
        ParserBase parser(text, origin);
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
                parser.add_error(msg::format(msgExpectedFailOrSkip));
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

    CiBaselineData parse_and_apply_ci_baseline(View<CiBaselineLine> lines,
                                               ExclusionsMap& exclusions_map,
                                               SkipFailures skip_failures)
    {
        std::vector<PackageSpec> expected_failures;
        std::vector<PackageSpec> required_success;
        std::map<Triplet, std::vector<std::string>> added_exclusions;
        for (const auto& triplet_entry : exclusions_map.triplets)
        {
            added_exclusions.emplace(
                std::piecewise_construct, std::forward_as_tuple(triplet_entry.triplet), std::tuple<>{});
        }

        for (auto& line : lines)
        {
            auto triplet_match = added_exclusions.find(line.triplet);
            if (triplet_match != added_exclusions.end())
            {
                if (line.state == CiBaselineState::Pass)
                {
                    required_success.emplace_back(line.port_name, line.triplet);
                    continue;
                }
                if (line.state == CiBaselineState::Fail)
                {
                    expected_failures.emplace_back(line.port_name, line.triplet);
                    if (skip_failures == SkipFailures::No)
                    {
                        continue;
                    }
                }

                triplet_match->second.push_back(line.port_name);
            }
        }

        for (auto& triplet_entry : exclusions_map.triplets)
        {
            triplet_entry.exclusions.append(
                SortedVector<std::string>(std::move(added_exclusions.find(triplet_entry.triplet)->second)));
        }

        return CiBaselineData{
            SortedVector<PackageSpec>(std::move(expected_failures)),
            SortedVector<PackageSpec>(std::move(required_success)),
        };
    }

    LocalizedString format_ci_result(const PackageSpec& spec,
                                     BuildResult result,
                                     const CiBaselineData& cidata,
                                     StringView cifile,
                                     bool allow_unexpected_passing)
    {
        switch (result)
        {
            case BuildResult::BUILD_FAILED:
            case BuildResult::POST_BUILD_CHECKS_FAILED:
            case BuildResult::FILE_CONFLICTS:
                if (!cidata.expected_failures.contains(spec))
                {
                    return msg::format(msgCiBaselineRegression,
                                       msg::spec = spec,
                                       msg::build_result = to_string_locale_invariant(result),
                                       msg::path = cifile);
                }
                break;
            case BuildResult::SUCCEEDED:
                if (!allow_unexpected_passing && cidata.expected_failures.contains(spec))
                {
                    return msg::format(msgCiBaselineUnexpectedPass, msg::spec = spec, msg::path = cifile);
                }
                break;
            case BuildResult::CASCADED_DUE_TO_MISSING_DEPENDENCIES:
                if (cidata.required_success.contains(spec))
                {
                    return msg::format(msgCiBaselineDisallowedCascade, msg::spec = spec, msg::path = cifile);
                }
            default: break;
        }
        return {};
    }

    namespace
    {
        bool respect_entry(const ParsedQualifiedSpecifier& entry,
                           Triplet triplet,
                           Triplet host_triplet,
                           CMakeVars::CMakeVarProvider& var_provider)
        {
            if (auto maybe_triplet = entry.triplet.get())
            {
                return *maybe_triplet == triplet;
            }
            else if (auto maybe_platform = entry.platform.get())
            {
                return maybe_platform->evaluate(
                    var_provider.get_or_load_dep_info_vars(PackageSpec{entry.name, triplet}, host_triplet));
            }
            return true;
        }
    }

    CiFeatureBaseline parse_ci_feature_baseline(StringView text,
                                                StringView origin,
                                                ParseMessages& messages,
                                                Triplet triplet,
                                                Triplet host_triplet,
                                                CMakeVars::CMakeVarProvider& var_provider)
    {
        CiFeatureBaseline result;
        ParserBase parser(text, origin);
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

            // port-name     =    (fail|skip)\b
            auto maybe_spec = parse_qualified_specifier(parser);
            if (!maybe_spec) break;
            auto& spec = maybe_spec.value_or_exit(VCPKG_LINE_INFO);
            if (spec.platform.has_value() && spec.triplet.has_value())
            {
                parser.add_error(msg::format(msgBaselineOnlyPlatformExpressionOrTriplet));
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
            static constexpr StringLiteral CASCADE = "cascade";
            static constexpr StringLiteral NO_TEST = "no-separate-feature-test";
            static constexpr CiFeatureBaselineState NO_TEST_STATE = static_cast<CiFeatureBaselineState>(100);
            CiFeatureBaselineState state;
            if (parser.try_match_keyword(FAIL))
            {
                state = CiFeatureBaselineState::Fail;
            }
            else if (parser.try_match_keyword(SKIP))
            {
                state = CiFeatureBaselineState::Skip;
            }
            else if (parser.try_match_keyword(CASCADE))
            {
                state = CiFeatureBaselineState::Cascade;
            }
            else if (parser.try_match_keyword(NO_TEST))
            {
                state = NO_TEST_STATE;
            }
            else
            {
                parser.add_error(msg::format(msgExpectedFailOrSkip));
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

            if (respect_entry(spec, triplet, host_triplet, var_provider))
            {
                if (spec.features.has_value())
                {
                    auto& features = *spec.features.get();
                    if (state == CiFeatureBaselineState::Skip)
                    {
                        result.ports[spec.name].skip_features.insert(features.begin(), features.end());
                    }
                    else if (state == CiFeatureBaselineState::Cascade)
                    {
                        result.ports[spec.name].cascade_features.insert(features.begin(), features.end());
                    }
                    else if (state == CiFeatureBaselineState::Fail)
                    {
                        features.emplace_back("core");
                        result.ports[spec.name].fail_configurations.push_back(
                            Util::sort_unique_erase(std::move(features)));
                    }
                    else if (state == NO_TEST_STATE)
                    {
                        result.ports[spec.name].no_separate_feature_test.insert(features.begin(), features.end());
                    }
                }
                else
                {
                    result.ports[spec.name].state = state;
                }
            }
        }

        // failure
        messages = std::move(parser).extract_messages();
        result.ports.clear();
        return result;
    }

    const CiFeatureBaselineEntry& CiFeatureBaseline::get_port(const std::string& port_name) const
    {
        auto iter = ports.find(port_name);
        if (iter != ports.end())
        {
            return iter->second;
        }
        static CiFeatureBaselineEntry empty_entry;
        return empty_entry;
    }

    bool CiFeatureBaselineEntry::will_fail(const InternalFeatureSet& internal_feature_set) const
    {
        return Util::Vectors::contains(fail_configurations, internal_feature_set);
    }

    std::string to_string(CiFeatureBaselineState state)
    {
        std::string s;
        to_string(s, state);
        return s;
    }

    void to_string(std::string& out, CiFeatureBaselineState state)
    {
        switch (state)
        {
            case CiFeatureBaselineState::Fail: out += "fail"; return;
            case CiFeatureBaselineState::Pass: out += "pass"; return;
            case CiFeatureBaselineState::Cascade: out += "cascade"; return;
            case CiFeatureBaselineState::Skip: out += "skip"; return;
        }
        Checks::unreachable(VCPKG_LINE_INFO);
    }

}
