#include <vcpkg/base/cmd-parser.h>
#include <vcpkg/base/strings.h>

#include <stdint.h>

#include <algorithm>

namespace
{
    using namespace vcpkg;

    void help_table_newline_indent(std::string& target)
    {
        target.push_back('\n');
        target.append(34, ' ');
    }

    static constexpr ptrdiff_t S_MAX_LINE_LENGTH = 100;

    void insert_lowercase_strings(std::vector<std::string>& target, const std::vector<std::string>& source)
    {
        target.reserve(source.size());
        for (const std::string& value : source)
        {
            target.emplace_back(Strings::ascii_to_lowercase(value));
        }
    }

    std::string switch_name_to_display(vcpkg::StringView switch_name, StabilityTag stability)
    {
        std::string result{"--"};
        if (stability == StabilityTag::Experimental)
        {
            result.append("x-");
        }

        result.append(switch_name.data(), switch_name.size());
        return result;
    }

    std::string option_name_to_display(vcpkg::StringView switch_name, StabilityTag stability)
    {
        auto result = switch_name_to_display(switch_name, stability);
        result.append("=...");
        return result;
    }

    bool try_parse_switch(vcpkg::StringView target, vcpkg::StringView switch_name, StabilityTag stability, bool& value)
    {
        auto first = target.data();
        const auto last = first + target.size();
        if (first == last || *first != '-')
        {
            return false;
        }

        ++first;
        if (first == last || *first != '-')
        {
            return false;
        }

        ++first;

        const bool saw_no = last - first >= 3 && first[0] == 'n' && first[1] == 'o' && first[2] == '-';
        if (saw_no)
        {
            first += 3;
        }

        const bool saw_z = last - first >= 2 && first[0] == 'z' && first[1] == '-';
        if (saw_z)
        {
            first += 2;
        }

        const bool saw_x = last - first >= 2 && first[0] == 'x' && first[1] == '-';
        if (saw_x)
        {
            first += 2;
        }

        switch (stability)
        {
            case StabilityTag::Standard:
                if (saw_z)
                {
                    return false;
                }

                break;
            case StabilityTag::Experimental:
                if (!saw_x || saw_z)
                {
                    return false;
                }

                break;
            case StabilityTag::ImplementationDetail:
                if (saw_x || !saw_z)
                {
                    return false;
                }

                break;
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }

        const bool saw_name = std::equal(first, last, switch_name.begin(), switch_name.end());
        if (!saw_name)
        {
            return false;
        }

        value = !saw_no;
        return true;
    }

    enum class TryParseOptionResult
    {
        NoMatch,
        ValueSet,
        ValueIsNextParameter
    };

    TryParseOptionResult try_parse_option(vcpkg::StringView target_lowercase,
                                          vcpkg::StringView target,
                                          vcpkg::StringView option_name,
                                          StabilityTag stability,
                                          std::string& value)
    {
        auto first = target_lowercase.data();
        const auto last = first + target_lowercase.size();
        if (first == last || *first != '-')
        {
            return TryParseOptionResult::NoMatch;
        }

        ++first;
        if (first == last || *first != '-')
        {
            return TryParseOptionResult::NoMatch;
        }

        ++first;

        const bool saw_z = last - first >= 2 && first[0] == 'z' && first[1] == '-';
        if (saw_z)
        {
            first += 2;
        }

        const bool saw_x = last - first >= 2 && first[0] == 'x' && first[1] == '-';
        if (saw_x)
        {
            first += 2;
        }

        switch (stability)
        {
            case StabilityTag::Standard:
                if (saw_z)
                {
                    return TryParseOptionResult::NoMatch;
                }

                break;
            case StabilityTag::Experimental:
                if (!saw_x || saw_z)
                {
                    return TryParseOptionResult::NoMatch;
                }

                break;
            case StabilityTag::ImplementationDetail:
                if (saw_x || !saw_z)
                {
                    return TryParseOptionResult::NoMatch;
                }

                break;
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }

        if (static_cast<size_t>(last - first) < option_name.size())
        {
            return TryParseOptionResult::NoMatch;
        }

        auto name_end = first + option_name.size();
        if (!std::equal(first, name_end, option_name.begin(), option_name.end()))
        {
            return TryParseOptionResult::NoMatch;
        }

        if (name_end == last)
        {
            return TryParseOptionResult::ValueIsNextParameter;
        }

        if (*name_end == '=')
        {
            ++name_end; // consume =
            const auto original_last = target.data() + target.size();
            const auto original_first = original_last - (last - name_end);
            value.assign(original_first, original_last);
            return TryParseOptionResult::ValueSet;
        }

        return TryParseOptionResult::NoMatch;
    }
}

