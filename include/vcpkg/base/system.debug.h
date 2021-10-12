#pragma once

#include <vcpkg/base/chrono.h>
#include <vcpkg/base/lineinfo.h>
#include <vcpkg/base/system.print.h>

#include <atomic>

namespace vcpkg::Debug
{
    extern std::atomic<bool> g_debugging;

    inline void println(StringView sv)
    {
        if (g_debugging)
        {
            msg::write_text_to_stdout(Color::None, "[DEBUG] ");
            msg::write_text_to_stdout(Color::None, sv);
            msg::write_text_to_stdout(Color::None, "\n");
        }
    }
    template<class... Args>
    void println(fmt::format_string<Args...> f, const Args&... args)
    {
        if (g_debugging)
        {
            auto msg = fmt::format(f, args...);
            Debug::println(msg);
        }
    }

    template<class... Args>
    void print(const Args&... args)
    {
        if (g_debugging) print2("[DEBUG] ", args...);
    }

    template<class F, class R = std::result_of_t<F && ()>, class = std::enable_if_t<!std::is_void<R>::value>>
    R time(LineInfo line, F&& f)
    {
        if (g_debugging)
        {
            auto timer = ElapsedTimer::create_started();
            auto&& result = f();
            print2("[DEBUG] ", line, " took ", timer, '\n');
            return static_cast<R&&>(result);
        }
        else
            return f();
    }

    template<class F, class R = std::result_of_t<F && ()>, class = std::enable_if_t<std::is_void<R>::value>>
    void time(LineInfo line, F&& f)
    {
        if (g_debugging)
        {
            auto timer = ElapsedTimer::create_started();
            f();
            print2("[DEBUG] ", line, " took ", timer, '\n');
        }
        else
            f();
    }
}
