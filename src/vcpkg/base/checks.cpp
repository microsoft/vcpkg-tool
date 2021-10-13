#include <vcpkg/base/checks.h>
#include <vcpkg/base/stringview.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/messages.h>

#include <stdlib.h>

namespace vcpkg
{
    static void (*g_shutdown_handler)() = nullptr;

    DECLARE_AND_REGISTER_MESSAGE(UnreachableCode,
                                 (msg::line_info),
                                 "",
                                 "Error: Unreachable code was reached\n{line_info}");

    void Checks::register_global_shutdown_handler(void (*func)())
    {
        if (g_shutdown_handler)
            // Setting the handler twice is a program error. Terminate.
            std::abort();
        g_shutdown_handler = func;
    }

    [[noreturn]] void Checks::final_cleanup_and_exit(const int exit_code)
    {
        static std::atomic<bool> have_entered{false};
        if (have_entered.exchange(true))
        {
#if defined(_WIN32)
            ::TerminateProcess(::GetCurrentProcess(), exit_code);
#else
            std::abort();
#endif
        }

        if (g_shutdown_handler) g_shutdown_handler();

        fflush(nullptr);

#if defined(_WIN32)
        ::TerminateProcess(::GetCurrentProcess(), exit_code);
#endif
        std::exit(exit_code);
    }

    [[noreturn]] void Checks::unreachable(const LineInfo& line_info)
    {
        msg::println(Color::Error, msgUnreachableCode, msg::line_info = line_info);
#ifndef NDEBUG
        std::abort();
#else
        final_cleanup_and_exit(EXIT_FAILURE);
#endif
    }

    [[noreturn]] void Checks::exit_with_code(const LineInfo& line_info, const int exit_code)
    {
        Debug::print(Strings::format("%s(%d)\n", line_info.file_name, line_info.line_number));
        final_cleanup_and_exit(exit_code);
    }

    [[noreturn]] void Checks::exit_fail(const LineInfo& line_info) { exit_with_code(line_info, EXIT_FAILURE); }

    [[noreturn]] void Checks::exit_success(const LineInfo& line_info) { exit_with_code(line_info, EXIT_SUCCESS); }

    [[noreturn]] void Checks::exit_with_message(const LineInfo& line_info, StringView error_message)
    {
        msg::write_unlocalized_text_to_stdout(Color::Error, error_message);
        msg::write_newline_to_stdout();
        exit_fail(line_info);
    }

    namespace
    {
        DECLARE_AND_REGISTER_MESSAGE(VcpkgHasCrashedNoDetails, (msg::line_info), "",
    R"(Error: vcpkg has crashed; no additional details are available.
The source line is {line_info})");
        DECLARE_AND_REGISTER_MESSAGE(VcpkgRerunBootstrap, (), "", "Note: Updating vcpkg by rerunning bootstrap-vcpkg may resolve this failure.");
    }

    void Checks::check_exit(const LineInfo& line_info, bool expression)
    {
        if (!expression)
        {
            msg::println(Color::Error, msgVcpkgHasCrashedNoDetails, msg::line_info = line_info);
            exit_fail(line_info);
        }
    }

    void Checks::check_exit(const LineInfo& line_info, bool expression, StringView error_message)
    {
        if (!expression)
        {
            exit_with_message(line_info, error_message);
        }
    }

    static void display_upgrade_message()
    {
        msg::println(Color::Error, msgVcpkgRerunBootstrap);
    }

    [[noreturn]] void Checks::exit_maybe_upgrade(const LineInfo& line_info)
    {
        display_upgrade_message();
        exit_fail(line_info);
    }

    [[noreturn]] void Checks::exit_maybe_upgrade(const LineInfo& line_info, StringView error_message)
    {
        msg::write_unlocalized_text_to_stdout(Color::Error, error_message);
        msg::write_newline_to_stdout();
        display_upgrade_message();
        exit_fail(line_info);
    }

    void Checks::check_maybe_upgrade(const LineInfo& line_info, bool expression)
    {
        if (!expression)
        {
            exit_maybe_upgrade(line_info);
        }
    }

    void Checks::check_maybe_upgrade(const LineInfo& line_info, bool expression, StringView error_message)
    {
        if (!expression)
        {
            exit_maybe_upgrade(line_info, error_message);
        }
    }
}
