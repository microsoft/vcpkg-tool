#include <vcpkg/commands.generate-message-map.h>

#include <vcpkg/base/json.h>
#include <vcpkg/base/messages.h>

namespace vcpkg::Commands
{
    using namespace msg;
    void GenerateDefaultMessageMapCommand::perform_and_exit(const VcpkgCmdArguments&, Filesystem&) const
    {
        // in order to implement sorting, we create a vector of messages before converting into a JSON object
        struct Message
        {
            std::string name;
            std::string value;
            std::string comment;
        };
        struct MessageSorter
        {
            bool operator()(const Message& lhs, const Message& rhs) const
            {
                return lhs.name < rhs.name;
            }
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
        write_text_to_stdout(Color::None, Json::stringify(obj, {}));
        Checks::exit_success(VCPKG_LINE_INFO);
    }
}
