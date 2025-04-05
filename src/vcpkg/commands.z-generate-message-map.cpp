#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/json.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/setup-messages.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.z-generate-message-map.h>
#include <vcpkg/vcpkgcmdarguments.h>

using namespace vcpkg;

namespace
{
    constexpr CommandSwitch GENERATE_MESSAGE_MAP_SWITCHES[]{
        {SwitchNoOutputComments, msgCmdGenerateMessageMapOptNoOutputComments},
    };

    struct BadPrefixTest
    {
        StringLiteral prefix;
        StringLiteral prefix_name;
    };
} // unnamed namespace

namespace vcpkg
{
    constexpr CommandMetadata CommandZGenerateDefaultMessageMapMetadata{
        "z-generate-default-message-map",
        {/*intentionally undocumented*/},
        {},
        Undocumented,
        AutocompletePriority::Never,
        2,
        2,
        {GENERATE_MESSAGE_MAP_SWITCHES},
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

    void command_z_generate_default_message_map_and_exit(const VcpkgCmdArguments& args, const Filesystem& fs)
    {
        auto parsed_args = args.parse_arguments(CommandZGenerateDefaultMessageMapMetadata);
        const bool output_comments = !Util::Sets::contains(parsed_args.switches, SwitchNoOutputComments);

        auto messages = msg::get_sorted_english_messages();

        bool has_errors = false;
        LocalizedString format_string_parsing_error;
        Json::Object obj;

        std::vector<BadPrefixTest> tests{
            {"error:", "ErrorPrefix"},
            {"internal error:", "InternalErrorPrefix"},
            {"message:", "MessagePrefix"},
            {"note:", "NotePrefix"},
            {"warning:", "WarningPrefix"},
        };

        for (auto& msg : messages)
        {
            for (auto&& test : tests)
            {
                if (Strings::case_insensitive_ascii_starts_with(msg.value, test.prefix))
                {
                    has_errors = true;
                    msg::print(
                        error_prefix().append_raw(fmt::format("The message named {} starts with {}, it must be changed "
                                                              "to prepend {} in code instead.\n",
                                                              msg.name,
                                                              test.prefix,
                                                              test.prefix_name)));
                }
            }

            if (msg.value.contains("   "))
            {
                has_errors = true;
                msg::print(error_prefix().append_raw(
                    fmt::format("The message named {} contains what appears to be indenting which must be "
                                "changed to use LocalizedString::append_indent instead.\n",
                                msg.name)));
            }

            if (!msg.value.empty() && msg.value.back() == '\n')
            {
                has_errors = true;
                msg::print(error_prefix().append_raw(
                    fmt::format("The message named {} ends with a newline which should be added "
                                "by formatting rather than by localization.",
                                msg.name)));
            }

            auto mismatches = get_format_arg_mismatches(msg.value, msg.comment, format_string_parsing_error);
            if (!format_string_parsing_error.data().empty())
            {
                has_errors = true;
                msg::print(error_prefix().append_raw(fmt::format("parsing format string for {}:\n", msg.name)));
            }

            if (!mismatches.arguments_without_comment.empty() || !mismatches.comments_without_argument.empty())
            {
                has_errors = true;
                msg::print(error_prefix().append_raw(fmt::format("message {} has an incorrect comment:\n", msg.name)));

                for (const auto& arg : mismatches.arguments_without_comment)
                {
                    msg::print(error_prefix().append_raw(
                        fmt::format("{{{}}} is in the message, but is not commented\n", arg)));
                }
                for (const auto& comment : mismatches.comments_without_argument)
                {
                    msg::print(error_prefix().append_raw(
                        fmt::format("{{{}}} is in the comment, but not used in the message\n", comment)));
                }
            }

            obj.insert(msg.name, Json::Value::string(std::move(msg.value)));
            if (output_comments && !msg.comment.empty())
            {
                obj.insert("_" + msg.name + ".comment", Json::Value::string(std::move(msg.comment)));
            }
        }

        if (has_errors)
        {
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        // get the path to artifacts messages.json
        Path path_to_artifact_messages = parsed_args.command_arguments[1];

        // parse file to get json obj
        auto artifact_messages = Json::parse_file(VCPKG_LINE_INFO, fs, path_to_artifact_messages).value;
        auto artifact_obj = artifact_messages.object(VCPKG_LINE_INFO);

        for (auto&& it : artifact_obj)
        {
            obj.insert(it.first, it.second);
        }

        auto stringified = Json::stringify(obj);
        Path filepath = fs.current_path(VCPKG_LINE_INFO) / parsed_args.command_arguments[0];
        fs.write_contents(filepath, stringified, VCPKG_LINE_INFO);
        Checks::exit_success(VCPKG_LINE_INFO);
    }
}
