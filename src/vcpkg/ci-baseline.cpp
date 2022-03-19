#include <vcpkg/base/parse.h>
#include <vcpkg/base/util.h>

#include <vcpkg/ci-baseline.h>

#include <tuple>

using namespace vcpkg;

namespace
{
    DECLARE_AND_REGISTER_MESSAGE(ExpectedPortName, (), "", "expected a port name here");
    DECLARE_AND_REGISTER_MESSAGE(ExpectedTripletName, (), "", "expected a triplet name here");
    DECLARE_AND_REGISTER_MESSAGE(ExpectedFailOrSkip, (), "", "expected 'fail' or 'skip' here");
    DECLARE_AND_REGISTER_MESSAGE(UnknownBaselineFileContent, (), "", "unrecognizable baseline entry; expected 'port:triplet=(fail|skip)'");
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
            CiBaselineState state;
            if (parser.try_match_keyword(FAIL))
            {
                state = CiBaselineState::Fail;
            }
            else if (parser.try_match_keyword(SKIP))
            {
                state = CiBaselineState::Skip;
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
            else
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

    SortedVector<PackageSpec> parse_and_apply_ci_baseline(View<CiBaselineLine> lines, ExclusionsMap& exclusions_map)
    {
        std::vector<PackageSpec> expected_failures;
        std::map<Triplet, std::vector<std::string>> added_known_fails;
        for (const auto& triplet_entry : exclusions_map.triplets)
        {
            added_known_fails.emplace(
                std::piecewise_construct, std::forward_as_tuple(triplet_entry.triplet), std::tuple<>{});
        }

        for (auto& line : lines)
        {
            auto triplet_match = added_known_fails.find(line.triplet);
            if (triplet_match != added_known_fails.end())
            {
                if (line.state == CiBaselineState::Skip)
                {
                    triplet_match->second.push_back(line.port_name);
                }
                else if (line.state == CiBaselineState::Fail)
                {
                    expected_failures.emplace_back(line.port_name, line.triplet);
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
