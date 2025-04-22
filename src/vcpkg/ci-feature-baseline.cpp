#include <vcpkg/base/parse.h>
#include <vcpkg/base/util.h>

#include <vcpkg/ci-feature-baseline.h>
#include <vcpkg/cmakevars.h>

using namespace vcpkg;

namespace vcpkg
{

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

        static constexpr StringLiteral FAIL = "fail";
        static constexpr StringLiteral SKIP = "skip";
        static constexpr StringLiteral CASCADE = "cascade";
        static constexpr StringLiteral PASS = "pass";
        static constexpr StringLiteral NO_TEST = "no-separate-feature-test";
        static constexpr StringLiteral OPTIONS = "options";
        static constexpr StringLiteral FEATURE_FAIL = "feature-fails";
        static constexpr StringLiteral COMBINATION_FAIL = "combination-fails";

        enum class CiFeatureBaselineParseState
        {
            Skip,
            Fail,
            Cascade,
            Pass,
            NoTest,
            Options,
            FeatureFail,
            CombinationFail,
        };

        static constexpr StringLiteral ci_feature_baseline_state_names[] = {
            FAIL, SKIP, CASCADE, PASS, NO_TEST, OPTIONS, FEATURE_FAIL, COMBINATION_FAIL};

        StringLiteral to_string_literal(CiFeatureBaselineParseState state)
        {
            auto as_int = static_cast<unsigned int>(state);
            if (as_int >= std::size(ci_feature_baseline_state_names))
            {
                Checks::unreachable(VCPKG_LINE_INFO);
            }

            return ci_feature_baseline_state_names[as_int];
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
        ParserBase parser(text, origin, {});
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
            auto maybe_spec = parse_qualified_specifier(
                parser, AllowFeatures::Yes, ParseExplicitTriplet::Allow, AllowPlatformSpec::Yes);
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

            auto cur_loc = parser.cur_loc();
            CiFeatureBaselineParseState state;
            if (parser.try_match_keyword(FAIL))
            {
                state = CiFeatureBaselineParseState::Fail;
            }
            else if (parser.try_match_keyword(SKIP))
            {
                state = CiFeatureBaselineParseState::Skip;
            }
            else if (parser.try_match_keyword(CASCADE))
            {
                state = CiFeatureBaselineParseState::Cascade;
            }
            else if (parser.try_match_keyword(NO_TEST))
            {
                state = CiFeatureBaselineParseState::NoTest;
            }
            else if (parser.try_match_keyword(OPTIONS))
            {
                state = CiFeatureBaselineParseState::Options;
            }
            else if (parser.try_match_keyword(FEATURE_FAIL))
            {
                state = CiFeatureBaselineParseState::FeatureFail;
            }
            else if (parser.try_match_keyword(COMBINATION_FAIL))
            {
                state = CiFeatureBaselineParseState::CombinationFail;
            }
            else
            {
                parser.add_error(msg::format(msgExpectedFeatureBaselineState));
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
            if (spec.features.has_value())
            {
                if (state == CiFeatureBaselineParseState::Fail)
                {
                    parser.add_error(msg::format(msgFeatureBaselineNoFeaturesForFail), cur_loc);
                    break;
                }
                if (state != CiFeatureBaselineParseState::CombinationFail &&
                    state != CiFeatureBaselineParseState::Options)
                {
                    const bool contains_core = Util::contains(*spec.features.get(), "core");
                    if (contains_core)
                    {
                        parser.add_error(msg::format(msgNoCoreFeatureAllowedInNonFailBaselineEntry,
                                                     msg::value = to_string_literal(state)),
                                         cur_loc);
                        break;
                    }
                }
            }
            else if (state == CiFeatureBaselineParseState::NoTest || state == CiFeatureBaselineParseState::Options ||
                     state == CiFeatureBaselineParseState::FeatureFail ||
                     state == CiFeatureBaselineParseState::CombinationFail)
            {
                parser.add_error(msg::format(msgFeatureBaselineExpectedFeatures, msg::value = to_string_literal(state)),
                                 cur_loc);
                break;
            }

            if (respect_entry(spec, triplet, host_triplet, var_provider))
            {
                auto& entry = result.ports[spec.name];
                if (spec.features.has_value())
                {
                    auto& features = *spec.features.get();
                    const auto error_if_already_defined = [&](const auto& set, auto state) {
                        if (auto iter = Util::find_if(
                                features, [&](const auto& feature) { return Util::Sets::contains(set, feature); });
                            iter != features.end())
                        {
                            parser.add_error(msg::format(msgFeatureBaselineEntryAlreadySpecified,
                                                         msg::feature = *iter,
                                                         msg::value = to_string_literal(state)),
                                             cur_loc);
                            return true;
                        }
                        return false;
                    };
                    if (state == CiFeatureBaselineParseState::Skip)
                    {
                        if (error_if_already_defined(entry.failing_features, CiFeatureBaselineParseState::FeatureFail))
                            break;
                        if (error_if_already_defined(entry.cascade_features, CiFeatureBaselineParseState::Cascade))
                            break;
                        entry.skip_features.insert(features.begin(), features.end());
                    }
                    else if (state == CiFeatureBaselineParseState::Cascade)
                    {
                        if (error_if_already_defined(entry.failing_features, CiFeatureBaselineParseState::FeatureFail))
                            break;
                        if (error_if_already_defined(entry.skip_features, CiFeatureBaselineParseState::Skip)) break;
                        entry.cascade_features.insert(features.begin(), features.end());
                    }
                    else if (state == CiFeatureBaselineParseState::CombinationFail)
                    {
                        if (error_if_already_defined(entry.skip_features, CiFeatureBaselineParseState::Skip)) break;
                        if (error_if_already_defined(entry.cascade_features, CiFeatureBaselineParseState::Cascade))
                            break;
                        features.emplace_back("core");
                        entry.fail_configurations.push_back(Util::sort_unique_erase(std::move(features)));
                    }
                    else if (state == CiFeatureBaselineParseState::FeatureFail)
                    {
                        if (error_if_already_defined(entry.skip_features, CiFeatureBaselineParseState::Skip)) break;
                        if (error_if_already_defined(entry.cascade_features, CiFeatureBaselineParseState::Cascade))
                            break;
                        entry.failing_features.insert(features.begin(), features.end());
                    }
                    else if (state == CiFeatureBaselineParseState::NoTest)
                    {
                        entry.no_separate_feature_test.insert(features.begin(), features.end());
                    }
                    else if (state == CiFeatureBaselineParseState::Options)
                    {
                        entry.options.push_back(features);
                    }
                }
                else
                {
                    switch (state)
                    {
                        case CiFeatureBaselineParseState::Skip: entry.state = CiFeatureBaselineState::Skip; break;
                        case CiFeatureBaselineParseState::Fail: entry.state = CiFeatureBaselineState::Fail; break;
                        case CiFeatureBaselineParseState::Cascade: entry.state = CiFeatureBaselineState::Cascade; break;
                        case CiFeatureBaselineParseState::Pass: entry.state = CiFeatureBaselineState::Pass; break;
                        case CiFeatureBaselineParseState::NoTest:
                        case CiFeatureBaselineParseState::Options:
                        case CiFeatureBaselineParseState::FeatureFail:
                        case CiFeatureBaselineParseState::CombinationFail:
                        default: Checks::unreachable(VCPKG_LINE_INFO);
                    }
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
        if (!failing_features.empty() && Util::any_of(internal_feature_set, [&](const std::string& feature) {
                return Util::Sets::contains(failing_features, feature);
            }))
        {
            return true;
        }
        if (std::is_sorted(internal_feature_set.begin(), internal_feature_set.end()))
        {
            return Util::Vectors::contains(fail_configurations, internal_feature_set);
        }
        return Util::any_of(fail_configurations, [&](const std::vector<std::string>& fail_configuration) {
            return fail_configuration.size() == internal_feature_set.size() &&
                   Util::all_of(internal_feature_set, [&](const std::string& feature) {
                       return Util::contains(fail_configuration, feature);
                   });
        });
    }

    StringLiteral to_string_literal(CiFeatureBaselineState state)
    {
        auto as_int = static_cast<unsigned int>(state);
        if (as_int > static_cast<unsigned int>(CiFeatureBaselineState::Pass))
        {
            Checks::unreachable(VCPKG_LINE_INFO);
        }

        return ci_feature_baseline_state_names[as_int];
    }
}
