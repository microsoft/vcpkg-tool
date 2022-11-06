#include <vcpkg/base/system_headers.h>

#include <vcpkg/base/chrono.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/json.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/pragmas.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.process.h>

#include <vcpkg/cgroup-parser.h>
#include <vcpkg/commands.contact.h>
#include <vcpkg/commands.h>
#include <vcpkg/commands.version.h>
#include <vcpkg/globalstate.h>
#include <vcpkg/help.h>
#include <vcpkg/input.h>
#include <vcpkg/metrics.h>
#include <vcpkg/paragraphs.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkglib.h>
#include <vcpkg/vcpkgpaths.h>

#include <locale.h>

#include <cassert>
#include <clocale>
#include <memory>
#include <random>

#if defined(_WIN32)
#pragma comment(lib, "ole32")
#pragma comment(lib, "shell32")
#endif

using namespace vcpkg;

namespace
{
    void invalid_command(const std::string& cmd)
    {
        msg::println(Color::error, msgVcpkgInvalidCommand, msg::command_name = cmd);
        print_usage();
        Checks::exit_fail(VCPKG_LINE_INFO);
    }

    bool detect_container(vcpkg::Filesystem& fs)
    {
        (void)fs;
#if defined(_WIN32)
        if (test_registry_key(HKEY_LOCAL_MACHINE, R"(SYSTEM\CurrentControlSet\Services\cexecsvc)"))
        {
            Debug::println("Detected Container Execution Service");
            return true;
        }

        auto username = get_username();
        if (username == L"ContainerUser" || username == L"ContainerAdministrator")
        {
            Debug::println("Detected container username");
            return true;
        }
#elif defined(__linux__)
        if (fs.exists("/.dockerenv", IgnoreErrors{}))
        {
            Debug::println("Detected /.dockerenv file");
            return true;
        }

        // check /proc/1/cgroup, if we're running in a container then the control group for each hierarchy will be:
        //   /docker/<containerid>, or
        //   /lxc/<containerid>
        //
        // Example of /proc/1/cgroup contents:
        // 2:memory:/docker/66a5f8000f3f2e2a19c3f7d60d870064d26996bdfe77e40df7e3fc955b811d14
        // 1:name=systemd:/docker/66a5f8000f3f2e2a19c3f7d60d870064d26996bdfe77e40df7e3fc955b811d14
        // 0::/docker/66a5f8000f3f2e2a19c3f7d60d870064d26996bdfe77e40df7e3fc955b811d14
        auto cgroup_contents = fs.read_contents("/proc/1/cgroup", IgnoreErrors{});
        if (detect_docker_in_cgroup_file(cgroup_contents, "/proc/1/cgroup"))
        {
            Debug::println("Detected docker in cgroup");
            return true;
        }
#endif
        return false;
    }

    void inner(vcpkg::Filesystem& fs, const VcpkgCmdArguments& args)
    {
        // track version on each invocation
        get_global_metrics_collector().track_string(StringMetric::VcpkgVersion, Commands::Version::version.to_string());

        if (args.command.empty())
        {
            print_usage();
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        static const auto find_command = [&](auto&& commands) {
            auto it = Util::find_if(commands, [&](auto&& commandc) {
                return Strings::case_insensitive_ascii_equals(commandc.name, args.command);
            });
            using std::end;
            if (it != end(commands))
            {
                return &*it;
            }
            else
            {
                return static_cast<decltype(&*it)>(nullptr);
            }
        };

        get_global_metrics_collector().track_bool(BoolMetric::DetectedContainer, detect_container(fs));

        if (const auto command_function = find_command(Commands::get_available_basic_commands()))
        {
            get_global_metrics_collector().track_string(StringMetric::CommandName, command_function->name);
            return command_function->function->perform_and_exit(args, fs);
        }

        const VcpkgPaths paths(fs, args);
        get_global_metrics_collector().track_bool(BoolMetric::FeatureFlagManifests, paths.manifest_mode_enabled());
        get_global_metrics_collector().track_bool(BoolMetric::OptionOverlayPorts, !paths.overlay_ports.empty());

        fs.current_path(paths.root, VCPKG_LINE_INFO);

        if (const auto command_function = find_command(Commands::get_available_paths_commands()))
        {
            get_global_metrics_collector().track_string(StringMetric::CommandName, command_function->name);
            return command_function->function->perform_and_exit(args, paths);
        }

        Triplet default_triplet = vcpkg::default_triplet(args);
        check_triplet(default_triplet, paths);
        Triplet host_triplet = vcpkg::default_host_triplet(args);
        check_triplet(host_triplet, paths);

        if (const auto command_function = find_command(Commands::get_available_triplet_commands()))
        {
            get_global_metrics_collector().track_string(StringMetric::CommandName, command_function->name);
            return command_function->function->perform_and_exit(args, paths, default_triplet, host_triplet);
        }

        return invalid_command(args.command);
    }

    const ElapsedTimer g_total_time;
}

namespace vcpkg::Checks
{
    // Implements link seam from basic_checks.h
    void on_final_cleanup_and_exit()
    {
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
    }
}

#if defined(_WIN32)
// note: this prevents a false positive for -Wmissing-prototypes on clang-cl
int wmain(int, const wchar_t* const* const);

#if !defined(_MSC_VER)
#include <shellapi.h>
int main(int argc, const char* const* const /*argv*/)
{
    wchar_t** wargv;
    wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    return wmain(argc, wargv);
}
#endif

int wmain(const int argc, const wchar_t* const* const argv)
#else
int main(const int argc, const char* const* const argv)
#endif
{
    if (argc == 0) std::abort();

    ElapsedTimer total_timer;
    auto& fs = get_real_filesystem();
    auto maybe_vslang = get_environment_variable("VSLANG");
    if (const auto vslang = maybe_vslang.get())
    {
        const auto maybe_lcid_opt = Strings::strto<int>(*vslang);
        if (const auto lcid_opt = maybe_lcid_opt.get())
        {
            const auto maybe_map = msg::get_message_map_from_lcid(*lcid_opt);
            if (const auto map = maybe_map.get())
            {
                msg::load_from_message_map(*map);
            }
        }
    }

#if defined(_WIN32)
    g_init_console_cp = GetConsoleCP();
    g_init_console_output_cp = GetConsoleOutputCP();
    g_init_console_initialized = true;

    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);

