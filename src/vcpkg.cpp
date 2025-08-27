#include <vcpkg/base/system-headers.h>

#include <vcpkg/base/chrono.h>
#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/json.h>
#include <vcpkg/base/jsonreader.h>
#include <vcpkg/base/pragmas.h>
#include <vcpkg/base/setup-messages.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.h>
#include <vcpkg/base/system.process.h>
#include <vcpkg/base/util.h>

#include <vcpkg/bundlesettings.h>
#include <vcpkg/cgroup-parser.h>
#include <vcpkg/commands.h>
#include <vcpkg/commands.version.h>
#include <vcpkg/metrics.h>
#include <vcpkg/paragraphs.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

#include <locale.h>

#if defined(_WIN32)
#include <atomic>

#pragma comment(lib, "ole32")
#pragma comment(lib, "shell32")
#endif

#if defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__)
#define TEST_LIBCURL_AVAILABLE
#include <dlfcn.h>
#endif

using namespace vcpkg;

namespace
{
#if defined(_WIN32)
    std::atomic<int> g_init_console_cp(0);
    std::atomic<int> g_init_console_output_cp(0);
    std::atomic<bool> g_init_console_initialized(false);
#endif // ^^^ _WIN32

    void invalid_command(const VcpkgCmdArguments& args)
    {
        msg::write_unlocalized_text_to_stderr(
            Color::error,
            LocalizedString::from_raw(ErrorPrefix)
                .append(msgVcpkgInvalidCommand, msg::command_name = args.get_command())
                .append_raw('\n'));
        msg::write_unlocalized_text_to_stderr(Color::none, get_zero_args_usage());
        Checks::exit_fail(VCPKG_LINE_INFO);
    }

    Optional<std::string> detect_libcurl()
    {
        // Determine if libcurl.so.4 is installed in the system by attempting to load it
        // At the moment we don't do anything with it, but we're tracking availability
        // of libcurl to replace the current download/upload implementation
#if defined(TEST_LIBCURL_AVAILABLE)
        // calling dlclose() on the handle after calling curl_version() causes asan to
        // report a false leak, so we intentionally don't unload the library
        if (auto handle = dlopen("libcurl.so.4", RTLD_NOW | RTLD_LOCAL))
        {
            auto curl_version_fn = reinterpret_cast<const char* (*)()>(dlsym(handle, "curl_version"));
            if (!dlerror() && curl_version_fn)
            {
                return {curl_version_fn()};
            }
        }
#endif
        return nullopt;
    }

    bool detect_container(const Filesystem& fs)
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

    template<class CommandRegistrationKind>
    static const CommandRegistrationKind* choose_command(const std::string& command_name,
                                                         View<CommandRegistrationKind> command_registrations)
    {
        for (const auto& command_registration : command_registrations)
        {
            if (Strings::case_insensitive_ascii_equals(command_registration.metadata.name, command_name))
            {
                return &command_registration;
            }
        }

        return nullptr;
    }

