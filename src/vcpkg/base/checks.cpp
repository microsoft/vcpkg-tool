#include <vcpkg/base/checks.h>
#include <vcpkg/base/chrono.h>
#include <vcpkg/base/json.h>
#include <vcpkg/base/jsonreader.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/stringview.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.process.h>

#include <vcpkg/globalstate.h>
#include <vcpkg/metrics.h>
#include <vcpkg/paragraphs.h>

#include <stdlib.h>

namespace
{
    using namespace vcpkg;

    LocalizedString locale_invariant_lineinfo(const LineInfo& line_info)
    {
        return LocalizedString::from_raw(fmt::format("{}: ", line_info));
    }

    const ElapsedTimer g_total_time;
}

namespace vcpkg
{
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

        const auto elapsed_us_inner = g_total_time.microseconds();
        bool debugging = Debug::g_debugging;

        get_global_metrics_collector().track_elapsed_us(elapsed_us_inner);
        Debug::g_debugging = false;
        flush_global_metrics(get_real_filesystem());

#if defined(_WIN32)
        if (g_init_console_initialized)
        {
            SetConsoleCP(g_init_console_cp);
            SetConsoleOutputCP(g_init_console_output_cp);
        }
#endif

        if (debugging)
        {
            msg::write_unlocalized_text_to_stdout(Color::none,
                                                  Strings::concat("[DEBUG] Time in subprocesses: ",
                                                                  get_subproccess_stats(),
                                                                  " us\n",
                                                                  "[DEBUG] Time in parsing JSON: ",
                                                                  Json::get_json_parsing_stats(),
                                                                  " us\n",
                                                                  "[DEBUG] Time in JSON reader: ",
                                                                  Json::Reader::get_reader_stats(),
                                                                  " us\n",
                                                                  "[DEBUG] Time in filesystem: ",
                                                                  get_filesystem_stats(),
                                                                  " us\n",
                                                                  "[DEBUG] Time in loading ports: ",
                                                                  Paragraphs::get_load_ports_stats(),
                                                                  " us\n",
                                                                  "[DEBUG] Exiting after ",
                                                                  g_total_time.to_string(),
                                                                  " (",
                                                                  static_cast<int64_t>(elapsed_us_inner),
                                                                  " us)\n"));
        }

        fflush(nullptr);

#if defined(_WIN32)
        ::TerminateProcess(::GetCurrentProcess(), exit_code);
#endif
        std::exit(exit_code);
    }

    [[noreturn]] void Checks::unreachable(const LineInfo& line_info)
    {
        msg::println(Color::error, locale_invariant_lineinfo(line_info).append(msgChecksUnreachableCode));
#ifndef NDEBUG
        std::abort();
#else
        final_cleanup_and_exit(EXIT_FAILURE);
#endif
    }

    [[noreturn]] void Checks::unreachable(const LineInfo& line_info, StringView message)
    {
        msg::write_unlocalized_text_to_stdout(Color::error, locale_invariant_lineinfo(line_info).append_raw(message));
#ifndef NDEBUG
        std::abort();
#else
        final_cleanup_and_exit(EXIT_FAILURE);
#endif
    }

    [[noreturn]] void Checks::exit_with_code(const LineInfo& line_info, const int exit_code)
    {
        Debug::println(locale_invariant_lineinfo(line_info));
        final_cleanup_and_exit(exit_code);
    }

    [[noreturn]] void Checks::exit_fail(const LineInfo& line_info) { exit_with_code(line_info, EXIT_FAILURE); }

    [[noreturn]] void Checks::exit_success(const LineInfo& line_info) { exit_with_code(line_info, EXIT_SUCCESS); }

    [[noreturn]] void Checks::exit_with_message(const LineInfo& line_info, StringView error_message)
    {
        msg::write_unlocalized_text_to_stdout(Color::error, error_message);
        msg::write_unlocalized_text_to_stdout(Color::error, "\n");
        exit_fail(line_info);
    }
    [[noreturn]] void Checks::msg_exit_with_message(const LineInfo& line_info, const LocalizedString& error_message)
    {
        msg::println(Color::error, error_message);
        exit_fail(line_info);
    }

    [[noreturn]] void Checks::exit_with_message_and_line(const LineInfo& line_info, StringView error_message)
    {
        msg::println(Color::error, locale_invariant_lineinfo(line_info).append_raw(error_message));
        exit_fail(line_info);
    }

    void Checks::check_exit(const LineInfo& line_info, bool expression)
    {
        if (!expression)
        {
            msg::println(Color::error,
                         msg::format(msg::msgInternalErrorMessage)
                             .append(locale_invariant_lineinfo(line_info))
                             .append(msgChecksFailedCheck)
                             .append_raw('\n')
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
        msg::write_unlocalized_text_to_stdout(Color::error, error_message);
        msg::write_unlocalized_text_to_stdout(Color::error, "\n");
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
