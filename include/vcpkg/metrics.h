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

        void set_send_metrics(bool should_send_metrics);
        void set_print_metrics(bool should_print_metrics);
        void enable();

        void track_metric(const std::string& name, double value);
        void track_buildtime(const std::string& name, double value);
        void track_property(const std::string& name, const std::string& value);
        void track_feature(const std::string& feature, bool value);

        bool metrics_enabled();

        void upload(const std::string& payload);
        void flush(Filesystem& fs);
    };

    extern LockGuarded<Metrics> g_metrics;
}
