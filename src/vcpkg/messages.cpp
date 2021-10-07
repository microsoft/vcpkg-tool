#include <vcpkg/messages.h>
#include <map>

namespace vcpkg
{
    struct MessageContext::MessageContextImpl
    {
        // this is basically a SoA - each index is:
        // {
        //   name
        //   default_string
        //   localized_string
        // }
        // requires: names.size() == default_strings.size() == localized_strings.size()
        static std::vector<std::string> names; // const after startup
        static std::vector<std::string> default_strings; // const after startup
        static std::vector<std::string> localization_comments; // const after startup
        std::vector<std::string> localized_strings;

        MessageContextImpl() : localized_strings(default_strings.size()) {}
    };
    std::vector<std::string> MessageContext::MessageContextImpl::default_strings{};
    std::vector<std::string> MessageContext::MessageContextImpl::names{};
    std::vector<std::string> MessageContext::MessageContextImpl::localization_comments{};

    MessageContext::MessageContext(const Json::Object& message_map) : impl(std::make_unique<MessageContextImpl>())
    {
        std::vector<std::string> names_without_localization;
        auto& strs = impl->localized_strings;

        for (::size_t index = 0; index < MessageContextImpl::names.size(); ++index)
        {
            const auto& name = MessageContextImpl::names[index];
            if (auto p = message_map.get(MessageContextImpl::names[index]))
            {
                strs[index] = p->string().to_string();
            }
            else
            {
                names_without_localization.push_back(name);
            }
        }

        if (!names_without_localization.empty())
        {
            println(Color::Warning, NoLocalizationForMessages);
            for (const auto& name : names_without_localization)
            {
                write_text_to_stdout(Color::Warning, fmt::format("    - {}\n", name));
            }
        }
    }

    MessageContext::MessageContext(StringView language) : impl(std::make_unique<MessageContextImpl>())
    {
        (void)language;
    }

    ::size_t MessageContext::last_message_index()
    {
        return MessageContextImpl::names.size();
    }

    ::size_t MessageContext::_internal_register_message(StringView name, StringView format_string, StringView comment)
    {
        auto res = MessageContextImpl::names.size();
        MessageContextImpl::names.push_back(name.to_string());
        MessageContextImpl::default_strings.push_back(format_string.to_string());
        MessageContextImpl::localization_comments.push_back(comment.to_string());
        return res;
    }

    StringView MessageContext::get_format_string(::size_t index) const
    {
        Checks::check_exit(VCPKG_LINE_INFO, impl->localized_strings.size() == MessageContextImpl::default_strings.size());
        Checks::check_exit(VCPKG_LINE_INFO, index < MessageContextImpl::default_strings.size());
        const auto& localized = impl->localized_strings[index];
        if(localized.empty())
        {
            return MessageContextImpl::default_strings[index];
        }
        else
        {
            return localized;
        }
    }
    StringView MessageContext::get_message_name(::size_t index)
    {
        Checks::check_exit(VCPKG_LINE_INFO, index < MessageContextImpl::names.size());
        return MessageContextImpl::names[index];
    }
    StringView MessageContext::get_default_format_string(::size_t index)
    {
        Checks::check_exit(VCPKG_LINE_INFO, index < MessageContextImpl::default_strings.size());
        return MessageContextImpl::default_strings[index];
    }
    StringView MessageContext::get_localization_comment(::size_t index)
    {
        Checks::check_exit(VCPKG_LINE_INFO, index < MessageContextImpl::localization_comments.size());
        return MessageContextImpl::localization_comments[index];
    }

#define REGISTER_MESSAGE(NAME) \
    const ::size_t MessageContext :: NAME ## _t :: index = \
        _internal_register_message(name(), default_format_string(), NAME ## _t::localization_comment())

    REGISTER_MESSAGE(VcpkgHasCrashed);
    REGISTER_MESSAGE(AllRequestedPackagesInstalled);
    REGISTER_MESSAGE(NoLocalizationForMessages);

    void GenerateDefaultMessageMapCommand::perform_and_exit(const VcpkgCmdArguments&, Filesystem&) const
    {
        Json::Object obj;
        auto size = MessageContext::last_message_index();
        for (::size_t index = 0; index < size; ++index)
        {
            obj.insert(
                MessageContext::get_message_name(index).to_string(),
                Json::Value::string(MessageContext::get_default_format_string(index).to_string())
            );
            auto loc_comment = MessageContext::get_localization_comment(index);
            if (!loc_comment.empty())
            {
                obj.insert(
                    fmt::format("_{}.comment", MessageContext::get_message_name(index)),
                    Json::Value::string(loc_comment.to_string())
                );
            }
        }

        write_text_to_stdout(Color::None, Json::stringify(obj, {}));
        Checks::exit_success(VCPKG_LINE_INFO);
    }
}