namespace vcpkg
{
    void HelpTableFormatter::format(StringView col1, StringView col2)
    {
        static constexpr std::size_t initial_space = 2;
        static constexpr std::size_t col1_capacity = 31;
        static constexpr std::size_t seperating_space = 1;

        m_str.append(initial_space, ' ');
        Strings::append(m_str, col1);
        if (col1.size() > col1_capacity)
        {
            help_table_newline_indent(m_str);
        }
        else
        {
            m_str.append((col1_capacity + seperating_space) - col1.size(), ' ');
        }
        text(col2, initial_space + col1_capacity + seperating_space);

        m_str.push_back('\n');
    }

    void HelpTableFormatter::header(StringView name)
    {
        m_str.append(name.data(), name.size());
        m_str.push_back(':');
        m_str.push_back('\n');
    }

    void HelpTableFormatter::example(StringView example_text)
    {
        m_str.append(example_text.data(), example_text.size());
        m_str.push_back('\n');
    }

    void HelpTableFormatter::blank() { m_str.push_back('\n'); }

    // Note: this formatting code does not properly handle unicode, however all of our documentation strings are English
    // ASCII.
    void HelpTableFormatter::text(StringView text, int indent)
    {
        const char* line_start = text.begin();
        const char* const e = text.end();
        const char* best_break = std::find_if(line_start, e, [](char ch) { return ch == ' ' || ch == '\n'; });

        while (best_break != e)
        {
            const char* next_break = std::find_if(best_break + 1, e, [](char ch) { return ch == ' ' || ch == '\n'; });
            if (*best_break == '\n' || next_break - line_start + indent > S_MAX_LINE_LENGTH)
            {
                m_str.append(line_start, best_break);
                m_str.push_back('\n');
                line_start = best_break + 1;
                best_break = next_break;
                m_str.append(indent, ' ');
            }
            else
            {
                best_break = next_break;
            }
        }
        m_str.append(line_start, best_break);
    }

    std::vector<std::string> convert_argc_argv_to_arguments(int argc, const CommandLineCharType* const* const argv)
    {
        std::vector<std::string> result;
        // starts at 1 to skip over the program name
        for (int idx = 1; idx < argc; ++idx)
        {
            result.emplace_back(
#if defined(_WIN32)
                Strings::to_utf8(argv[idx])
#else
                argv[idx]
#endif // ^^^ !_WIN32
            );
        }

        return result;
    }

    ExpectedL<Unit> replace_response_file_parameters(std::vector<std::string>& inputs,
                                                     const ILineReader& response_file_source)
    {
        auto first = inputs.begin();
        auto last = inputs.end();
        while (first != last)
        {
            if (first->empty() || first->front() != '@')
            {
                ++first;
                continue;
            }

            const auto file_name = static_cast<StringView>(*first);
            auto maybe_response_file_lines = response_file_source.read_lines(file_name.substr(1));
            if (auto response_file_lines = maybe_response_file_lines.get())
            {
                if (response_file_lines->empty())
                {
                    first = inputs.erase(first);
                    last = inputs.end();
                    continue;
                }

                // replace the response file name with the first line, and insert the rest
                auto lines_begin = std::make_move_iterator(response_file_lines->begin());
                const auto lines_end = std::make_move_iterator(response_file_lines->end());
                *first = *lines_begin;
                ++first;
                ++lines_begin;
                if (lines_begin != lines_end)
                {
                    first = inputs.insert(first, lines_begin, lines_end);
                    first += response_file_lines->size() - 1;
                    last = inputs.end();
                }
            }
            else
            {
                return std::move(maybe_response_file_lines).error();
            }
        }

        return Unit{};
    }

    CmdParser::CmdParser()
        : argument_strings(), argument_strings_lowercase(), argument_parsed(), errors(), options_table()
    {
        options_table.header(msg::format(msgOptions));
    }

    CmdParser::CmdParser(View<std::string> inputs)
        : argument_strings(inputs.begin(), inputs.end())
        , argument_strings_lowercase()
        , argument_parsed(argument_strings.size(), false)
        , errors()
        , options_table()
    {
        insert_lowercase_strings(argument_strings_lowercase, argument_strings);
        options_table.header(msg::format(msgOptions));
    }