    initialize_global_job_object();
#else
    static const char* const utf8_locales[] = {
        "C.UTF-8",
        "POSIX.UTF-8",
        "en_US.UTF-8",
    };

    for (const char* utf8_locale : utf8_locales)
    {
        if (::setlocale(LC_ALL, utf8_locale))
        {
            ::setenv("LC_ALL", utf8_locale, true);
            break;
        }
    }
#endif
    set_environment_variable("VCPKG_COMMAND", get_exe_path_of_current_process().generic_u8string());

    // Prevent child processes (ex. cmake) from producing "colorized"
    // output (which may include ANSI escape codes), since it would
    // complicate parsing the output.
    //
    // See http://bixense.com/clicolors for the semantics associated with
    // the CLICOLOR and CLICOLOR_FORCE env variables
    //
    set_environment_variable("CLICOLOR_FORCE", {});
    set_environment_variable("CLICOLOR", "0");

    register_console_ctrl_handler();

#if (defined(__aarch64__) || defined(__arm__) || defined(__s390x__) ||                                                 \
     ((defined(__ppc64__) || defined(__PPC64__) || defined(__ppc64le__) || defined(__PPC64LE__)) &&                    \
      defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)) ||                                       \
     defined(_M_ARM) || defined(_M_ARM64)) &&                                                                          \
    !defined(_WIN32) && !defined(__APPLE__)
    if (!get_environment_variable("VCPKG_FORCE_SYSTEM_BINARIES").has_value())
    {
        Checks::msg_exit_with_message(VCPKG_LINE_INFO, msgForceSystemBinariesOnWeirdPlatforms);
    }
#endif

    VcpkgCmdArguments args = VcpkgCmdArguments::create_from_command_line(fs, argc, argv);
    if (const auto p = args.debug.get()) Debug::g_debugging = *p;
    args.imbue_from_environment();
    VcpkgCmdArguments::imbue_or_apply_process_recursion(args);
    if (const auto p = args.debug_env.get(); p && *p)
    {
        msg::write_unlocalized_text_to_stdout(Color::none,
                                              "[DEBUG] The following environment variables are currently set:\n" +
                                                  get_environment_variables() + '\n');
    }
    else if (Debug::g_debugging)
    {
        Debug::println("To include the environment variables in debug output, pass --debug-env");
    }
    args.check_feature_flag_consistency();

    bool to_enable_metrics = true;
    auto disable_metrics_tag_file_path = get_exe_path_of_current_process();
    disable_metrics_tag_file_path.replace_filename("vcpkg.disable-metrics");

    std::error_code ec;
    if (fs.exists(disable_metrics_tag_file_path, ec) || ec)
    {
        to_enable_metrics = false;
    }

    if (auto p = args.disable_metrics.get())
    {
        to_enable_metrics = !*p;
    }

    if (to_enable_metrics)
    {
        g_metrics_enabled = true;
    }

    if (const auto p = args.print_metrics.get())
    {
        g_should_print_metrics = *p;
    }

    if (const auto p = args.send_metrics.get())
    {
        g_should_send_metrics = *p;
    }

    if (args.send_metrics.value_or(false) && !to_enable_metrics)
    {
        msg::println_warning(msgVcpkgSendMetricsButDisabled);
    }

    args.debug_print_feature_flags();
    args.track_feature_flag_metrics();
    args.track_environment_metrics();

    if (Debug::g_debugging)
    {
        inner(fs, args);
        Checks::exit_fail(VCPKG_LINE_INFO);
    }

    std::string exc_msg;
    try
    {
        inner(fs, args);
        Checks::exit_fail(VCPKG_LINE_INFO);
    }
    catch (std::exception& e)
    {
        exc_msg = e.what();
    }
    catch (...)
    {
        exc_msg = "unknown error(...)";
    }

    fflush(stdout);
    msg::println(msgVcpkgHasCrashed);
    fflush(stdout);
    msg::println();
    LocalizedString data_blob;
    data_blob.append_raw("Version=")
        .append_raw(Commands::Version::version)
        .append_raw("\nEXCEPTION=")
        .append_raw(exc_msg)
        .append_raw("\nCMD=\n");
    for (int x = 0; x < argc; ++x)
    {
#if defined(_WIN32)
        data_blob.append_raw(Strings::to_utf8(argv[x])).append_raw("|\n");
#else
        data_blob.append_raw(argv[x]).append_raw("|\n");
#endif
    }

    msg::print(data_blob);
    fflush(stdout);

    // It is expected that one of the sub-commands will exit cleanly before we get here.
    Checks::exit_fail(VCPKG_LINE_INFO);
}
