#include <vcpkg/base/json.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.generate-message-map.h>

namespace vcpkg::Commands
{
    static constexpr StringLiteral OPTION_OUTPUT_COMMENTS = "output-comments";
    static constexpr StringLiteral OPTION_NO_OUTPUT_COMMENTS = "no-output-comments";

    static constexpr CommandSwitch GENERATE_MESSAGE_MAP_SWITCHES[]{
        {OPTION_OUTPUT_COMMENTS, "When generating the message map, include comments (the default)"},
        {OPTION_NO_OUTPUT_COMMENTS,
         "When generating the message map, exclude comments (useful for generating the english localization file)"},
    };

    const CommandStructure COMMAND_STRUCTURE = {
        create_example_string(R"###(x-generate-default-message-map locales/messages.json)###"),
        2,
        2,
        {GENERATE_MESSAGE_MAP_SWITCHES, {}, {}},
        nullptr,
    };

    std::vector<StringView> get_all_format_args(StringView fstring, LocalizedString& error)
    {
        error = {};
        std::vector<StringView> res;

        auto last = fstring.end();
        auto it = std::find(fstring.begin(), last, '{');
        for (; it != last; it = std::find(it, last, '{'))
        {
            // *it == `{`
            ++it;
            // *it == (first character of thing)
            if (it == last)
            {
                error = msg::format(msgAllFormatArgsUnbalancedBraces, msg::value = fstring);
                break;
            }
            else if (*it == '{')
            {
                // raw brace, continue
                ++it;
                continue;
            }

            auto close_brace = std::find(it, last, '}');
            if (close_brace == last)
            {
                error = msg::format(msgAllFormatArgsUnbalancedBraces, msg::value = fstring);
                break;
            }

            if (it == close_brace)
            {
                error = msg::format(msgAllFormatArgsRawArgument, msg::value = fstring);
                it = close_brace + 1;
                continue;
            }

            // look for `{ {}`
            auto open_brace_in_between =
                std::find(std::make_reverse_iterator(close_brace), std::make_reverse_iterator(it), '{').base();
            if (open_brace_in_between != it)
            {
                error = msg::format(msgAllFormatArgsUnbalancedBraces, msg::value = fstring);
                if (open_brace_in_between != close_brace - 1)
                {
                    res.emplace_back(open_brace_in_between, close_brace);
                }
                it = close_brace + 1;
                continue;
            }

            res.emplace_back(it, close_brace);
            it = close_brace + 1;
        }

        return res;
    }

    FormatArgMismatches get_format_arg_mismatches(StringView value, StringView comment, LocalizedString& error)
    {
        FormatArgMismatches res;
        auto comment_args = get_all_format_args(comment, error);
        // ignore error; comments are allowed to be incorrect format strings
        auto value_args = get_all_format_args(value, error);
        if (!error.data().empty())
        {
            return res;
        }

        Util::sort_unique_erase(value_args);
        Util::sort_unique_erase(comment_args);

        auto value_it = value_args.begin();
        auto comment_it = comment_args.begin();

        while (value_it != value_args.end() && comment_it != comment_args.end())
        {
            if (*value_it == *comment_it)
            {
                ++value_it;
                ++comment_it;
            }
            else if (*value_it < *comment_it)
            {
                res.arguments_without_comment.push_back(*value_it);
                ++value_it;
            }
            else
            {
                // *comment_it < *value_it
                res.comments_without_argument.push_back(*comment_it);
                ++comment_it;
            }
        }

        res.arguments_without_comment.insert(res.arguments_without_comment.end(), value_it, value_args.end());
        res.comments_without_argument.insert(res.comments_without_argument.end(), comment_it, comment_args.end());

        return res;
    }