    CmdParser::CmdParser(std::vector<std::string>&& inputs)
        : argument_strings(std::move(inputs))
        , argument_strings_lowercase()
        , argument_parsed(argument_strings.size(), false)
        , errors()
        , options_table()
    {
        insert_lowercase_strings(argument_strings_lowercase, argument_strings);
        options_table.header(msg::format(msgOptions));
    }

    CmdParser::CmdParser(const CmdParser&) = default;
    CmdParser::CmdParser(CmdParser&&) = default;
    CmdParser& CmdParser::operator=(const CmdParser&) = default;
    CmdParser& CmdParser::operator=(CmdParser&&) = default;

    bool CmdParser::parse_switch(StringView switch_name, StabilityTag stability, bool& value)
    {
        std::size_t found = 0;
        for (std::size_t idx = 0; idx < argument_strings.size(); ++idx)
        {
            if (argument_parsed[idx] != false)
            {
                continue;
            }

            if (try_parse_switch(argument_strings_lowercase[idx], switch_name, stability, value))
            {
                if (found == 1)
                {
                    errors.emplace_back(msg::format_error(msgSwitchUsedMultipleTimes, msg::option = switch_name));
                }

                ++found;
                argument_parsed[idx] = true;
            }
        }

        return found > 0;
    }

    bool CmdParser::parse_switch(StringView switch_name, StabilityTag stability, Optional<bool>& value)
    {
        bool target;
        bool parsed = parse_switch(switch_name, stability, target);
        if (parsed)
        {
            value.emplace(target);
        }

        return parsed;
    }

    bool CmdParser::parse_switch(StringView switch_name, StabilityTag stability)
    {
        bool target = false;
        parse_switch(switch_name, stability, target);
        return target;
    }

    bool CmdParser::parse_switch(StringView switch_name,
                                 StabilityTag stability,
                                 bool& value,
                                 const LocalizedString& help_text)
    {
        Checks::check_exit(VCPKG_LINE_INFO, stability != StabilityTag::ImplementationDetail);
        options_table.format(switch_name_to_display(switch_name, stability), help_text);
        return parse_switch(switch_name, stability, value);
    }

    bool CmdParser::parse_switch(StringView switch_name,
                                 StabilityTag stability,
                                 Optional<bool>& value,
                                 const LocalizedString& help_text)
    {
        Checks::check_exit(VCPKG_LINE_INFO, stability != StabilityTag::ImplementationDetail);
        options_table.format(switch_name_to_display(switch_name, stability), help_text);
        return parse_switch(switch_name, stability, value);
    }

    bool CmdParser::parse_switch(StringView switch_name, StabilityTag stability, const LocalizedString& help_text)
    {
        Checks::check_exit(VCPKG_LINE_INFO, stability != StabilityTag::ImplementationDetail);
        options_table.format(switch_name_to_display(switch_name, stability), help_text);
        return parse_switch(switch_name, stability);
    }

    bool CmdParser::parse_option(StringView option_name, StabilityTag stability, std::string& value)
    {
        std::size_t found = 0;
        for (std::size_t idx = 0; idx < argument_strings.size(); ++idx)
        {
            if (argument_parsed[idx] != false)
            {
                continue;
            }

            auto parse_result =
                try_parse_option(argument_strings_lowercase[idx], argument_strings[idx], option_name, stability, value);
            if (parse_result == TryParseOptionResult::NoMatch)
            {
                continue;
            }

            if (found == 1)
            {
                errors.emplace_back(msg::format_error(msgOptionUsedMultipleTimes, msg::option = option_name));
            }

            if (parse_result == TryParseOptionResult::ValueIsNextParameter)
            {
                argument_parsed[idx] = true;
                if (idx + 1 == argument_strings.size() || argument_parsed[idx + 1] != false)
                {
                    errors.emplace_back(msg::format_error(msgOptionRequiresAValue, msg::option = option_name));
                    continue;
                }

                if (Strings::starts_with(argument_strings[idx + 1], "--"))
                {
                    errors.emplace_back(msg::format_error(msgOptionRequiresANonDashesValue,
                                                          msg::option = option_name,
                                                          msg::actual = argument_strings[idx],
                                                          msg::value = argument_strings[idx + 1]));
                    ++idx;
                    continue;
                }

                ++found;
                argument_parsed[idx + 1] = true;
                value = argument_strings[idx + 1];
                ++idx;
            }
            else
            {
                Checks::check_exit(VCPKG_LINE_INFO, parse_result == TryParseOptionResult::ValueSet);
                ++found;
                argument_parsed[idx] = true;
            }
        }

        return found > 0;
    }

