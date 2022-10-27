#pragma once

#include <vcpkg/base/chrono.h>

#include <atomic>

namespace vcpkg
{
    extern std::atomic<int> g_init_console_cp;
    extern std::atomic<int> g_init_console_output_cp;
    extern std::atomic<bool> g_init_console_initialized;
}
