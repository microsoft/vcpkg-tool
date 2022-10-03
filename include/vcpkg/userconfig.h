#pragma once

#include <vcpkg/fwd/userconfig.h>

#include <vcpkg/base/files.h>

#include <string>

namespace vcpkg
{
    struct UserConfig
    {
        std::string user_id;
        std::string user_time;
        std::string user_mac;

        std::string last_completed_survey;

        void to_string(std::string&) const;
        std::string to_string() const;
        void try_write(Filesystem& fs) const;
    };

    UserConfig try_parse_user_config(StringView content);
    UserConfig try_read_user_config(const Filesystem& fs);
    Path get_user_dir();
}
