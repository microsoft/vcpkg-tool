#pragma once

#include <vcpkg/fwd/userconfig.h>

#include <vcpkg/base/files.h>

#include <string>

namespace vcpkg
{
    struct MetricsUserConfig
    {
        std::string user_id;
        std::string user_time;
        std::string user_mac;

        std::string last_completed_survey;

        void to_string(std::string&) const;
        std::string to_string() const;
        void try_write(Filesystem& fs) const;

        // If *this is missing data normally provided by the system, fill it in;
        // otherwise, no effects.
        // Returns whether any values needed to be modified.
        bool fill_in_system_values();
    };

    MetricsUserConfig try_parse_metrics_user(StringView content);
    MetricsUserConfig try_read_metrics_user(const Filesystem& fs);
}