    bool CmdParser::parse_option(StringView option_name, StabilityTag stability, Optional<std::string>& value)
    {
        std::string target;
        bool parsed = parse_option(option_name, stability, target);
        if (parsed)
        {
            value.emplace(std::move(target));
        }

        return parsed;
    }

    bool CmdParser::parse_option(StringView option_name,
                                 StabilityTag stability,
                                 std::string& value,
                                 const LocalizedString& help_text)
    {
        Checks::check_exit(VCPKG_LINE_INFO, stability != StabilityTag::ImplementationDetail);
        options_table.format(option_name_to_display(option_name, stability), help_text);
        return parse_option(option_name, stability, value);
    }

    bool CmdParser::parse_option(StringView option_name,
                                 StabilityTag stability,
                                 Optional<std::string>& value,
                                 const LocalizedString& help_text)
    {
        Checks::check_exit(VCPKG_LINE_INFO, stability != StabilityTag::ImplementationDetail);
        options_table.format(option_name_to_display(option_name, stability), help_text);
        return parse_option(option_name, stability, value);
    }

    bool CmdParser::parse_multi_option(StringView option_name, StabilityTag stability, std::vector<std::string>& value)
    {
        bool found = false;
        std::string temp_entry;
        for (std::size_t idx = 0; idx < argument_strings.size(); ++idx)
        {
            if (argument_parsed[idx] != false)
            {
                continue;
            }

            auto parse_result = try_parse_option(
                argument_strings_lowercase[idx], argument_strings[idx], option_name, stability, temp_entry);
            if (parse_result == TryParseOptionResult::NoMatch)
            {
                continue;
            }

            if (parse_result == TryParseOptionResult::ValueIsNextParameter)
            {
                if (idx + 1 == argument_strings.size() || argument_parsed[idx + 1] != false)
                {
                    errors.emplace_back(msg::format_error(msgOptionRequiresAValue, msg::option = option_name));
                    continue;
                }

                argument_parsed[idx] = true;
                if (Strings::starts_with(argument_strings[idx + 1], "--"))
                {
                    errors.emplace_back(msg::format_error(msgOptionRequiresANonDashesValue,
                                                          msg::option = option_name,
                                                          msg::actual = argument_strings[idx],
                                                          msg::value = argument_strings[idx + 1]));
                    ++idx;
                    continue;
                }

                if (!found)
                {
                    value.clear();
                    found = true;
                }

                value.emplace_back(argument_strings[idx + 1]);
                argument_parsed[idx + 1] = true;
                ++idx;
            }
            else
            {
                Checks::check_exit(VCPKG_LINE_INFO, parse_result == TryParseOptionResult::ValueSet);
                if (!found)
                {
                    value.clear();
                    found = true;
                }

                value.emplace_back(std::move(temp_entry));
                temp_entry.clear();
                argument_parsed[idx] = true;
            }
        }

        return found;
    }

    bool CmdParser::parse_multi_option(StringView option_name,
                                       StabilityTag stability,
                                       Optional<std::vector<std::string>>& value)
    {
        std::vector<std::string> target;
        bool parsed = parse_multi_option(option_name, stability, target);
        if (parsed)
        {
            value.emplace(std::move(target));
        }

        return parsed;
    }

    bool CmdParser::parse_multi_option(StringView option_name,
                                       StabilityTag stability,
                                       std::vector<std::string>& value,
                                       const LocalizedString& help_text)
    {
        Checks::check_exit(VCPKG_LINE_INFO, stability != StabilityTag::ImplementationDetail);
        options_table.format(option_name_to_display(option_name, stability), help_text);
        return parse_multi_option(option_name, stability, value);
    }

    bool CmdParser::parse_multi_option(StringView option_name,
                                       StabilityTag stability,
                                       Optional<std::vector<std::string>>& value,
                                       const LocalizedString& help_text)
    {
        Checks::check_exit(VCPKG_LINE_INFO, stability != StabilityTag::ImplementationDetail);
        options_table.format(option_name_to_display(option_name, stability), help_text);
        return parse_multi_option(option_name, stability, value);
    }

