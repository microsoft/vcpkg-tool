#include <vcpkg/base/contractual-constants.h>
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
                return maybe_triplet->value == triplet;
            }
            else if (auto maybe_platform = entry.platform.get())
            {
                return maybe_platform->value.evaluate(
                    var_provider.get_or_load_dep_info_vars(PackageSpec{entry.name.value, triplet}, host_triplet));
            }
            return true;
        }

        static constexpr StringLiteral SKIP = "skip";
        static constexpr StringLiteral FAIL = "fail";
        static constexpr StringLiteral CASCADE = "cascade";
        static constexpr StringLiteral PASS = "pass";
        static constexpr StringLiteral NO_TEST = "no-separate-feature-test";
        static constexpr StringLiteral OPTIONS = "options";
        static constexpr StringLiteral FEATURE_FAIL = "feature-fails";
        static constexpr StringLiteral COMBINATION_FAIL = "combination-fails";

        enum class CiFeatureBaselineKeyword
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
            SKIP, FAIL, CASCADE, PASS, NO_TEST, OPTIONS, FEATURE_FAIL, COMBINATION_FAIL};

        StringLiteral to_string_literal(CiFeatureBaselineKeyword state)
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

            auto keyword_loc = parser.cur_loc();
            CiFeatureBaselineKeyword keyword;
            if (parser.try_match_keyword(FAIL))
            {
                keyword = CiFeatureBaselineKeyword::Fail;
            }
            else if (parser.try_match_keyword(SKIP))
            {
                keyword = CiFeatureBaselineKeyword::Skip;
            }
            else if (parser.try_match_keyword(CASCADE))
            {
                keyword = CiFeatureBaselineKeyword::Cascade;
            }
            else if (parser.try_match_keyword(NO_TEST))
            {
                keyword = CiFeatureBaselineKeyword::NoTest;
            }
            else if (parser.try_match_keyword(OPTIONS))
            {
                keyword = CiFeatureBaselineKeyword::Options;
            }
            else if (parser.try_match_keyword(FEATURE_FAIL))
            {
                keyword = CiFeatureBaselineKeyword::FeatureFail;
            }
            else if (parser.try_match_keyword(COMBINATION_FAIL))
            {
                keyword = CiFeatureBaselineKeyword::CombinationFail;
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
            if (auto spec_features = spec.features.get())
            {
                if (keyword == CiFeatureBaselineKeyword::Fail)
                {
                    parser.add_error(msg::format(msgFeatureBaselineNoFeaturesForFail), keyword_loc);
                    break;
                }
                if (keyword != CiFeatureBaselineKeyword::CombinationFail &&
                    keyword != CiFeatureBaselineKeyword::Options)
                {
                    auto maybe_core_feature = Util::find_if(*spec_features, [](const Located<std::string>& feature) {
                        return feature.value == FeatureNameCore;
                    });

                    if (maybe_core_feature != spec_features->end())
                    {
                        parser.add_error(msg::format(msgNoCoreFeatureAllowedInNonFailBaselineEntry,
                                                     msg::value = to_string_literal(keyword)),
                                         maybe_core_feature->loc);
                        break;
                    }
                }
            }
            else if (keyword == CiFeatureBaselineKeyword::NoTest || keyword == CiFeatureBaselineKeyword::Options ||
                     keyword == CiFeatureBaselineKeyword::FeatureFail ||
                     keyword == CiFeatureBaselineKeyword::CombinationFail)
            {
                parser.add_error(
                    msg::format(msgFeatureBaselineExpectedFeatures, msg::value = to_string_literal(keyword)),
                    keyword_loc);
                break;
            }

            if (respect_entry(spec, triplet, host_triplet, var_provider))
            {
                auto& entry = result.ports[spec.name.value];
                if (auto spec_features = spec.features.get())
                {
                    const auto error_if_already_defined =
                        [&](const std::set<Located<std::string>, LocatedStringLess>& conflict_set,
                            CiFeatureBaselineKeyword state) {
                            for (auto&& this_decl_feature : *spec_features)
                            {
                                auto conflict_decl_feature = conflict_set.find(this_decl_feature.value);
                                if (conflict_decl_feature == conflict_set.end())
                                {
                                    continue;
                                }

                                if (!parser.messages().any_errors())
                                {
                                    parser.add_error(msg::format(msgFeatureBaselineEntryAlreadySpecified,
                                                                 msg::feature = this_decl_feature.value,
                                                                 msg::value = to_string_literal(state)),
                                                     this_decl_feature.loc);
                                    parser.add_note(msg::format(msgPreviousDeclarationWasHere),
                                                    conflict_decl_feature->loc);
                                }

                                return true;
                            }
                            return false;
                        };

                    if (keyword == CiFeatureBaselineKeyword::Skip)
                    {
                        if (error_if_already_defined(entry.failing_features, CiFeatureBaselineKeyword::FeatureFail))
                            break;
                        if (error_if_already_defined(entry.cascade_features, CiFeatureBaselineKeyword::Cascade)) break;
                        entry.skip_features.insert(spec_features->begin(), spec_features->end());
                    }
                    else if (keyword == CiFeatureBaselineKeyword::Cascade)
                    {
                        if (error_if_already_defined(entry.failing_features, CiFeatureBaselineKeyword::FeatureFail))
                            break;
                        if (error_if_already_defined(entry.skip_features, CiFeatureBaselineKeyword::Skip)) break;
                        entry.cascade_features.insert(spec_features->begin(), spec_features->end());
                    }
                    else if (keyword == CiFeatureBaselineKeyword::CombinationFail)
                    {
                        if (error_if_already_defined(entry.skip_features, CiFeatureBaselineKeyword::Skip)) break;
                        if (error_if_already_defined(entry.cascade_features, CiFeatureBaselineKeyword::Cascade)) break;
                        auto failing_features = hoist_locations(std::move(*spec_features));
                        failing_features.value.emplace_back(FeatureNameCore);
                        Util::sort_unique_erase(failing_features.value);
                        entry.fail_configurations.push_back(std::move(failing_features));
                    }
                    else if (keyword == CiFeatureBaselineKeyword::FeatureFail)
                    {
                        if (error_if_already_defined(entry.skip_features, CiFeatureBaselineKeyword::Skip)) break;
                        if (error_if_already_defined(entry.cascade_features, CiFeatureBaselineKeyword::Cascade)) break;
                        entry.failing_features.insert(spec_features->begin(), spec_features->end());
                    }
                    else if (keyword == CiFeatureBaselineKeyword::NoTest)
                    {
                        entry.no_separate_feature_test.insert(spec_features->begin(), spec_features->end());
                    }
                    else if (keyword == CiFeatureBaselineKeyword::Options)
                    {
                        entry.options.push_back(hoist_locations(std::move(*spec_features)));
                    }
                }
                else
                {
                    switch (keyword)
                    {
                        case CiFeatureBaselineKeyword::Skip: entry.state = CiFeatureBaselineState::Skip; break;
                        case CiFeatureBaselineKeyword::Fail: entry.state = CiFeatureBaselineState::Fail; break;
                        case CiFeatureBaselineKeyword::Cascade: entry.state = CiFeatureBaselineState::Cascade; break;
                        case CiFeatureBaselineKeyword::Pass: entry.state = CiFeatureBaselineState::Pass; break;
                        case CiFeatureBaselineKeyword::NoTest:
                        case CiFeatureBaselineKeyword::Options:
                        case CiFeatureBaselineKeyword::FeatureFail:
                        case CiFeatureBaselineKeyword::CombinationFail:
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
            return Util::any_of(fail_configurations, [&](const Located<std::vector<std::string>>& fail_configuration) {
                return fail_configuration.value == internal_feature_set;
            });
        }

        return Util::any_of(fail_configurations, [&](const Located<std::vector<std::string>>& fail_configuration) {
            return fail_configuration.value.size() == internal_feature_set.size() &&
                   Util::all_of(internal_feature_set, [&](const std::string& feature) {
                       return Util::contains(fail_configuration.value, feature);
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
