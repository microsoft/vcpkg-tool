#include <vcpkg/base/files.h>
#include <vcpkg/base/lazy.h>
#include <vcpkg/base/system.h>

#include <vcpkg/paragraphs.h>
#include <vcpkg/userconfig.h>

namespace vcpkg
{
    Path get_user_dir()
    {
#if defined(_WIN32)
        return get_appdata_local().value_or_exit(VCPKG_LINE_INFO) / "vcpkg";
#else
        auto maybe_home = get_environment_variable("HOME");
        return Path(maybe_home.value_or("/var")) / ".vcpkg";
#endif
    }

    static Path get_config_path() { return get_user_dir() / "config"; }

    UserConfig UserConfig::try_read_data(const Filesystem& fs)
    {
        UserConfig ret;
        try
        {
            auto maybe_pghs = Paragraphs::get_paragraphs(fs, get_config_path());
            if (const auto p_pghs = maybe_pghs.get())
            {
                const auto& pghs = *p_pghs;

                Paragraph keys;
                if (pghs.size() > 0) keys = pghs[0];

                for (size_t x = 1; x < pghs.size(); ++x)
                {
                    for (auto&& p : pghs[x])
                        keys.insert(p);
                }

                ret.user_id = keys["User-Id"].first;
                ret.user_time = keys["User-Since"].first;
                ret.user_mac = keys["Mac-Hash"].first;
                ret.last_completed_survey = keys["Survey-Completed"].first;
            }
        }
        catch (...)
        {
        }

        return ret;
    }

    void UserConfig::try_write_data(Filesystem& fs) const
    {
        try
        {
            auto config_path = get_config_path();
            auto config_dir = config_path.parent_path();
            std::error_code ec;
            fs.create_directory(config_dir, ec);
            fs.write_contents(config_path,
                              Strings::format("User-Id: %s\n"
                                              "User-Since: %s\n"
                                              "Mac-Hash: %s\n"
                                              "Survey-Completed: %s\n",
                                              user_id,
                                              user_time,
                                              user_mac,
                                              last_completed_survey),
                              ec);
        }
        catch (...)
        {
        }
    }
}
