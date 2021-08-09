#pragma once

#include <vcpkg/base/fwd/lockguarded.h>

#include <vcpkg/base/chrono.h>

#include <atomic>
#include <string>

namespace vcpkg
{
    struct GlobalState
    {
        static LockGuarded<ElapsedTimer> timer;
        static LockGuarded<std::string> g_surveydate;

        static std::atomic<int> g_init_console_cp;
        static std::atomic<int> g_init_console_output_cp;
        static std::atomic<bool> g_init_console_initialized;
    };
}
