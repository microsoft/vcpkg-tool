#pragma once

#include <vcpkg/base/fwd/messages.h>

#include <vcpkg/base/lineinfo.h>
#include <vcpkg/base/strings.h>

#include <atomic>

namespace vcpkg::Debug
{
    extern std::atomic<bool> g_debugging;

    template<class... Args>
    void print(const Args&... args)
    {
        if (g_debugging) msg::write_unlocalized_text(Color::none, Strings::concat("[DEBUG] ", args...));
    }
    template<class... Args>
    void println(const Args&... args)
    {
        if (g_debugging) msg::write_unlocalized_text(Color::none, Strings::concat("[DEBUG] ", args..., '\n'));
    }
}
