#include <vcpkg/messages.h>
#include <atomic>
#include <map>
#include <mutex>

namespace vcpkg::msg
{
    // this is basically a SoA - each index is:
    // {
    //   name
    //   default_string
    //   localized_string
    // }
    // requires: names.size() == default_strings.size() == localized_strings.size()
    static std::vector<std::string> message_names; // const after startup
    static std::vector<std::string> default_message_strings; // const after startup
    static std::vector<std::string> localization_comments; // const after startup

    static std::atomic<bool> initialized;
    static std::vector<std::string> localized_strings; // const after initialization

    void threadunsafe_initialize_context()
    {
        if (initialized)
        {
            write_text_to_stdout(Color::Error, "double-initialized message context; this is a very serious bug in vcpkg\n");
            Checks::exit_fail(VCPKG_LINE_INFO);
        }
        localized_strings.resize(message_names.size());
        initialized = true;
    }
    void threadunsafe_initialize_context(const Json::Object& message_map)
    {
        threadunsafe_initialize_context();
        std::vector<std::string> names_without_localization;

        for (::size_t index = 0; index < message_names.size(); ++index)
        {
            const auto& name = message_names[index];
            if (auto p = message_map.get(message_names[index]))
            {
                localized_strings[index] = p->string().to_string();
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

    void threadunsafe_initialize_context(const Filesystem& fs, StringView language, const Path& locale_base)
    {
        auto path_to_locale = locale_base;
        path_to_locale /= language;
        path_to_locale += ".json";

        auto message_map = Json::parse_file(VCPKG_LINE_INFO, fs, path_to_locale);
        if (!message_map.first.is_object())
        {
            write_text_to_stdout(Color::Error, "Invalid locale file '{}' - locale file must be an object.\n");
            Checks::exit_fail(VCPKG_LINE_INFO);
        }
        threadunsafe_initialize_context(message_map.first.object());
    }

    ::size_t detail::last_message_index()
    {
        return message_names.size();
    }

    static ::size_t startup_register_message(StringView name, StringView format_string, StringView comment)
    {
        auto res = message_names.size();
        message_names.push_back(name.to_string());
        default_message_strings.push_back(format_string.to_string());
        localization_comments.push_back(comment.to_string());
        return res;
    }

    StringView detail::get_format_string(::size_t index)
    {
        Checks::check_exit(VCPKG_LINE_INFO, localized_strings.size() == default_message_strings.size());
        Checks::check_exit(VCPKG_LINE_INFO, index < default_message_strings.size());
        const auto& localized = localized_strings[index];
        if(localized.empty())
        {
            return default_message_strings[index];
        }
        else
        {
            return localized;
        }
    }
    StringView detail::get_message_name(::size_t index)
    {
        Checks::check_exit(VCPKG_LINE_INFO, index < message_names.size());
        return message_names[index];
    }
    StringView detail::get_default_format_string(::size_t index)
    {
        Checks::check_exit(VCPKG_LINE_INFO, index < default_message_strings.size());
        return default_message_strings[index];
    }
    StringView detail::get_localization_comment(::size_t index)
    {
        Checks::check_exit(VCPKG_LINE_INFO, index < localization_comments.size());
        return localization_comments[index];
    }

#define REGISTER_MESSAGE(NAME) \
    const ::size_t NAME ## _t :: index = \
        startup_register_message(name(), default_format_string(), NAME ## _t::localization_comment())

    REGISTER_MESSAGE(VcpkgHasCrashed);
    REGISTER_MESSAGE(AllRequestedPackagesInstalled);
    REGISTER_MESSAGE(NoLocalizationForMessages);

#undef REGISTER_MESSAGE

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
