#include <vcpkg/base/checks.h>
#include <vcpkg/base/stringview.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.print.h>

#include <stdlib.h>

namespace vcpkg
{
    static void (*g_shutdown_handler)() = nullptr;

    DECLARE_AND_REGISTER_MESSAGE(ChecksLineInfo, (msg::vcpkg_line_info), "{Locked}", "{vcpkg_line_info}: ");
    DECLARE_AND_REGISTER_MESSAGE(ChecksUnreachableCode, (), "", "unreachable code was reached");
    DECLARE_AND_REGISTER_MESSAGE(ChecksFailedCheck, (), "", "vcpkg has crashed; no additional details are available.");
    DECLARE_AND_REGISTER_MESSAGE(ChecksUpdateVcpkg,
                                 (),
                                 "",
                                 "updating vcpkg by rerunning bootstrap-vcpkg may resolve this failure.");

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
        msg::println(Color::error,
                     msg::format(msgChecksLineInfo, msg::vcpkg_line_info = line_info).append(msgChecksUnreachableCode));
#ifndef NDEBUG
        std::abort();
#else
        final_cleanup_and_exit(EXIT_FAILURE);
#endif
    }

    [[noreturn]] void Checks::exit_with_code(const LineInfo& line_info, const int exit_code)
    {
        Debug::println(msg::format(msgChecksLineInfo, msg::vcpkg_line_info = line_info));
        final_cleanup_and_exit(exit_code);
    }

    [[noreturn]] void Checks::exit_fail(const LineInfo& line_info) { exit_with_code(line_info, EXIT_FAILURE); }

    [[noreturn]] void Checks::exit_success(const LineInfo& line_info) { exit_with_code(line_info, EXIT_SUCCESS); }

    [[noreturn]] void Checks::exit_with_message(const LineInfo& line_info, StringView error_message)
    {
        print2(Color::error, error_message, '\n');
        exit_fail(line_info);
    }
    [[noreturn]] void Checks::msg_exit_with_message(const LineInfo& line_info, const LocalizedString& error_message)
    {
        msg::println(Color::error, error_message);
        exit_fail(line_info);
    }

    [[noreturn]] void Checks::exit_with_message_and_line(const LineInfo& line_info, StringView error_message)
    {
        msg::print(Color::error, msgChecksLineInfo, msg::vcpkg_line_info = line_info);
        print2(Color::error, error_message, '\n');
        exit_fail(line_info);
    }

    void Checks::check_exit(const LineInfo& line_info, bool expression)
    {
        if (!expression)
        {
            msg::println(Color::error,
                         msg::format(msg::msgInternalErrorMessage)
                             .append(msgChecksLineInfo, msg::vcpkg_line_info = line_info)
                             .append(msgChecksFailedCheck)
                             .appendnl()
                             .append(msg::msgInternalErrorMessageContact));
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

    void Checks::msg_check_exit(const LineInfo& line_info, bool expression, const LocalizedString& error_message)
    {
        if (!expression)
        {
            msg_exit_with_message(line_info, error_message);
        }
    }

    static void display_upgrade_message()
    {
        msg::println(Color::error, msg::format(msg::msgNoteMessage).append(msgChecksUpdateVcpkg));
    }

    [[noreturn]] void Checks::exit_maybe_upgrade(const LineInfo& line_info)
    {
        display_upgrade_message();
        exit_fail(line_info);
    }

    [[noreturn]] void Checks::exit_maybe_upgrade(const LineInfo& line_info, StringView error_message)
    {
        print2(Color::error, error_message, '\n');
        display_upgrade_message();
        exit_fail(line_info);
    }
    [[noreturn]] void Checks::msg_exit_maybe_upgrade(const LineInfo& line_info, const LocalizedString& error_message)
    {
        msg::println(Color::error, error_message);
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

    void Checks::msg_check_maybe_upgrade(const LineInfo& line_info,
                                         bool expression,
                                         const LocalizedString& error_message)
    {
        if (!expression)
        {
            msg_exit_maybe_upgrade(line_info, error_message);
        }
    }
}