    void GenerateDefaultMessageMapCommand::perform_and_exit(const VcpkgCmdArguments& args, Filesystem& fs) const
    {
        auto parsed_args = args.parse_arguments(COMMAND_STRUCTURE);

        const bool output_comments = !Util::Sets::contains(parsed_args.switches, OPTION_NO_OUTPUT_COMMENTS);

        if (!output_comments && Util::Sets::contains(parsed_args.switches, OPTION_OUTPUT_COMMENTS))
        {
            Checks::msg_exit_with_error(
                VCPKG_LINE_INFO, msg::msgBothYesAndNoOptionSpecifiedError, msg::option = OPTION_OUTPUT_COMMENTS);
        }

        // in order to implement sorting, we create a vector of messages before converting into a JSON object
        struct Message
        {
            std::string name;
            std::string value;
            std::string comment;
        };
        struct MessageSorter
        {
            bool operator()(const Message& lhs, const Message& rhs) const { return lhs.name < rhs.name; }
        };

        const ::size_t size = msg::detail::number_of_messages();
        std::vector<Message> messages(size);
        for (::size_t index = 0; index < size; ++index)
        {
            auto& msg = messages[index];
            msg.name = msg::detail::get_message_name(index).to_string();
            msg.value = msg::detail::get_default_format_string(index).to_string();
            msg.comment = msg::detail::get_localization_comment(index).to_string();
        }
        std::sort(messages.begin(), messages.end(), MessageSorter{});

        bool has_errors = false;
        LocalizedString format_string_parsing_error;
        Json::Object obj;
        for (Message& msg : messages)
        {
            if (msg.name != "ErrorMessage" && Strings::case_insensitive_ascii_starts_with(msg.value, "error:"))
            {
                has_errors = true;
                msg::println_error(msgErrorMessageMustUsePrintError, msg::value = msg.name);
            }

            if (msg.name != "WarningMessage" && Strings::case_insensitive_ascii_starts_with(msg.value, "warning:"))
            {
                has_errors = true;
                msg::println_error(msgWarningMessageMustUsePrintWarning, msg::value = msg.name);
            }

            if (Strings::contains(msg.value, "   "))
            {
                has_errors = true;
                msg::println_error(msgLocalizedMessageMustNotContainIndents, msg::value = msg.name);
            }

            if (!msg.value.empty() && msg.value.back() == '\n')
            {
                has_errors = true;
                msg::println_error(msgLocalizedMessageMustNotEndWithNewline, msg::value = msg.name);
            }

            auto mismatches = get_format_arg_mismatches(msg.value, msg.comment, format_string_parsing_error);
            if (!format_string_parsing_error.data().empty())
            {
                has_errors = true;
                msg::println_error(msg::format(msgGenerateMsgErrorParsingFormatArgs, msg::value = msg.name)
                                       .append(format_string_parsing_error));
            }

            if (!mismatches.arguments_without_comment.empty() || !mismatches.comments_without_argument.empty())
            {
                has_errors = true;
                msg::println_error(msgGenerateMsgIncorrectComment, msg::value = msg.name);

                for (const auto& arg : mismatches.arguments_without_comment)
                {
                    msg::println(Color::error, msgGenerateMsgNoCommentValue, msg::value = arg);
                }
                for (const auto& comment : mismatches.comments_without_argument)
                {
                    msg::println(Color::error, msgGenerateMsgNoArgumentValue, msg::value = comment);
                }
            }

            obj.insert(msg.name, Json::Value::string(std::move(msg.value)));
            if (output_comments && !msg.comment.empty())
            {
                obj.insert(fmt::format("_{}.comment", msg.name), Json::Value::string(std::move(msg.comment)));
            }
        }

        if (has_errors)
        {
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        // get the path to artifacts messages.json
        Path path_to_artifact_messages = args.command_arguments[1];

        // parse file to get json obj
        auto artifact_messages = Json::parse_file(VCPKG_LINE_INFO, fs, path_to_artifact_messages).first;
        auto artifact_obj = artifact_messages.object(VCPKG_LINE_INFO);

        for (auto&& it : artifact_obj)
        {
            obj.insert(it.first, it.second);
        }

        auto stringified = Json::stringify(obj);
        Path filepath = fs.current_path(VCPKG_LINE_INFO) / args.command_arguments[0];
        fs.write_contents(filepath, stringified, VCPKG_LINE_INFO);
        Checks::exit_success(VCPKG_LINE_INFO);
    }
}
