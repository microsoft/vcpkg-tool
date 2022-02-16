#include <vcpkg/base/json.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.generate-message-map.h>

namespace
{
    namespace msg = vcpkg::msg;
    DECLARE_AND_REGISTER_MESSAGE(
        GenerateMsgNoComment,
        (msg::value),
        "example of {value} is 'GenerateMsgHasParametersButNoComment'",
        R"(message {value} accepts arguments, but there was no comment associated with it.
    You should add a comment explaining what the argument will be replaced with.)");
    DECLARE_AND_REGISTER_MESSAGE(GenerateMsgNoCommentError, (), "", "At least one message that accepts arguments did not have a comment; add a comment in order to silence this message.");
}

namespace vcpkg::Commands
{
    using namespace msg;

    static constexpr StringLiteral OPTION_REQUIRE_COMMENTS = "require-comments";

    static constexpr CommandSwitch GENERATE_MESSAGE_MAP_SWITCHES[]{
        {OPTION_REQUIRE_COMMENTS, "Require comments for messages that take arguments."},
    };

    const static CommandStructure COMMAND_STRUCTURE = {
        create_example_string(R"###(x-generate-default-message-map locales/messages.json)###"),
        1,
        1,
        {GENERATE_MESSAGE_MAP_SWITCHES, {}, {}},
        nullptr,
    };

    static bool contains_format_argument(StringView sv)
    {
        auto last = sv.end();
        auto it = std::find(sv.begin(), last, '{');
        for (; it != last; it = std::find(it, last, '{'))
        {
            // *it == `{`
            ++it;
            // if the next character is EOF, then just return false
            if (it == last)
            {
                return false;
            }
            // if the next character is a `{`, then that's not a format argument
            // just skip that '{'
            else if (*it == '{')
            {
                ++it;
            }
            // otherwise, the next character is not a `{`, and thus: format argument
            else
            {
                return true;
            }
        }
        return false;
    }

    void GenerateDefaultMessageMapCommand::perform_and_exit(const VcpkgCmdArguments& args, Filesystem& fs) const
    {
        auto parsed_args = args.parse_arguments(COMMAND_STRUCTURE);

        bool require_comments = Util::Sets::contains(parsed_args.switches, OPTION_REQUIRE_COMMENTS);

        LocalizedString comments_msg_type;
        Color comments_msg_color;
        if (require_comments)
        {
            comments_msg_type = msg::format(msg::msgErrorMessage);
            comments_msg_color = Color::error;
        }
        else
        {
            comments_msg_type = msg::format(msg::msgWarningMessage);
            comments_msg_color = Color::warning;
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

        const ::size_t size = detail::number_of_messages();
        std::vector<Message> messages(size);
        for (::size_t index = 0; index < size; ++index)
        {
            auto& msg = messages[index];
            msg.name = detail::get_message_name(index).to_string();
            msg.value = detail::get_default_format_string(index).to_string();
            msg.comment = detail::get_localization_comment(index).to_string();
        }
        std::sort(messages.begin(), messages.end(), MessageSorter{});

        bool has_value_without_comment = false;
        Json::Object obj;
        for (Message& msg : messages)
        {
            if (contains_format_argument(msg.value) && msg.comment.empty())
            {
                has_value_without_comment = true;
                msg::print(comments_msg_color, comments_msg_type);
                msg::println(comments_msg_color, msgGenerateMsgNoComment, msg::value = msg.name);
            }

            obj.insert(msg.name, Json::Value::string(std::move(msg.value)));
            if (!msg.comment.empty())
            {
                obj.insert(fmt::format("_{}.comment", msg.name), Json::Value::string(std::move(msg.comment)));
            }
        }

        if (has_value_without_comment && require_comments)
        {
            Checks::exit_with_message(VCPKG_LINE_INFO, msgGenerateMsgNoCommentError);
        }

        auto stringified = Json::stringify(obj, {});
        Path filepath = fs.current_path(VCPKG_LINE_INFO) / args.command_arguments[0];
        fs.write_contents(filepath, stringified, VCPKG_LINE_INFO);
        Checks::exit_success(VCPKG_LINE_INFO);
    }
}
