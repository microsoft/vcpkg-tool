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
            static constexpr StringLiteral FAIL = "fail";
            static constexpr StringLiteral SKIP = "skip";
            static constexpr StringLiteral CASCADE = "cascade";
            static constexpr StringLiteral PASS = "pass";
            static constexpr StringLiteral NO_TEST = "no-separate-feature-test";
            static constexpr StringLiteral OPTIONS = "options";
            static constexpr StringLiteral FEATURE_FAIL = "feature-fails";
            static constexpr StringLiteral COMBINATION_FAIL = "combination-fails";
            static constexpr int first_free = static_cast<int>(CiFeatureBaselineState::FirstFree);
            static constexpr CiFeatureBaselineState NO_TEST_STATE = static_cast<CiFeatureBaselineState>(first_free);
            static constexpr CiFeatureBaselineState OPTIONS_STATE = static_cast<CiFeatureBaselineState>(first_free + 1);
            static constexpr CiFeatureBaselineState FEATURE_FAIL_STATE =
                static_cast<CiFeatureBaselineState>(first_free + 2);
            static constexpr CiFeatureBaselineState COMBINATION_FAIL_STATE =
                static_cast<CiFeatureBaselineState>(first_free + 3);
            const StringLiteral names[] = {FAIL, SKIP, CASCADE, PASS, NO_TEST, OPTIONS, FEATURE_FAIL, COMBINATION_FAIL};
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
            else if (parser.try_match_keyword(OPTIONS))
            {
                state = OPTIONS_STATE;
            }
            else if (parser.try_match_keyword(FEATURE_FAIL))
            {
                state = FEATURE_FAIL_STATE;
            }
            else if (parser.try_match_keyword(COMBINATION_FAIL))
            {
                state = COMBINATION_FAIL_STATE;
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
            if (spec.features.has_value())
            {
                if (state == CiFeatureBaselineState::Fail)
                {
                    parser.add_error(msg::format(msgFeatureBaselineNoFeaturesForFail), cur_loc);
                    break;
                }
                if (state != COMBINATION_FAIL_STATE && state != OPTIONS_STATE)
                {
                    const bool contains_core = Util::contains(*spec.features.get(), "core");
                    if (contains_core)
                    {
                        parser.add_error(msg::format(msgNoCoreFeatureAllowedInNonFailBaselineEntry,
                                                     msg::value = names[static_cast<int>(state)]),
                                         cur_loc);
                        break;
                    }
                }
            }
            else if (state == NO_TEST_STATE || state == OPTIONS_STATE || state == FEATURE_FAIL_STATE ||
                     state == COMBINATION_FAIL_STATE)
            {
                parser.add_error(
                    msg::format(msgFeatureBaselineExpectedFeatures, msg::value = (names[static_cast<int>(state)])),
                    cur_loc);
                break;
            }

            if (respect_entry(spec, triplet, host_triplet, var_provider))
            {
                if (spec.features.has_value())
                {
                    auto& features = *spec.features.get();
                    auto& entry = result.ports[spec.name];
                    const auto error_if_already_defined = [&](const auto& set, auto state) {
                        if (auto iter = Util::find_if(
                                features, [&](const auto& feature) { return Util::Sets::contains(set, feature); });
                            iter != features.end())
                        {
                            parser.add_error(msg::format(msgFeatureBaselineEntryAlreadySpecified,
                                                         msg::feature = *iter,
                                                         msg::value = (names[static_cast<int>(state)])),
                                             cur_loc);
                            return true;
                        }
                        return false;
                    };
                    if (state == CiFeatureBaselineState::Skip)
                    {
                        if (error_if_already_defined(entry.failing_features, FEATURE_FAIL_STATE)) break;
                        if (error_if_already_defined(entry.cascade_features, CiFeatureBaselineState::Cascade)) break;
                        entry.skip_features.insert(features.begin(), features.end());
                    }
                    else if (state == CiFeatureBaselineState::Cascade)
                    {
                        if (error_if_already_defined(entry.failing_features, FEATURE_FAIL_STATE)) break;
                        if (error_if_already_defined(entry.skip_features, CiFeatureBaselineState::Skip)) break;
                        entry.cascade_features.insert(features.begin(), features.end());
                    }
                    else if (state == COMBINATION_FAIL_STATE)
                    {
                        if (error_if_already_defined(entry.skip_features, CiFeatureBaselineState::Skip)) break;
                        if (error_if_already_defined(entry.cascade_features, CiFeatureBaselineState::Cascade)) break;
                        features.emplace_back("core");
                        entry.fail_configurations.push_back(Util::sort_unique_erase(std::move(features)));
                    }
                    else if (state == FEATURE_FAIL_STATE)
                    {
                        if (error_if_already_defined(entry.skip_features, CiFeatureBaselineState::Skip)) break;
                        if (error_if_already_defined(entry.cascade_features, CiFeatureBaselineState::Cascade)) break;
                        entry.failing_features.insert(features.begin(), features.end());
                    }
                    else if (state == NO_TEST_STATE)
                    {
                        entry.no_separate_feature_test.insert(features.begin(), features.end());
                    }
                    else if (state == OPTIONS_STATE)
                    {
                        entry.options.push_back(features);
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
        if (!failing_features.empty() && Util::any_of(internal_feature_set, [&](const auto& feature) {
                return Util::Sets::contains(failing_features, feature);
            }))
        {
            return true;
        }
        if (std::is_sorted(internal_feature_set.begin(), internal_feature_set.end()))
        {
            return Util::Vectors::contains(fail_configurations, internal_feature_set);
        }
        return Util::any_of(fail_configurations, [&](auto& fail_configuration) {
            return fail_configuration.size() == internal_feature_set.size() &&
                   Util::all_of(internal_feature_set,
                                [&](auto& feature) { return Util::contains(fail_configuration, feature); });
        });
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
            case CiFeatureBaselineState::FirstFree:;
        }
        Checks::unreachable(VCPKG_LINE_INFO);
    }

}
