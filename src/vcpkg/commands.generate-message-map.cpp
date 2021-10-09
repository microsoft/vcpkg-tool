#include <vcpkg/commands.generate-message-map.h>

#include <vcpkg/base/json.h>
#include <vcpkg/base/messages.h>

namespace vcpkg::Commands
{
    using namespace msg;
    void GenerateDefaultMessageMapCommand::perform_and_exit(const VcpkgCmdArguments&, Filesystem&) const
    {
        Json::Object obj;
        auto size = detail::last_message_index();
        for (::size_t index = 0; index < size; ++index)
        {
            obj.insert(
                detail::get_message_name(index).to_string(),
                Json::Value::string(detail::get_default_format_string(index).to_string())
            );
            auto loc_comment = detail::get_localization_comment(index);
            if (!loc_comment.empty())
            {
                obj.insert(
                    fmt::format("_{}.comment", detail::get_message_name(index)),
                    Json::Value::string(loc_comment.to_string())
                );
            }
        }

        write_text_to_stdout(Color::None, Json::stringify(obj, {}));
        Checks::exit_success(VCPKG_LINE_INFO);
    }
}
