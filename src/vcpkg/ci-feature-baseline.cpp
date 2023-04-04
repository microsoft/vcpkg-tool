#include <vcpkg/base/parse.h>
#include <vcpkg/base/util.h>

#include <vcpkg/build.h>
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
        }
        Checks::unreachable(VCPKG_LINE_INFO);
    }

}
