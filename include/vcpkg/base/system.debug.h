#pragma once

#include <vcpkg/base/chrono.h>
#include <vcpkg/base/lineinfo.h>

#include <atomic>

namespace vcpkg::Debug
{
    extern std::atomic<bool> g_debugging;

    void print(StringView sv);
    void println(StringView sv);

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
        if (g_debugging) print(Strings::concat_or_view(args...));
    }

    template<class F, class R = std::result_of_t<F && ()>, class = std::enable_if_t<!std::is_void<R>::value>>
    R time(LineInfo line, F&& f)
    {
        if (g_debugging)
        {
            auto timer = ElapsedTimer::create_started();
            auto&& result = f();
            println("{} took {}", line, timer);
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
            println("{} took {}", line, timer);
        }
        else
            f();
    }
}
