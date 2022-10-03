#include <vcpkg/base/files.h>
#include <vcpkg/base/format.h>
#include <vcpkg/base/system.h>

#include <vcpkg/paragraphs.h>
#include <vcpkg/userconfig.h>

#include <iterator>

namespace
{
    using namespace vcpkg;
    static constexpr char CONFIG_NAME[] = "config";

    void set_value_if_set(std::string& target, const Paragraph& p, const std::string& key)
    {
        auto position = p.find(key);
        if (position != p.end())
        {
            target = position->second.first;
        }
    }
}

namespace vcpkg
{
    void UserConfig::to_string(std::string& target) const
    {
        fmt::format_to(std::back_inserter(target),
                       "User-Id: {}\n"
                       "User-Since: {}\n"
                       "Mac-Hash: {}\n"
                       "Survey-Completed: {}\n",
                       user_id,
                       user_time,
                       user_mac,
                       last_completed_survey);
    }

    std::string UserConfig::to_string() const
    {
        std::string ret;
        to_string(ret);
        return ret;
    }

    void UserConfig::try_write(Filesystem& fs) const
    {
        auto user_dir = get_user_dir();
        fs.create_directories(user_dir, IgnoreErrors{});
        auto config_path = user_dir / CONFIG_NAME;
        fs.write_contents(config_path, to_string(), IgnoreErrors{});
    }

    UserConfig try_parse_user_config(StringView content)
    {
        UserConfig ret;
        auto maybe_paragraph = Paragraphs::parse_single_merged_paragraph(content, "userconfig");
        if (const auto p = maybe_paragraph.get())
        {
            const auto& paragraph = *p;
            set_value_if_set(ret.user_id, paragraph, "User-Id");
            set_value_if_set(ret.user_time, paragraph, "User-Since");
            set_value_if_set(ret.user_mac, paragraph, "Mac-Hash");
            set_value_if_set(ret.last_completed_survey, paragraph, "Survey-Completed");
        }

        return ret;
    }

    UserConfig try_read_user_config(const Filesystem& fs)
    {
        std::error_code ec;
        const auto content = fs.read_contents(get_user_dir() / CONFIG_NAME, ec);
        if (ec)
        {
            return UserConfig{};
        }

        return try_parse_user_config(content);
    }

    Path get_user_dir()
    {
#if defined(_WIN32)
        return get_appdata_local().value_or_exit(VCPKG_LINE_INFO) / "vcpkg";
#else
        auto maybe_home = get_environment_variable("HOME");
        return Path(maybe_home.value_or("/var")) / ".vcpkg";
#endif
    }
}
