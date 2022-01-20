#include <vcpkg/base/json.h>
#include <vcpkg/base/messages.h>

#include <vcpkg/commands.generate-message-map.h>

namespace vcpkg::Commands
{
    using namespace msg;

    const CommandStructure COMMAND_STRUCTURE = {
        create_example_string(R"###(x-generate-default-message-map locales/messages.json)###"),
        0,
        1,
        {{}, {}, {}},
        nullptr,
    };

    void GenerateDefaultMessageMapCommand::perform_and_exit(const VcpkgCmdArguments& args, Filesystem& fs) const
    {
        auto parsed_args = args.parse_arguments(COMMAND_STRUCTURE);

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
            messages[index].name = detail::get_message_name(index).to_string();
            messages[index].value = detail::get_default_format_string(index).to_string();
            messages[index].comment = detail::get_localization_comment(index).to_string();
        }
        std::sort(messages.begin(), messages.end(), MessageSorter{});

        Json::Object obj;
        for (Message& msg : messages)
        {
            obj.insert(msg.name, Json::Value::string(std::move(msg.value)));
            if (!msg.comment.empty())
            {
                obj.insert(fmt::format("_{}.comment", msg.name), Json::Value::string(std::move(msg.comment)));
            }
        }

        auto stringified = Json::stringify(obj, {});
        if (args.command_arguments.size() == 0)
        {
            msg::write_unlocalized_text_to_stdout(Color::none, stringified);
        }
        else
        {
            Path filepath = fs.current_path(VCPKG_LINE_INFO) / args.command_arguments[0];
            fs.write_contents(filepath, stringified, VCPKG_LINE_INFO);
        }
        Checks::exit_success(VCPKG_LINE_INFO);
    }
}