    Optional<std::string> CmdParser::extract_first_command_like_arg_lowercase()
    {
        for (std::size_t idx = 0; idx < argument_strings.size(); ++idx)
        {
            if (argument_parsed[idx] == false)
            {
                const auto& this_arg = argument_strings_lowercase[idx];
                if (this_arg == "--version")
                {
                    argument_parsed[idx] = true;
                    return "version";
                }

                if (!Strings::starts_with(this_arg, "--"))
                {
                    argument_parsed[idx] = true;
                    return this_arg;
                }
            }
        }

        return nullopt;
    }

    std::vector<std::string> CmdParser::get_remaining_args() const
    {
        std::vector<std::string> results;
        results.reserve(std::count(argument_parsed.begin(), argument_parsed.end(), static_cast<char>(false)));
        for (std::size_t idx = 0; idx < argument_strings.size(); ++idx)
        {
            if (argument_parsed[idx] == false)
            {
                results.emplace_back(argument_strings[idx]);
            }
        }

        return results;
    }

    void CmdParser::add_unexpected_argument_errors_after(size_t idx)
    {
        for (; idx < argument_parsed.size(); ++idx)
        {
            if (!argument_parsed[idx])
            {
                argument_parsed[idx] = true;
                add_unexpected_argument_error(argument_strings[idx]);
            }
        }
    }

    void CmdParser::add_unexpected_argument_errors() { add_unexpected_argument_errors_after(0); }

    bool CmdParser::add_unexpected_switch_errors()
    {
        bool errors = false;
        for (size_t idx = 0; idx < argument_parsed.size(); ++idx)
        {
            if (!argument_parsed[idx] && Strings::starts_with(argument_strings[idx], "--"))
            {
                errors = true;
                argument_parsed[idx] = true;
                add_unexpected_switch_error(argument_strings[idx]);
            }
        }

        return errors;
    }

    void CmdParser::add_unexpected_argument_error(const std::string& unrecognized)
    {
        errors.emplace_back(msg::format_error(msgUnexpectedArgument, msg::option = unrecognized));
    }

    void CmdParser::add_unexpected_switch_error(const std::string& unrecognized)
    {
        if (Strings::contains(unrecognized, '='))
        {
            errors.emplace_back(msg::format_error(msgUnexpectedOption, msg::option = unrecognized));
        }
        else
        {
            errors.emplace_back(msg::format_error(msgUnexpectedSwitch, msg::option = unrecognized));
        }
    }

    bool CmdParser::consume_remaining_args_impl(std::vector<std::string>& results)
    {
        bool error = false;
        results.reserve(std::count(argument_parsed.begin(), argument_parsed.end(), static_cast<char>(false)));
        for (std::size_t idx = 0; idx < argument_parsed.size(); ++idx)
        {
            if (argument_parsed[idx] == false)
            {
                argument_parsed[idx] = true;
                if (Strings::starts_with(argument_strings[idx], "--"))
                {
                    error = true;
                    results.clear();
                    add_unexpected_switch_error(argument_strings[idx]);
                }
                else if (!error)
                {
                    results.emplace_back(argument_strings[idx]);
                }
            }
        }

        return error;
    }

    void delistify_conjoined_multivalue(std::vector<std::string>& target)
    {
        auto first = target.begin();
        while (first != target.end())
        {
            if (Strings::contains(*first, ','))
            {
                auto as_split = Strings::split(*first, ',');
                switch (as_split.size())
                {
                    case 0: first = target.erase(first); break;
                    case 1:
                        *first = std::move(as_split[0]);
                        ++first;
                        break;
                    default:
                        *first = std::move(as_split[0]);
                        ++first;
                        first = target.insert(first,
                                              std::make_move_iterator(as_split.begin() + 1),
                                              std::make_move_iterator(as_split.end()));
                        first += as_split.size() - 1;
                }
            }
            else
            {
                ++first;
            }
        }
    }

    void CmdParser::enforce_no_remaining_args(StringView command_name)
    {
        (void)add_unexpected_switch_errors();
        for (std::size_t idx = 0; idx < argument_parsed.size(); ++idx)
        {
            if (argument_parsed[idx] == false)
            {
                errors.emplace_back(msg::format_error(msgNonZeroRemainingArgs, (msg::command_name = command_name)));
                add_unexpected_argument_errors_after(idx);
                return;
            }
        }
    }