    void inner(const Filesystem& fs, const VcpkgCmdArguments& args, const BundleSettings& bundle)
    {
        // track version on each invocation
        get_global_metrics_collector().track_string(StringMetric::VcpkgVersion, vcpkg_executable_version);

        get_global_metrics_collector().track_bool(BoolMetric::DetectedContainer, detect_container(fs));
        get_global_metrics_collector().track_string(StringMetric::DetectedLibCurlVersion,
                                                    detect_libcurl().value_or("unknown"));

        if (args.get_command().empty())
        {
            msg::write_unlocalized_text_to_stderr(Color::none, get_zero_args_usage());
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        if (const auto command_function = choose_command(args.get_command(), basic_commands))
        {
            get_global_metrics_collector().track_string(StringMetric::CommandName, command_function->metadata.name);
            return command_function->function(args, fs);
        }

        const VcpkgPaths paths(fs, args, bundle);
        get_global_metrics_collector().track_bool(BoolMetric::FeatureFlagManifests, paths.manifest_mode_enabled());
        get_global_metrics_collector().track_bool(BoolMetric::OptionOverlayPorts, !paths.overlay_ports.empty());

        fs.current_path(paths.root, VCPKG_LINE_INFO);

        if (const auto command_function = choose_command(args.get_command(), paths_commands))
        {
            get_global_metrics_collector().track_string(StringMetric::CommandName, command_function->metadata.name);
            return command_function->function(args, paths);
        }

        Triplet default_triplet = vcpkg::default_triplet(args, paths.get_triplet_db());
        Triplet host_triplet = vcpkg::default_host_triplet(args, paths.get_triplet_db());
        if (const auto command_function = choose_command(args.get_command(), triplet_commands))
        {
            get_global_metrics_collector().track_string(StringMetric::CommandName, command_function->metadata.name);
            return command_function->function(args, paths, default_triplet, host_triplet);
        }

        return invalid_command(args);
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
        flush_global_metrics(real_filesystem);

#if defined(_WIN32)
        if (g_init_console_initialized)
        {
            SetConsoleCP(g_init_console_cp);
            SetConsoleOutputCP(g_init_console_output_cp);
        }
#endif

        if (debugging)
        {
            auto exit_debug_msg = fmt::format("[DEBUG] Time in subprocesses: {}us\n"
                                              "[DEBUG] Time in parsing JSON: {}us\n"
                                              "[DEBUG] Time in JSON reader: {}us\n"
                                              "[DEBUG] Time in filesystem: {}us\n"
                                              "[DEBUG] Time in loading ports: {}us\n"
                                              "[DEBUG] Exiting after {} ({}us)\n",
                                              get_subproccess_stats(),
                                              Json::get_json_parsing_stats(),
                                              Json::Reader::get_reader_stats(),
                                              get_filesystem_stats(),
                                              Paragraphs::get_load_ports_stats(),
                                              g_total_time.to_string(),
                                              static_cast<int64_t>(elapsed_us_inner));
            msg::write_unlocalized_text(Color::none, exit_debug_msg);
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
    auto maybe_vslang = get_environment_variable(EnvironmentVariableVsLang);
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

    reset_processor_architecture_environment_variable();
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
    set_environment_variable(EnvironmentVariableVcpkgCommand, get_exe_path_of_current_process().generic_u8string());

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

#if (defined(__arm__) || defined(__s390x__) || defined(__riscv) ||                                                     \
     ((defined(__ppc64__) || defined(__PPC64__) || defined(__ppc64le__) || defined(__PPC64LE__)) &&                    \
      defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)) ||                                       \
     defined(_M_ARM) || defined(_M_ARM64)) &&                                                                          \
    !defined(_WIN32) && !defined(__APPLE__)
    if (!get_environment_variable(EnvironmentVariableVcpkgForceSystemBinaries).has_value())
    {
        Checks::msg_exit_with_message(VCPKG_LINE_INFO, msgForceSystemBinariesOnWeirdPlatforms);
    }
#endif

    VcpkgCmdArguments args = VcpkgCmdArguments::create_from_command_line(real_filesystem, argc, argv);
    if (const auto p = args.debug.get()) Debug::g_debugging = *p;
    args.imbue_from_environment();
    VcpkgCmdArguments::imbue_or_apply_process_recursion(args);
    if (const auto p = args.debug_env.get(); p && *p)
    {
        msg::write_unlocalized_text(Color::none,
                                    "[DEBUG] The following environment variables are currently set:\n" +
                                        Strings::join("\n", get_environment_variables()) + '\n');
    }
    else if (Debug::g_debugging)
    {
        Debug::println("To include the environment variables in debug output, pass --debug-env");
    }
    args.check_feature_flag_consistency();
    const auto current_exe_path = get_exe_path_of_current_process();

    bool to_enable_metrics = true;
    {
        auto disable_metrics_tag_file_path = current_exe_path;
        disable_metrics_tag_file_path.replace_filename("vcpkg.disable-metrics");
        std::error_code ec;
        if (real_filesystem.exists(disable_metrics_tag_file_path, ec) || ec)
        {
            Debug::println("Disabling metrics because vcpkg.disable-metrics exists");
            to_enable_metrics = false;
        }
    }

    auto bundle_path = current_exe_path;
    bundle_path.replace_filename(FileVcpkgBundleDotJson);
    Debug::println("Trying to load bundleconfig from ", bundle_path);
    auto bundle =
        real_filesystem.try_read_contents(bundle_path).then(&try_parse_bundle_settings).value_or(BundleSettings{});
    Debug::println("Bundle config: ", bundle.to_string());

    if (to_enable_metrics)
    {
        if (auto p = args.disable_metrics.get())
        {
            if (*p)
            {
                Debug::println("Force disabling metrics with --disable-metrics");
                to_enable_metrics = false;
            }
            else
            {
                Debug::println("Force enabling metrics with --no-disable-metrics");
                to_enable_metrics = true;
            }
        }
#ifdef _WIN32
        else if (bundle.deployment == DeploymentKind::VisualStudio)
        {
            std::vector<std::string> opt_in_points;
            opt_in_points.push_back(R"(SOFTWARE\Policies\Microsoft\VisualStudio\SQM)");
            opt_in_points.push_back(R"(SOFTWARE\WOW6432Node\Policies\Microsoft\VisualStudio\SQM)");
            if (auto vsversion = bundle.vsversion.get())
            {
                opt_in_points.push_back(fmt::format(R"(SOFTWARE\Microsoft\VSCommon\{}\SQM)", *vsversion));
                opt_in_points.push_back(fmt::format(R"(SOFTWARE\WOW6432Node\Microsoft\VSCommon\{}\SQM)", *vsversion));
            }

            std::string* opted_in_at = nullptr;
            for (auto&& opt_in_point : opt_in_points)
            {
                if (get_registry_dword(HKEY_LOCAL_MACHINE, opt_in_point, "OptIn").value_or(0) != 0)
                {
                    opted_in_at = &opt_in_point;
                    break;
                }
            }

            if (opted_in_at)
            {
                Debug::println("VS telemetry opted in at ", *opted_in_at, R"(\\OptIn)");
            }
            else
            {
                Debug::println("VS telemetry not opted in, disabling metrics");
                to_enable_metrics = false;
            }
        }
#endif // _WIN32
    }

    if (to_enable_metrics)
    {
        g_metrics_enabled = true;
        Debug::println("Metrics enabled.");
        get_global_metrics_collector().track_string(StringMetric::DeploymentKind, to_string_literal(bundle.deployment));
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
        msg::write_unlocalized_text_to_stderr(
            Color::warning,
            LocalizedString::from_raw(WarningPrefix).append(msgVcpkgSendMetricsButDisabled).append_raw('\n'));
    }

    args.debug_print_feature_flags();
    args.track_feature_flag_metrics();
    args.track_environment_metrics();

    if (Debug::g_debugging)
    {
        inner(real_filesystem, args, bundle);
        Checks::exit_fail(VCPKG_LINE_INFO);
    }

    std::string exc_msg;
    try
    {
        inner(real_filesystem, args, bundle);
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
    auto data_blob = LocalizedString::from_raw(ErrorPrefix)
                         .append(msgVcpkgHasCrashed)
                         .append_raw("\nVersion=")
                         .append_raw(vcpkg_executable_version)
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

    msg::write_unlocalized_text_to_stderr(Color::none, data_blob);

    // It is expected that one of the sub-commands will exit cleanly before we get here.
    Checks::exit_fail(VCPKG_LINE_INFO);
}
