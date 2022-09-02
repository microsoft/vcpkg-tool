#pragma once

#include <vcpkg/base/files.h>
#include <vcpkg/base/lockguarded.h>
#include <vcpkg/base/util.h>

#include <string>

namespace vcpkg
{
    struct Metrics
    {
        Metrics() = default;
        Metrics(const Metrics&) = delete;
        Metrics& operator=(const Metrics&) = delete;

        static void set_send_metrics(bool should_send_metrics);
        static void set_print_metrics(bool should_print_metrics);

        // This function is static and must be called outside the g_metrics lock.
        static void enable();

        void track_metric(const std::string& name, double value);
        void track_buildtime(const std::string& name, double value);
        void track_property(const std::string& name, const std::string& value);
        void track_property(const std::string& name, bool value);
        void track_feature(const std::string& feature, bool value);
        void track_option(const std::string& option, bool value);

        bool metrics_enabled();

        void upload(const std::string& payload);
        void flush(Filesystem& fs);
    };

    Optional<StringView> find_first_nonzero_mac(StringView sv);

    extern LockGuarded<Metrics> g_metrics;
}