    std::string CmdParser::consume_only_remaining_arg(StringView command_name)
    {
        const auto error = add_unexpected_switch_errors();
        std::size_t idx = 0;
        std::size_t selected;
        for (;; ++idx)
        {
            if (idx >= argument_parsed.size())
            {
                errors.emplace_back(msg::format_error(msgNonOneRemainingArgs, (msg::command_name = command_name)));
                return std::string{};
            }

            if (argument_parsed[idx] == false)
            {
                argument_parsed[idx] = true;
                selected = idx;
                break;
            }
        }

        while (++idx < argument_parsed.size())
        {
            if (argument_parsed[idx] == false)
            {
                errors.emplace_back(msg::format_error(msgNonOneRemainingArgs, (msg::command_name = command_name)));
                add_unexpected_argument_errors_after(idx);
                return std::string{};
            }
        }

        if (error)
        {
            return std::string{};
        }

        return argument_strings[selected];
    }

    Optional<std::string> CmdParser::consume_only_remaining_arg_optional(StringView command_name)
    {
        const auto error = add_unexpected_switch_errors();
        std::size_t idx = 0;
        std::size_t selected;
        for (;; ++idx)
        {
            if (idx >= argument_parsed.size())
            {
                return nullopt;
            }

            if (argument_parsed[idx] == false)
            {
                argument_parsed[idx] = true;
                selected = idx;
                break;
            }
        }

        while (++idx < argument_parsed.size())
        {
            if (argument_parsed[idx] == false)
            {
                errors.emplace_back(
                    msg::format_error(msgNonZeroOrOneRemainingArgs, (msg::command_name = command_name)));
                add_unexpected_argument_errors_after(idx);
                return nullopt;
            }
        }

        if (error)
        {
            return nullopt;
        }

        return argument_strings[selected];
    }

    std::vector<std::string> CmdParser::consume_remaining_args()
    {
        std::vector<std::string> results;
        (void)consume_remaining_args_impl(results);
        return results;
    }

    std::vector<std::string> CmdParser::consume_remaining_args(StringView command_name, std::size_t arity)
    {
        Checks::check_exit(VCPKG_LINE_INFO, arity != 0); // use enforce_no_remaining_args instead
        Checks::check_exit(VCPKG_LINE_INFO, arity != 1); // use consume_only_remaining_arg instead
        std::vector<std::string> results;
        if (consume_remaining_args_impl(results))
        {
            return results;
        }

        if (results.size() != arity)
        {
            errors.emplace_back(msg::format_error(msgNonExactlyArgs,
                                                  msg::command_name = command_name,
                                                  msg::expected = arity,
                                                  msg::actual = results.size()));
            for (std::size_t idx = arity; idx < results.size(); ++idx)
            {
                errors.emplace_back(msg::format_error(msgUnexpectedArgument, msg::option = results[idx]));
            }

            results.clear();
        }

        return results;
    }

    std::vector<std::string> CmdParser::consume_remaining_args(StringView command_name,
                                                               std::size_t min_arity,
                                                               std::size_t max_arity)
    {
        Checks::check_exit(VCPKG_LINE_INFO, min_arity < max_arity); // if == use single parameter overload instead
        Checks::check_exit(VCPKG_LINE_INFO, max_arity != 0);        // use enforce_no_remaining_args instead
        Checks::check_exit(VCPKG_LINE_INFO, max_arity != 1);        // use consume_only_remaining_arg instead
        std::vector<std::string> results;
        if (consume_remaining_args_impl(results))
        {
            return results;
        }

        if (max_arity < results.size() || results.size() < min_arity)
        {
            errors.emplace_back(msg::format_error(msgNonRangeArgs,
                                                  msg::command_name = command_name,
                                                  msg::lower = min_arity,
                                                  msg::upper = max_arity,
                                                  msg::actual = results.size()));
            for (std::size_t idx = max_arity; idx < results.size(); ++idx)
            {
                errors.emplace_back(msg::format_error(msgUnexpectedArgument, msg::option = results[idx]));
            }

            results.clear();
        }

        return results;
    }

    LocalizedString CmdParser::get_options_table() const { return LocalizedString::from_raw(options_table.m_str); }

    LocalizedString CmdParser::get_full_help_text(const LocalizedString& help_text) const
    {
        auto result = help_text;
        result.append(get_options_table());
        return result;
    }

    void CmdParser::exit_with_errors(const LocalizedString& help_text)
    {
        if (errors.empty())
        {
            return;
        }

        for (auto&& error : errors)
        {
            msg::write_unlocalized_text_to_stdout(Color::error, error.append_raw("\n"));
        }

        msg::write_unlocalized_text_to_stdout(Color::none, get_full_help_text(help_text));
        Checks::exit_with_code(VCPKG_LINE_INFO, 1);
    }
}
