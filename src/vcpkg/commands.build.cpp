#include <vcpkg/base/cache.h>
#include <vcpkg/base/checks.h>
#include <vcpkg/base/chrono.h>
#include <vcpkg/base/file_sink.h>
#include <vcpkg/base/hash.h>
#include <vcpkg/base/message_sinks.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/optional.h>
#include <vcpkg/base/stringview.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.h>
#include <vcpkg/base/system.process.h>
#include <vcpkg/base/system.proxy.h>
#include <vcpkg/base/util.h>
#include <vcpkg/base/uuid.h>

#include <vcpkg/binarycaching.h>
#include <vcpkg/buildenvironment.h>
#include <vcpkg/cmakevars.h>
#include <vcpkg/commands.build.h>
#include <vcpkg/commands.version.h>
#include <vcpkg/dependencies.h>
#include <vcpkg/documentation.h>
#include <vcpkg/input.h>
#include <vcpkg/installedpaths.h>
#include <vcpkg/metrics.h>
#include <vcpkg/paragraphs.h>
#include <vcpkg/portfileprovider.h>
#include <vcpkg/postbuildlint.h>
#include <vcpkg/registries.h>
#include <vcpkg/spdx.h>
#include <vcpkg/statusparagraphs.h>
#include <vcpkg/tools.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkglib.h>
#include <vcpkg/vcpkgpaths.h>

using namespace vcpkg;

namespace
{
    struct NullBuildLogsRecorder final : IBuildLogsRecorder
    {
        void record_build_result(const VcpkgPaths& paths, const PackageSpec& spec, BuildResult result) const override
        {
            (void)paths;
            (void)spec;
            (void)result;
        }
    };

    static const NullBuildLogsRecorder null_build_logs_recorder_instance;
}

namespace vcpkg
{
    void command_build_and_exit_ex(const VcpkgCmdArguments& args,
                                   const FullPackageSpec& full_spec,
                                   Triplet host_triplet,
                                   const PathsPortFileProvider& provider,
                                   const IBuildLogsRecorder& build_logs_recorder,
                                   const VcpkgPaths& paths)
    {
        Checks::exit_with_code(VCPKG_LINE_INFO,
                               command_build_ex(args, full_spec, host_triplet, provider, build_logs_recorder, paths));
    }

    constexpr CommandMetadata CommandBuildMetadata{
        "build",
        msgCmdBuildSynopsis,
        {msgCmdBuildExample1, "vcpkg build zlib:x64-windows"},
        Undocumented,
        AutocompletePriority::Internal,
        1,
        1,
        {},
        nullptr,
    };

    void command_build_and_exit(const VcpkgCmdArguments& args,
                                const VcpkgPaths& paths,
                                Triplet default_triplet,
                                Triplet host_triplet)
    {
        // Build only takes a single package and all dependencies must already be installed
        const ParsedArguments options = args.parse_arguments(CommandBuildMetadata);
        bool default_triplet_used = false;
        const FullPackageSpec spec = check_and_get_full_package_spec(options.command_arguments[0],
                                                                     default_triplet,
                                                                     default_triplet_used,
                                                                     CommandBuildMetadata.get_example_text(),
                                                                     paths.get_triplet_db());
        if (default_triplet_used)
        {
            print_default_triplet_warning(args, paths.get_triplet_db());
        }

        auto& fs = paths.get_filesystem();
        auto registry_set = paths.make_registry_set();
        PathsPortFileProvider provider(
            fs, *registry_set, make_overlay_provider(fs, paths.original_cwd, paths.overlay_ports));
        Checks::exit_with_code(VCPKG_LINE_INFO,
                               command_build_ex(args, spec, host_triplet, provider, null_build_logs_recorder(), paths));
    }

    int command_build_ex(const VcpkgCmdArguments& args,
                         const FullPackageSpec& full_spec,
                         Triplet host_triplet,
                         const PathsPortFileProvider& provider,
                         const IBuildLogsRecorder& build_logs_recorder,
                         const VcpkgPaths& paths)
    {
        const PackageSpec& spec = full_spec.package_spec;
        auto var_provider_storage = CMakeVars::make_triplet_cmake_var_provider(paths);
        auto& var_provider = *var_provider_storage;
        var_provider.load_dep_info_vars({{spec}}, host_triplet);

        StatusParagraphs status_db = database_load_check(paths.get_filesystem(), paths.installed());
        auto action_plan = create_feature_install_plan(
            provider, var_provider, {&full_spec, 1}, status_db, {host_triplet, paths.packages()});

        var_provider.load_tag_vars(action_plan, host_triplet);

        compute_all_abis(paths, action_plan, var_provider, status_db);

        InstallPlanAction* action = nullptr;
        for (auto& install_action : action_plan.already_installed)
        {
            if (install_action.spec == full_spec.package_spec)
            {
                Checks::msg_exit_with_error(VCPKG_LINE_INFO, msgBuildAlreadyInstalled, msg::spec = spec);
            }
        }

        for (auto& install_action : action_plan.install_actions)
        {
            if (install_action.spec == full_spec.package_spec)
            {
                action = &install_action;
            }
        }

        Checks::check_exit(VCPKG_LINE_INFO, action != nullptr);
        ASSUME(action != nullptr);
        auto& scf = *action->source_control_file_and_location.value_or_exit(VCPKG_LINE_INFO).source_control_file;
        const auto& spec_name = spec.name();
        const auto& core_paragraph_name = scf.to_name();
        if (spec_name != core_paragraph_name)
        {
            Checks::msg_exit_with_error(VCPKG_LINE_INFO,
                                        msgSourceFieldPortNameMismatch,
                                        msg::package_name = core_paragraph_name,
                                        msg::path = spec_name);
        }

        action->build_options = default_build_package_options;
        action->build_options.editable = Editable::Yes;
        action->build_options.clean_buildtrees = CleanBuildtrees::No;
        action->build_options.clean_packages = CleanPackages::No;

        auto binary_cache = BinaryCache::make(args, paths, out_sink).value_or_exit(VCPKG_LINE_INFO);
        const ElapsedTimer build_timer;
        const auto result = build_package(args, paths, *action, build_logs_recorder, status_db);
        msg::print(msgElapsedForPackage, msg::spec = spec, msg::elapsed = build_timer);
        if (result.code == BuildResult::CascadedDueToMissingDependencies)
        {
            LocalizedString errorMsg = msg::format_error(msgBuildDependenciesMissing);
            for (const auto& p : result.unmet_dependencies)
            {
                errorMsg.append_raw('\n').append_indent().append_raw(p.to_string());
            }

            Checks::msg_exit_with_message(VCPKG_LINE_INFO, errorMsg);
        }

        Checks::check_exit(VCPKG_LINE_INFO, result.code != BuildResult::Excluded);

        if (result.code != BuildResult::Succeeded)
        {
            LocalizedString warnings;
            for (auto&& msg : action->build_failure_messages)
            {
                warnings.append(msg).append_raw('\n');
            }
            if (!warnings.data().empty())
            {
                msg::print(Color::warning, warnings);
            }
            msg::println_error(create_error_message(result, spec));
            msg::print(create_user_troubleshooting_message(*action, paths, nullopt));
            return 1;
        }
        binary_cache.push_success(*action);

        return 0;
    }

    static constexpr StringLiteral NAME_EMPTY_PACKAGE = "PolicyEmptyPackage";
    static constexpr StringLiteral NAME_DLLS_WITHOUT_LIBS = "PolicyDLLsWithoutLIBs";
    static constexpr StringLiteral NAME_DLLS_WITHOUT_EXPORTS = "PolicyDLLsWithoutExports";
    static constexpr StringLiteral NAME_DLLS_IN_STATIC_LIBRARY = "PolicyDLLsInStaticLibrary";
    static constexpr StringLiteral NAME_MISMATCHED_NUMBER_OF_BINARIES = "PolicyMismatchedNumberOfBinaries";
    static constexpr StringLiteral NAME_ONLY_RELEASE_CRT = "PolicyOnlyReleaseCRT";
    static constexpr StringLiteral NAME_EMPTY_INCLUDE_FOLDER = "PolicyEmptyIncludeFolder";
    static constexpr StringLiteral NAME_ALLOW_OBSOLETE_MSVCRT = "PolicyAllowObsoleteMsvcrt";
    static constexpr StringLiteral NAME_ALLOW_RESTRICTED_HEADERS = "PolicyAllowRestrictedHeaders";
    static constexpr StringLiteral NAME_SKIP_DUMPBIN_CHECKS = "PolicySkipDumpbinChecks";
    static constexpr StringLiteral NAME_SKIP_ARCHITECTURE_CHECK = "PolicySkipArchitectureCheck";
    static constexpr StringLiteral NAME_CMAKE_HELPER_PORT = "PolicyCmakeHelperPort";
    static constexpr StringLiteral NAME_SKIP_ABSOLUTE_PATHS_CHECK = "PolicySkipAbsolutePathsCheck";

    static std::remove_const_t<decltype(ALL_POLICIES)> generate_all_policies()
    {
        std::remove_const_t<decltype(ALL_POLICIES)> res{};
        for (size_t i = 0; i < res.size(); ++i)
        {
            res[i] = static_cast<BuildPolicy>(i);
        }

        return res;
    }

    decltype(ALL_POLICIES) ALL_POLICIES = generate_all_policies();

    StringLiteral to_string_view(BuildPolicy policy)
    {
        switch (policy)
        {
            case BuildPolicy::EMPTY_PACKAGE: return NAME_EMPTY_PACKAGE;
            case BuildPolicy::DLLS_WITHOUT_LIBS: return NAME_DLLS_WITHOUT_LIBS;
            case BuildPolicy::DLLS_WITHOUT_EXPORTS: return NAME_DLLS_WITHOUT_EXPORTS;
            case BuildPolicy::DLLS_IN_STATIC_LIBRARY: return NAME_DLLS_IN_STATIC_LIBRARY;
            case BuildPolicy::MISMATCHED_NUMBER_OF_BINARIES: return NAME_MISMATCHED_NUMBER_OF_BINARIES;
            case BuildPolicy::ONLY_RELEASE_CRT: return NAME_ONLY_RELEASE_CRT;
            case BuildPolicy::EMPTY_INCLUDE_FOLDER: return NAME_EMPTY_INCLUDE_FOLDER;
            case BuildPolicy::ALLOW_OBSOLETE_MSVCRT: return NAME_ALLOW_OBSOLETE_MSVCRT;
            case BuildPolicy::ALLOW_RESTRICTED_HEADERS: return NAME_ALLOW_RESTRICTED_HEADERS;
            case BuildPolicy::SKIP_DUMPBIN_CHECKS: return NAME_SKIP_DUMPBIN_CHECKS;
            case BuildPolicy::SKIP_ARCHITECTURE_CHECK: return NAME_SKIP_ARCHITECTURE_CHECK;
            case BuildPolicy::CMAKE_HELPER_PORT: return NAME_CMAKE_HELPER_PORT;
            case BuildPolicy::SKIP_ABSOLUTE_PATHS_CHECK: return NAME_SKIP_ABSOLUTE_PATHS_CHECK;
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }
    }
    std::string to_string(BuildPolicy policy) { return to_string_view(policy).to_string(); }

    ZStringView to_cmake_variable(BuildPolicy policy)
    {
        switch (policy)
        {
            case BuildPolicy::EMPTY_PACKAGE: return "VCPKG_POLICY_EMPTY_PACKAGE";
            case BuildPolicy::DLLS_WITHOUT_LIBS: return "VCPKG_POLICY_DLLS_WITHOUT_LIBS";
            case BuildPolicy::DLLS_WITHOUT_EXPORTS: return "VCPKG_POLICY_DLLS_WITHOUT_EXPORTS";
            case BuildPolicy::DLLS_IN_STATIC_LIBRARY: return "VCPKG_POLICY_DLLS_IN_STATIC_LIBRARY";
            case BuildPolicy::MISMATCHED_NUMBER_OF_BINARIES: return "VCPKG_POLICY_MISMATCHED_NUMBER_OF_BINARIES";
            case BuildPolicy::ONLY_RELEASE_CRT: return "VCPKG_POLICY_ONLY_RELEASE_CRT";
            case BuildPolicy::EMPTY_INCLUDE_FOLDER: return "VCPKG_POLICY_EMPTY_INCLUDE_FOLDER";
            case BuildPolicy::ALLOW_OBSOLETE_MSVCRT: return "VCPKG_POLICY_ALLOW_OBSOLETE_MSVCRT";
            case BuildPolicy::ALLOW_RESTRICTED_HEADERS: return "VCPKG_POLICY_ALLOW_RESTRICTED_HEADERS";
            case BuildPolicy::SKIP_DUMPBIN_CHECKS: return "VCPKG_POLICY_SKIP_DUMPBIN_CHECKS";
            case BuildPolicy::SKIP_ARCHITECTURE_CHECK: return "VCPKG_POLICY_SKIP_ARCHITECTURE_CHECK";
            case BuildPolicy::CMAKE_HELPER_PORT: return "VCPKG_POLICY_CMAKE_HELPER_PORT";
            case BuildPolicy::SKIP_ABSOLUTE_PATHS_CHECK: return "VCPKG_POLICY_SKIP_ABSOLUTE_PATHS_CHECK";
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }
    }

    static constexpr StringLiteral NAME_BUILTIN_DOWNLOAD = "BUILT_IN";
    static constexpr StringLiteral NAME_ARIA2_DOWNLOAD = "ARIA2";

    StringLiteral to_string_view(DownloadTool tool)
    {
        switch (tool)
        {
            case DownloadTool::Builtin: return NAME_BUILTIN_DOWNLOAD;
            case DownloadTool::Aria2: return NAME_ARIA2_DOWNLOAD;
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }
    }

    std::string to_string(DownloadTool tool) { return to_string_view(tool).to_string(); }

    Optional<LinkageType> to_linkage_type(StringView str)
    {
        if (str == "dynamic") return LinkageType::Dynamic;
        if (str == "static") return LinkageType::Static;
        return nullopt;
    }

    namespace BuildInfoRequiredField
    {
        static constexpr StringLiteral CRT_LINKAGE = "CRTLinkage";
        static constexpr StringLiteral LIBRARY_LINKAGE = "LibraryLinkage";
    }

#if defined(_WIN32)
    static ZStringView to_vcvarsall_target(StringView cmake_system_name)
    {
        if (cmake_system_name.empty()) return "";
        if (cmake_system_name == "Windows") return "";
        if (cmake_system_name == "WindowsStore") return "store";

        Checks::msg_exit_with_error(VCPKG_LINE_INFO, msgUnsupportedSystemName, msg::system_name = cmake_system_name);
    }

    static ZStringView to_vcvarsall_toolchain(StringView target_architecture, const Toolset& toolset, Triplet triplet)
    {
        auto maybe_target_arch = to_cpu_architecture(target_architecture);
        if (!maybe_target_arch.has_value())
        {
            msg::println_error(msgInvalidArchitecture, msg::value = target_architecture);
            Checks::exit_maybe_upgrade(VCPKG_LINE_INFO);
        }

        auto target_arch = maybe_target_arch.value_or_exit(VCPKG_LINE_INFO);
        // Ask for an arm64 compiler when targeting arm64ec; arm64ec is selected with a different flag on the compiler
        // command line.
        if (target_arch == CPUArchitecture::ARM64EC)
        {
            target_arch = CPUArchitecture::ARM64;
        }

        auto host_architectures = get_supported_host_architectures();
        for (auto&& host : host_architectures)
        {
            const auto it = Util::find_if(toolset.supported_architectures, [&](const ToolsetArchOption& opt) {
                return host == opt.host_arch && target_arch == opt.target_arch;
            });
            if (it != toolset.supported_architectures.end()) return it->name;
        }

        const auto toolset_list = Strings::join(
            ", ", toolset.supported_architectures, [](const ToolsetArchOption& t) { return t.name.c_str(); });

        msg::println_error(msgUnsupportedToolchain,
                           msg::triplet = triplet,
                           msg::arch = target_architecture,
                           msg::path = toolset.visual_studio_root_path,
                           msg::list = toolset_list);
        msg::println(msgSeeURL, msg::url = docs::vcpkg_visual_studio_path_url);
        Checks::exit_maybe_upgrade(VCPKG_LINE_INFO);
    }
#endif

#if defined(_WIN32)
    const Environment& EnvCache::get_action_env(const VcpkgPaths& paths,
                                                const PreBuildInfo& pre_build_info,
                                                const Toolset& toolset)
    {
        auto build_env_cmd = make_build_env_cmd(pre_build_info, toolset);
        const auto& base_env = envs.get_lazy(pre_build_info.passthrough_env_vars, [&]() -> EnvMapEntry {
            std::unordered_map<std::string, std::string> env;

            for (auto&& env_var : pre_build_info.passthrough_env_vars)
            {
                auto maybe_env_val = get_environment_variable(env_var);
                if (auto env_val = maybe_env_val.get())
                {
                    env[env_var] = std::move(*env_val);
                }
            }
            static constexpr StringLiteral s_extra_vars[] = {
                "VCPKG_COMMAND",
                "VCPKG_FORCE_SYSTEM_BINARIES",
                VcpkgCmdArguments::RECURSIVE_DATA_ENV,
            };

            for (const auto& var : s_extra_vars)
            {
                auto val = get_environment_variable(var);
                if (auto p_val = val.get()) env.emplace(var, *p_val);
            }

            /*
             * On Windows 10 (>= 8.1) it is a user-friendly way to automatically set HTTP_PROXY and HTTPS_PROXY
             * environment variables by reading proxy settings via WinHttpGetIEProxyConfigForCurrentUser, preventing
             * users set and unset these variables manually (which is not a decent way). It is common in China or
             * any other regions that needs an proxy software (v2ray, shadowsocks, etc.), which sets the IE Proxy
             * Settings, but not setting environment variables. This will make vcpkg easier to use, specially when
             * use vcpkg in Visual Studio, we even cannot set HTTP(S)_PROXY in CLI, if we want to open or close
             * Proxy we need to restart VS.
             */

            // 2021-05-09 Fix: Detect If there's already HTTP(S)_PROXY presented in the environment variables.
            // If so, we no longer overwrite them.
            bool proxy_from_env = (get_environment_variable("HTTP_PROXY").has_value() ||
                                   get_environment_variable("HTTPS_PROXY").has_value());

            if (proxy_from_env)
            {
                msg::println(msgUseEnvVar, msg::env_var = format_environment_variable("HTTP(S)_PROXY"));
            }
            else
            {
                auto ieProxy = get_windows_ie_proxy_server();
                if (ieProxy.has_value() && !proxy_from_env)
                {
                    std::string server = Strings::to_utf8(ieProxy.get()->server);

                    // Separate settings in IE Proxy Settings, which is rare?
                    // Python implementation:
                    // https://github.com/python/cpython/blob/7215d1ae25525c92b026166f9d5cac85fb1defe1/Lib/urllib/request.py#L2655
                    if (Strings::contains(server, "="))
                    {
                        auto proxy_settings = Strings::split(server, ';');
                        for (auto& s : proxy_settings)
                        {
                            auto kvp = Strings::split(s, '=');
                            if (kvp.size() == 2)
                            {
                                auto& protocol = kvp[0];
                                auto& address = kvp[1];

                                /* Unlike Python's urllib implementation about this type of proxy configuration
                                 * (http=addr:port;https=addr:port)
                                 * https://github.com/python/cpython/blob/7215d1ae25525c92b026166f9d5cac85fb1defe1/Lib/urllib/request.py#L2682
                                 * we do not intentionally append protocol prefix to address. Because HTTPS_PROXY's
                                 * address is not always an HTTPS proxy, an HTTP proxy can also proxy HTTPS requests
                                 * without end-to-end security (As an HTTP Proxy can see your cleartext while an
                                 * HTTPS proxy can't).
                                 *
                                 * If the prefix (http=http://addr:port;https=https://addr:port) already exists in
                                 * the address, we should consider this address points to an HTTPS proxy, and assign
                                 * to HTTPS_PROXY directly. However, if it doesn't exist, then we should NOT append
                                 * an `https://` prefix to an `addr:port` as it could be an HTTP proxy, and the
                                 * connection request will fail.
                                 */

                                protocol = Strings::concat(Strings::ascii_to_uppercase(protocol), "_PROXY");
                                env.emplace(protocol, address);
                                msg::println(msgSettingEnvVar,
                                             msg::env_var = format_environment_variable(protocol),
                                             msg::url = address);
                            }
                        }
                    }
                    // Specified http:// prefix
                    else if (Strings::starts_with(server, "http://"))
                    {
                        msg::println(msgSettingEnvVar,
                                     msg::env_var = format_environment_variable("HTTP_PROXY"),
                                     msg::url = server);
                        env.emplace("HTTP_PROXY", server);
                    }
                    // Specified https:// prefix
                    else if (Strings::starts_with(server, "https://"))
                    {
                        msg::println(msgSettingEnvVar,
                                     msg::env_var = format_environment_variable("HTTPS_PROXY"),
                                     msg::url = server);
                        env.emplace("HTTPS_PROXY", server);
                    }
                    // Most common case: "ip:port" style, apply to HTTP and HTTPS proxies.
                    // An HTTP(S)_PROXY means https requests go through that, it can be:
                    // http:// prefixed: the request go through an HTTP proxy without end-to-end security.
                    // https:// prefixed: the request go through an HTTPS proxy with end-to-end security.
                    // Nothing prefixed: don't know the default behaviour, seems considering HTTP proxy as default.
                    // We simply set "ip:port" to HTTP(S)_PROXY variables because it works on most common cases.
                    else
                    {
                        msg::println(msgAutoSettingEnvVar,
                                     msg::env_var = format_environment_variable("HTTP(S)_PROXY"),
                                     msg::url = server);

                        env.emplace("HTTP_PROXY", server.c_str());
                        env.emplace("HTTPS_PROXY", server.c_str());
                    }
                }
            }
            return {env};
        });

        return base_env.cmd_cache.get_lazy(build_env_cmd, [&]() {
            const Path& powershell_exe_path = paths.get_tool_exe("powershell-core", out_sink);
            auto clean_env = get_modified_clean_environment(base_env.env_map, powershell_exe_path.parent_path());
            if (build_env_cmd.empty())
                return clean_env;
            else
                return cmd_execute_and_capture_environment(build_env_cmd, clean_env);
        });
    }
#else
    const Environment& EnvCache::get_action_env(const VcpkgPaths&, const PreBuildInfo&, const Toolset&)
    {
        return get_clean_environment();
    }
#endif

    static CompilerInfo load_compiler_info(const VcpkgPaths& paths,
                                           const PreBuildInfo& pre_build_info,
                                           const Toolset& toolset);

    static const std::string& get_toolchain_cache(Cache<Path, std::string>& cache,
                                                  const Path& tcfile,
                                                  const ReadOnlyFilesystem& fs)
    {
        return cache.get_lazy(tcfile, [&]() {
            return Hash::get_file_hash(fs, tcfile, Hash::Algorithm::Sha256).value_or_exit(VCPKG_LINE_INFO);
        });
    }

    const EnvCache::TripletMapEntry& EnvCache::get_triplet_cache(const ReadOnlyFilesystem& fs, const Path& p) const
    {
        return m_triplet_cache.get_lazy(p, [&]() -> TripletMapEntry {
            return TripletMapEntry{Hash::get_file_hash(fs, p, Hash::Algorithm::Sha256).value_or_exit(VCPKG_LINE_INFO)};
        });
    }

    const CompilerInfo& EnvCache::get_compiler_info(const VcpkgPaths& paths,
                                                    const PreBuildInfo& pre_build_info,
                                                    const Toolset& toolset)
    {
        if (!m_compiler_tracking || pre_build_info.disable_compiler_tracking)
        {
            static CompilerInfo empty_ci;
            return empty_ci;
        }

        const auto& fs = paths.get_filesystem();

        const auto& triplet_file_path = paths.get_triplet_db().get_triplet_file_path(pre_build_info.triplet);

        auto&& toolchain_hash = get_toolchain_cache(m_toolchain_cache, pre_build_info.toolchain_file(), fs);

        auto&& triplet_entry = get_triplet_cache(fs, triplet_file_path);

        return triplet_entry.compiler_info.get_lazy(toolchain_hash, [&]() -> CompilerInfo {
            if (m_compiler_tracking)
            {
                return load_compiler_info(paths, pre_build_info, toolset);
            }
            else
            {
                return CompilerInfo{};
            }
        });
    }

    const std::string& EnvCache::get_triplet_info(const VcpkgPaths& paths,
                                                  const PreBuildInfo& pre_build_info,
                                                  const Toolset& toolset)
    {
        const auto& fs = paths.get_filesystem();
        const auto& triplet_file_path = paths.get_triplet_db().get_triplet_file_path(pre_build_info.triplet);

        auto&& toolchain_hash = get_toolchain_cache(m_toolchain_cache, pre_build_info.toolchain_file(), fs);

        auto&& triplet_entry = get_triplet_cache(fs, triplet_file_path);

        if (m_compiler_tracking && !pre_build_info.disable_compiler_tracking)
        {
            return triplet_entry.triplet_infos.get_lazy(toolchain_hash, [&]() -> std::string {
                auto& compiler_info = get_compiler_info(paths, pre_build_info, toolset);
                return Strings::concat(triplet_entry.hash, '-', toolchain_hash, '-', compiler_info.hash);
            });
        }
        else
        {
            return triplet_entry.triplet_infos_without_compiler.get_lazy(toolchain_hash, [&]() -> std::string {
                return Strings::concat(triplet_entry.hash, '-', toolchain_hash);
            });
        }
    }

    vcpkg::Command make_build_env_cmd(const PreBuildInfo& pre_build_info, const Toolset& toolset)
    {
        if (!pre_build_info.using_vcvars()) return {};

#if !defined(WIN32)
        // pre_build_info.using_vcvars() should always be false on non-Win32 hosts.
        // If it was true, we should have failed earlier while selecting a Toolset
        (void)toolset;
        Checks::unreachable(VCPKG_LINE_INFO);
#else

        const char* tonull = " >nul";
        if (Debug::g_debugging)
        {
            tonull = "";
        }

        const auto arch = to_vcvarsall_toolchain(pre_build_info.target_architecture, toolset, pre_build_info.triplet);
        const auto target = to_vcvarsall_target(pre_build_info.cmake_system_name);

        return vcpkg::Command{"cmd"}.string_arg("/d").string_arg("/c").raw_arg(
            fmt::format(R"("{}" {} {} {} {} 2>&1 <NUL)",
                        toolset.vcvarsall,
                        Strings::join(" ", toolset.vcvarsall_options),
                        arch,
                        target,
                        tonull));
#endif
    }

    static std::vector<PackageSpec> fspecs_to_pspecs(View<FeatureSpec> fspecs)
    {
        std::set<PackageSpec> set;
        for (auto&& f : fspecs)
            set.insert(f.spec());
        std::vector<PackageSpec> ret{set.begin(), set.end()};
        return ret;
    }

    static std::unique_ptr<BinaryControlFile> create_binary_control_file(const InstallPlanAction& action,
                                                                         const BuildInfo& build_info)
    {
        const auto& scfl = action.source_control_file_and_location.value_or_exit(VCPKG_LINE_INFO);

        auto bcf = std::make_unique<BinaryControlFile>();

        auto find_itr = action.feature_dependencies.find("core");
        Checks::check_exit(VCPKG_LINE_INFO, find_itr != action.feature_dependencies.end());
        BinaryParagraph bpgh(*scfl.source_control_file->core_paragraph,
                             action.default_features.value_or_exit(VCPKG_LINE_INFO),
                             action.spec.triplet(),
                             action.public_abi(),
                             fspecs_to_pspecs(find_itr->second));
        if (const auto p_ver = build_info.detected_head_version.get())
        {
            bpgh.version = *p_ver;
        }
        bcf->core_paragraph = std::move(bpgh);

        bcf->features.reserve(action.feature_list.size());
        for (auto&& feature : action.feature_list)
        {
            find_itr = action.feature_dependencies.find(feature);
            Checks::check_exit(VCPKG_LINE_INFO, find_itr != action.feature_dependencies.end());
            auto maybe_fpgh = scfl.source_control_file->find_feature(feature);
            if (auto fpgh = maybe_fpgh.get())
            {
                bcf->features.emplace_back(action.spec, *fpgh, fspecs_to_pspecs(find_itr->second));
            }
        }
        return bcf;
    }

    static void write_binary_control_file(const Filesystem& fs, const Path& package_dir, const BinaryControlFile& bcf)
    {
        std::string start = Strings::serialize(bcf.core_paragraph);
        for (auto&& feature : bcf.features)
        {
            start.push_back('\n');
            start += Strings::serialize(feature);
        }
        const auto binary_control_file = package_dir / "CONTROL";
        fs.write_contents(binary_control_file, start, VCPKG_LINE_INFO);
    }

    static void get_generic_cmake_build_args(const VcpkgPaths& paths,
                                             Triplet triplet,
                                             const Toolset& toolset,
                                             std::vector<CMakeVariable>& out_vars)
    {
        out_vars.emplace_back("CMD", "BUILD");
        out_vars.emplace_back("DOWNLOADS", paths.downloads);
        out_vars.emplace_back("TARGET_TRIPLET", triplet.canonical_name());
        out_vars.emplace_back("TARGET_TRIPLET_FILE", paths.get_triplet_db().get_triplet_file_path(triplet));
        out_vars.emplace_back("VCPKG_BASE_VERSION", VCPKG_BASE_VERSION_AS_STRING);
        out_vars.emplace_back("VCPKG_CONCURRENCY", std::to_string(get_concurrency()));
        out_vars.emplace_back("VCPKG_PLATFORM_TOOLSET", toolset.version);
        // Make sure GIT could be found
        out_vars.emplace_back("GIT", paths.get_tool_exe(Tools::GIT, out_sink));
    }

    static CompilerInfo load_compiler_info(const VcpkgPaths& paths,
                                           const PreBuildInfo& pre_build_info,
                                           const Toolset& toolset)
    {
        auto& triplet = pre_build_info.triplet;
        msg::println(msgDetectCompilerHash, msg::triplet = triplet);
        auto buildpath = paths.buildtrees() / "detect_compiler";

        std::vector<CMakeVariable> cmake_args{
            {"CURRENT_PORT_DIR", paths.scripts / "detect_compiler"},
            {"CURRENT_BUILDTREES_DIR", buildpath},
            {"CURRENT_PACKAGES_DIR", paths.packages() / ("detect_compiler_" + triplet.canonical_name())},
            // The detect_compiler "port" doesn't depend on the host triplet, so always natively compile
            {"_HOST_TRIPLET", triplet.canonical_name()},
        };
        get_generic_cmake_build_args(paths, triplet, toolset, cmake_args);

        auto cmd = vcpkg::make_cmake_cmd(paths, paths.ports_cmake, std::move(cmake_args));
        RedirectedProcessLaunchSettings settings;
        settings.environment.emplace(paths.get_action_env(pre_build_info, toolset));
        auto& fs = paths.get_filesystem();
        fs.create_directory(buildpath, VCPKG_LINE_INFO);
        auto stdoutlog = buildpath / ("stdout-" + triplet.canonical_name() + ".log");
        CompilerInfo compiler_info;
        std::string buf;

        ExpectedL<int> rc = LocalizedString();
        {
            const auto out_file = fs.open_for_write(stdoutlog, VCPKG_LINE_INFO);
            rc = cmd_execute_and_stream_lines(cmd, settings, [&](StringView s) {
                static constexpr StringLiteral s_hash_marker = "#COMPILER_HASH#";
                if (Strings::starts_with(s, s_hash_marker))
                {
                    compiler_info.hash = s.substr(s_hash_marker.size()).to_string();
                }
                static constexpr StringLiteral s_version_marker = "#COMPILER_CXX_VERSION#";
                if (Strings::starts_with(s, s_version_marker))
                {
                    compiler_info.version = s.substr(s_version_marker.size()).to_string();
                }
                static constexpr StringLiteral s_id_marker = "#COMPILER_CXX_ID#";
                if (Strings::starts_with(s, s_id_marker))
                {
                    compiler_info.id = s.substr(s_id_marker.size()).to_string();
                }
                Debug::println(s);
                const auto old_buf_size = buf.size();
                Strings::append(buf, s, '\n');
                const auto write_size = buf.size() - old_buf_size;
                Checks::msg_check_exit(VCPKG_LINE_INFO,
                                       out_file.write(buf.c_str() + old_buf_size, 1, write_size) == write_size,
                                       msgErrorWhileWriting,
                                       msg::path = stdoutlog);
            });
        } // close out_file

        if (compiler_info.hash.empty() || !succeeded(rc))
        {
            Debug::println("Compiler information tracking can be disabled by passing --",
                           VcpkgCmdArguments::FEATURE_FLAGS_ARG,
                           "=-",
                           VcpkgCmdArguments::COMPILER_TRACKING_FEATURE);

            msg::println_error(msgErrorDetectingCompilerInfo, msg::path = stdoutlog);
            msg::write_unlocalized_text(Color::none, buf);
            Checks::msg_exit_with_error(VCPKG_LINE_INFO, msgErrorUnableToDetectCompilerInfo);
        }

        Debug::println("Detected compiler hash for triplet ", triplet, ": ", compiler_info.hash);
        return compiler_info;
    }

    static std::vector<CMakeVariable> get_cmake_build_args(const VcpkgCmdArguments& args,
                                                           const VcpkgPaths& paths,
                                                           const InstallPlanAction& action)
    {
        auto& scfl = action.source_control_file_and_location.value_or_exit(VCPKG_LINE_INFO);
        auto& scf = *scfl.source_control_file;
        auto& port_name = scf.to_name();

        std::string all_features;
        for (auto& feature : scf.feature_paragraphs)
        {
            all_features.append(feature->name + ";");
        }

        std::vector<CMakeVariable> variables{
            {"ALL_FEATURES", all_features},
            {"CURRENT_PORT_DIR", scfl.port_directory()},
            {"_HOST_TRIPLET", action.host_triplet.canonical_name()},
            {"FEATURES", Strings::join(";", action.feature_list)},
            {"PORT", port_name},
            {"VERSION", scf.to_version().text},
            {"VCPKG_USE_HEAD_VERSION", Util::Enum::to_bool(action.build_options.use_head_version) ? "1" : "0"},
            {"_VCPKG_DOWNLOAD_TOOL", to_string_view(action.build_options.download_tool)},
            {"_VCPKG_EDITABLE", Util::Enum::to_bool(action.build_options.editable) ? "1" : "0"},
            {"_VCPKG_NO_DOWNLOADS", !Util::Enum::to_bool(action.build_options.allow_downloads) ? "1" : "0"},
            {"Z_VCPKG_CHAINLOAD_TOOLCHAIN_FILE", action.pre_build_info(VCPKG_LINE_INFO).toolchain_file()},
        };

        if (action.build_options.download_tool == DownloadTool::Aria2)
        {
            variables.emplace_back("ARIA2", paths.get_tool_exe(Tools::ARIA2, out_sink));
        }

        if (auto cmake_debug = args.cmake_debug.get())
        {
            if (cmake_debug->is_port_affected(port_name))
            {
                variables.emplace_back("--debugger");
                variables.emplace_back(fmt::format("--debugger-pipe={}", cmake_debug->value));
            }
        }

        if (auto cmake_configure_debug = args.cmake_configure_debug.get())
        {
            if (cmake_configure_debug->is_port_affected(port_name))
            {
                variables.emplace_back(fmt::format("-DVCPKG_CMAKE_CONFIGURE_OPTIONS=--debugger;--debugger-pipe={}",
                                                   cmake_configure_debug->value));
            }
        }

        for (const auto& cmake_arg : args.cmake_args)
        {
            variables.emplace_back(cmake_arg);
        }

        if (action.build_options.backcompat_features == BackcompatFeatures::Prohibit)
        {
            variables.emplace_back("_VCPKG_PROHIBIT_BACKCOMPAT_FEATURES", "1");
        }

        get_generic_cmake_build_args(
            paths,
            action.spec.triplet(),
            action.abi_info.value_or_exit(VCPKG_LINE_INFO).toolset.value_or_exit(VCPKG_LINE_INFO),
            variables);

        if (Util::Enum::to_bool(action.build_options.only_downloads))
        {
            variables.emplace_back("VCPKG_DOWNLOAD_MODE", "true");
        }

        const ReadOnlyFilesystem& fs = paths.get_filesystem();

        std::vector<std::string> port_configs;
        for (const PackageSpec& dependency : action.package_dependencies)
        {
            Path port_config_path = paths.installed().vcpkg_port_config_cmake(dependency);

            if (fs.is_regular_file(port_config_path))
            {
                port_configs.emplace_back(std::move(port_config_path).native());
            }
        }

        if (!port_configs.empty())
        {
            variables.emplace_back("VCPKG_PORT_CONFIGS", Strings::join(";", port_configs));
        }

        return variables;
    }

    bool PreBuildInfo::using_vcvars() const
    {
        return (!external_toolchain_file.has_value() || load_vcvars_env) &&
               (cmake_system_name.empty() || cmake_system_name == "WindowsStore");
    }

    Path PreBuildInfo::toolchain_file() const
    {
        if (auto p = external_toolchain_file.get())
        {
            return *p;
        }
        else if (cmake_system_name == "Linux")
        {
            return m_paths.scripts / "toolchains/linux.cmake";
        }
        else if (cmake_system_name == "Darwin")
        {
            return m_paths.scripts / "toolchains/osx.cmake";
        }
        else if (cmake_system_name == "FreeBSD")
        {
            return m_paths.scripts / "toolchains/freebsd.cmake";
        }
        else if (cmake_system_name == "OpenBSD")
        {
            return m_paths.scripts / "toolchains/openbsd.cmake";
        }
        else if (cmake_system_name == "Android")
        {
            return m_paths.scripts / "toolchains/android.cmake";
        }
        else if (cmake_system_name == "iOS")
        {
            return m_paths.scripts / "toolchains/ios.cmake";
        }
        else if (cmake_system_name == "MinGW")
        {
            return m_paths.scripts / "toolchains/mingw.cmake";
        }
        else if (cmake_system_name == "WindowsStore")
        {
            return m_paths.scripts / "toolchains/uwp.cmake";
        }
        else if (target_is_xbox)
        {
            return m_paths.scripts / "toolchains/xbox.cmake";
        }
        else if (cmake_system_name.empty() || cmake_system_name == "Windows")
        {
            return m_paths.scripts / "toolchains/windows.cmake";
        }
        else
        {
            Checks::msg_exit_maybe_upgrade(VCPKG_LINE_INFO,
                                           msgUndeterminedToolChainForTriplet,
                                           msg::triplet = triplet,
                                           msg::system_name = cmake_system_name);
        }
    }

    static void write_sbom(const VcpkgPaths& paths,
                           const InstallPlanAction& action,
                           std::vector<Json::Value> heuristic_resources)
    {
        auto& fs = paths.get_filesystem();
        const auto& scfl = action.source_control_file_and_location.value_or_exit(VCPKG_LINE_INFO);
        const auto& scf = *scfl.source_control_file;

        auto doc_ns = Strings::concat("https://spdx.org/spdxdocs/",
                                      scf.to_name(),
                                      '-',
                                      action.spec.triplet(),
                                      '-',
                                      scf.to_version(),
                                      '-',
                                      generate_random_UUID());

        const auto now = CTime::now_string();
        const auto& abi = action.abi_info.value_or_exit(VCPKG_LINE_INFO);

        const auto json_path =
            action.package_dir.value_or_exit(VCPKG_LINE_INFO) / "share" / action.spec.name() / "vcpkg.spdx.json";
        fs.write_contents_and_dirs(
            json_path,
            create_spdx_sbom(
                action, abi.relative_port_files, abi.relative_port_hashes, now, doc_ns, std::move(heuristic_resources)),
            VCPKG_LINE_INFO);
    }

    static ExtendedBuildResult do_build_package(const VcpkgCmdArguments& args,
                                                const VcpkgPaths& paths,
                                                const InstallPlanAction& action,
                                                bool all_dependencies_satisfied)
    {
        const auto& pre_build_info = action.pre_build_info(VCPKG_LINE_INFO);

        auto& fs = paths.get_filesystem();
        auto&& scfl = action.source_control_file_and_location.value_or_exit(VCPKG_LINE_INFO);

        Triplet triplet = action.spec.triplet();
        const auto& triplet_db = paths.get_triplet_db();
        const auto& triplet_file_path = triplet_db.get_triplet_file_path(triplet);

        if (Strings::starts_with(triplet_file_path, triplet_db.community_triplet_directory))
        {
            msg::println_warning(msgUsingCommunityTriplet, msg::triplet = triplet.canonical_name());
            msg::println(msgLoadingCommunityTriplet, msg::path = triplet_file_path);
        }
        else if (!Strings::starts_with(triplet_file_path, triplet_db.default_triplet_directory))
        {
            msg::println(msgLoadingOverlayTriplet, msg::path = triplet_file_path);
        }

        if (!Strings::starts_with(scfl.control_path, paths.builtin_ports_directory()))
        {
            msg::println(msgInstallingFromLocation, msg::path = scfl.port_directory());
        }

        const auto& abi_info = action.abi_info.value_or_exit(VCPKG_LINE_INFO);

        const ElapsedTimer timer;
        auto cmd = vcpkg::make_cmake_cmd(paths, paths.ports_cmake, get_cmake_build_args(args, paths, action));

        RedirectedProcessLaunchSettings settings;
        auto& env = settings.environment.emplace(
            paths.get_action_env(*abi_info.pre_build_info, abi_info.toolset.value_or_exit(VCPKG_LINE_INFO)));

        auto buildpath = paths.build_dir(action.spec);
        fs.create_directory(buildpath, VCPKG_LINE_INFO);
        env.add_entry("GIT_CEILING_DIRECTORIES", fs.absolute(buildpath.parent_path(), VCPKG_LINE_INFO));
        auto stdoutlog = buildpath / ("stdout-" + action.spec.triplet().canonical_name() + ".log");
        ExpectedL<int> return_code = LocalizedString();
        {
            auto out_file = fs.open_for_write(stdoutlog, VCPKG_LINE_INFO);
            return_code = cmd_execute_and_stream_data(cmd, settings, [&](StringView sv) {
                msg::write_unlocalized_text(Color::none, sv);
                Checks::msg_check_exit(VCPKG_LINE_INFO,
                                       out_file.write(sv.data(), 1, sv.size()) == sv.size(),
                                       msgErrorWhileWriting,
                                       msg::path = stdoutlog);
            });
        } // close out_file

        const auto buildtimeus = timer.microseconds();
        const auto spec_string = action.spec.to_string();
        const bool build_failed = !succeeded(return_code);
        MetricsSubmission metrics;
        if (build_failed)
        {
            // With the exception of empty or helper ports, builds in "Download Mode" result in failure.
            if (action.build_options.only_downloads == OnlyDownloads::Yes)
            {
                // TODO: Capture executed command output and evaluate whether the failure was intended.
                // If an unintended error occurs then return a BuildResult::DOWNLOAD_FAILURE status.
                return ExtendedBuildResult{BuildResult::Downloaded};
            }
        }

        metrics.track_buildtime(Hash::get_string_hash(spec_string, Hash::Algorithm::Sha256) + ":[" +
                                    Strings::join(",",
                                                  action.feature_list,
                                                  [](const std::string& feature) {
                                                      return Hash::get_string_hash(feature, Hash::Algorithm::Sha256);
                                                  }) +
                                    "]",
                                buildtimeus);

        get_global_metrics_collector().track_submission(std::move(metrics));
        if (!all_dependencies_satisfied)
        {
            return ExtendedBuildResult{BuildResult::Downloaded};
        }

        if (build_failed)
        {
            const auto logs = buildpath / Strings::concat("error-logs-", action.spec.triplet(), ".txt");
            std::vector<std::string> error_logs;
            if (fs.exists(logs, VCPKG_LINE_INFO))
            {
                error_logs = fs.read_lines(logs).value_or_exit(VCPKG_LINE_INFO);
                Util::erase_remove_if(error_logs, [](const auto& line) { return line.empty(); });
            }
            return ExtendedBuildResult{BuildResult::BuildFailed, stdoutlog, std::move(error_logs)};
        }

        const BuildInfo build_info = read_build_info(fs, paths.build_info_file_path(action.spec));
        size_t error_count = 0;
        {
            FileSink file_sink{fs, stdoutlog, Append::YES};
            CombiningSink combo_sink{out_sink, file_sink};
            error_count = perform_post_build_lint_checks(
                action.spec, paths, pre_build_info, build_info, scfl.port_directory(), combo_sink);
        };
        if (error_count != 0 && action.build_options.backcompat_features == BackcompatFeatures::Prohibit)
        {
            return ExtendedBuildResult{BuildResult::PostBuildChecksFailed};
        }

        std::unique_ptr<BinaryControlFile> bcf = create_binary_control_file(action, build_info);

        write_sbom(paths, action, abi_info.heuristic_resources);
        write_binary_control_file(paths.get_filesystem(), action.package_dir.value_or_exit(VCPKG_LINE_INFO), *bcf);
        return {BuildResult::Succeeded, std::move(bcf)};
    }

    static ExtendedBuildResult do_build_package_and_clean_buildtrees(const VcpkgCmdArguments& args,
                                                                     const VcpkgPaths& paths,
                                                                     const InstallPlanAction& action,
                                                                     bool all_dependencies_satisfied)
    {
        auto result = do_build_package(args, paths, action, all_dependencies_satisfied);

        if (action.build_options.clean_buildtrees == CleanBuildtrees::Yes)
        {
            auto& fs = paths.get_filesystem();
            // Will keep the logs, which are regular files
            auto buildtree_dirs = fs.get_directories_non_recursive(paths.build_dir(action.spec), IgnoreErrors{});
            for (auto&& dir : buildtree_dirs)
            {
                fs.remove_all(dir, IgnoreErrors{});
            }
        }

        return result;
    }

    static std::string grdk_hash(const Filesystem& fs,
                                 Cache<Path, Optional<std::string>>& grdk_cache,
                                 const PreBuildInfo& pre_build_info)
    {
        if (auto game_dk_latest = pre_build_info.gamedk_latest_path.get())
        {
            const auto grdk_header_path = *game_dk_latest / "GRDK/gameKit/Include/grdk.h";
            const auto& maybe_header_hash = grdk_cache.get_lazy(grdk_header_path, [&]() -> Optional<std::string> {
                auto maybe_hash = Hash::get_file_hash(fs, grdk_header_path, Hash::Algorithm::Sha256);
                if (auto hash = maybe_hash.get())
                {
                    return std::move(*hash);
                }
                else
                {
                    return nullopt;
                }
            });

            if (auto header_hash = maybe_header_hash.get())
            {
                return *header_hash;
            }
        }

        return "none";
    }

    static void abi_entries_from_pre_build_info(const Filesystem& fs,
                                                Cache<Path, Optional<std::string>>& grdk_cache,
                                                const PreBuildInfo& pre_build_info,
                                                std::vector<AbiEntry>& abi_tag_entries)
    {
        if (pre_build_info.public_abi_override)
        {
            abi_tag_entries.emplace_back(
                "public_abi_override",
                Hash::get_string_hash(pre_build_info.public_abi_override.value_or_exit(VCPKG_LINE_INFO),
                                      Hash::Algorithm::Sha256));
        }

        for (const auto& env_var : pre_build_info.passthrough_env_vars_tracked)
        {
            if (auto e = get_environment_variable(env_var))
            {
                abi_tag_entries.emplace_back(
                    "ENV:" + env_var, Hash::get_string_hash(e.value_or_exit(VCPKG_LINE_INFO), Hash::Algorithm::Sha256));
            }
        }

        if (pre_build_info.target_is_xbox)
        {
            abi_tag_entries.emplace_back("grdk.h", grdk_hash(fs, grdk_cache, pre_build_info));
        }
    }

    static void populate_abi_tag(const VcpkgPaths& paths,
                                 InstallPlanAction& action,
                                 std::unique_ptr<PreBuildInfo>&& proto_pre_build_info,
                                 Span<const AbiEntry> dependency_abis,
                                 Cache<Path, Optional<std::string>>& grdk_cache)
    {
        Checks::check_exit(VCPKG_LINE_INFO, static_cast<bool>(proto_pre_build_info));
        const auto& pre_build_info = *proto_pre_build_info;
        const auto& toolset = paths.get_toolset(pre_build_info);
        auto& abi_info = action.abi_info.emplace();
        abi_info.pre_build_info = std::move(proto_pre_build_info);
        abi_info.toolset.emplace(toolset);

        if (action.build_options.use_head_version == UseHeadVersion::Yes)
        {
            Debug::print("Binary caching for package ", action.spec, " is disabled due to --head\n");
            return;
        }
        if (action.build_options.editable == Editable::Yes)
        {
            Debug::print("Binary caching for package ", action.spec, " is disabled due to --editable\n");
            return;
        }

        abi_info.compiler_info = paths.get_compiler_info(*abi_info.pre_build_info, toolset);
        for (auto&& dep_abi : dependency_abis)
        {
            if (dep_abi.value.empty())
            {
                Debug::print("Binary caching for package ",
                             action.spec,
                             " is disabled due to missing abi info for ",
                             dep_abi.key,
                             '\n');
                return;
            }
        }

        std::vector<AbiEntry> abi_tag_entries(dependency_abis.begin(), dependency_abis.end());

        const auto& triplet_abi = paths.get_triplet_info(pre_build_info, toolset);
        abi_info.triplet_abi.emplace(triplet_abi);
        const auto& triplet_canonical_name = action.spec.triplet().canonical_name();
        abi_tag_entries.emplace_back("triplet", triplet_canonical_name);
        abi_tag_entries.emplace_back("triplet_abi", triplet_abi);
        auto& fs = paths.get_filesystem();
        abi_entries_from_pre_build_info(fs, grdk_cache, pre_build_info, abi_tag_entries);

        // If there is an unusually large number of files in the port then
        // something suspicious is going on.
        constexpr int max_port_file_count = 100;

        std::string portfile_cmake_contents;
        auto&& scfl = action.source_control_file_and_location.value_or_exit(VCPKG_LINE_INFO);
        auto port_dir = scfl.port_directory();
        auto raw_files = fs.get_regular_files_recursive_lexically_proximate(port_dir, VCPKG_LINE_INFO);
        if (raw_files.size() > max_port_file_count)
        {
            msg::println_warning(
                msgHashPortManyFiles, msg::package_name = action.spec.name(), msg::count = raw_files.size());
        }

        std::vector<Path> files;         // will be port_files without .DS_Store entries
        std::vector<std::string> hashes; // will be corresponding hashes
        for (auto& port_file : raw_files)
        {
            if (port_file.filename() == ".DS_Store")
            {
                continue;
            }

            const auto& abs_port_file = files.emplace_back(port_dir / port_file);
            if (port_file.extension() == ".cmake")
            {
                auto contents = fs.read_contents(abs_port_file, VCPKG_LINE_INFO);
                portfile_cmake_contents += contents;
                hashes.push_back(vcpkg::Hash::get_string_sha256(contents));
            }
            else
            {
                hashes.push_back(vcpkg::Hash::get_file_hash(fs, abs_port_file, Hash::Algorithm::Sha256)
                                     .value_or_exit(VCPKG_LINE_INFO));
            }

            abi_tag_entries.emplace_back(port_file, hashes.back());
        }

        abi_tag_entries.emplace_back("cmake", paths.get_tool_version(Tools::CMAKE, out_sink));

        // This #ifdef is mirrored in tools.cpp's PowershellProvider
#if defined(_WIN32)
        abi_tag_entries.emplace_back("powershell", paths.get_tool_version("powershell-core", out_sink));
#endif

        auto& helpers = paths.get_cmake_script_hashes();
        for (auto&& helper : helpers)
        {
            if (Strings::case_insensitive_ascii_contains(portfile_cmake_contents, helper.first))
            {
                abi_tag_entries.emplace_back(helper.first, helper.second);
            }
        }

        abi_tag_entries.emplace_back("ports.cmake", paths.get_ports_cmake_hash().to_string());
        abi_tag_entries.emplace_back("post_build_checks", "2");
        InternalFeatureSet sorted_feature_list = action.feature_list;
        // Check that no "default" feature is present. Default features must be resolved before attempting to calculate
        // a package ABI, so the "default" should not have made it here.
        static constexpr StringLiteral default_literal{"default"};
        const bool has_no_pseudo_features = std::none_of(
            sorted_feature_list.begin(), sorted_feature_list.end(), [](StringView s) { return s == default_literal; });
        Checks::check_exit(VCPKG_LINE_INFO, has_no_pseudo_features);
        Util::sort_unique_erase(sorted_feature_list);

        // Check that the "core" feature is present. After resolution into InternalFeatureSet "core" meaning "not
        // default" should have already been handled so "core" should be here.
        Checks::check_exit(
            VCPKG_LINE_INFO,
            std::binary_search(sorted_feature_list.begin(), sorted_feature_list.end(), StringLiteral{"core"}));

        abi_tag_entries.emplace_back("features", Strings::join(";", sorted_feature_list));

        Util::sort(abi_tag_entries);

        const std::string full_abi_info =
            Strings::join("", abi_tag_entries, [](const AbiEntry& p) { return p.key + " " + p.value + "\n"; });

        if (Debug::g_debugging)
        {
            std::string message = Strings::concat("[DEBUG] <abientries for ", action.spec, ">\n");
            for (auto&& entry : abi_tag_entries)
            {
                Strings::append(message, "[DEBUG]   ", entry.key, "|", entry.value, "\n");
            }
            Strings::append(message, "[DEBUG] </abientries>\n");
            msg::write_unlocalized_text(Color::none, message);
        }

        auto abi_tag_entries_missing = Util::filter(abi_tag_entries, [](const AbiEntry& p) { return p.value.empty(); });
        if (!abi_tag_entries_missing.empty())
        {
            Debug::println("Warning: abi keys are missing values:\n",
                           Strings::join("\n", abi_tag_entries_missing, [](const AbiEntry& e) -> const std::string& {
                               return e.key;
                           }));
            return;
        }

        auto abi_file_path = paths.build_dir(action.spec);
        fs.create_directory(abi_file_path, VCPKG_LINE_INFO);
        abi_file_path /= triplet_canonical_name + ".vcpkg_abi_info.txt";
        fs.write_contents(abi_file_path, full_abi_info, VCPKG_LINE_INFO);

        auto& scf = scfl.source_control_file;
        abi_info.package_abi = Hash::get_string_sha256(full_abi_info);
        abi_info.abi_tag_file.emplace(std::move(abi_file_path));
        abi_info.relative_port_files = std::move(files);
        abi_info.relative_port_hashes = std::move(hashes);
        abi_info.heuristic_resources.push_back(
            run_resource_heuristics(portfile_cmake_contents, scf->core_paragraph->version.text));
    }

    void compute_all_abis(const VcpkgPaths& paths,
                          ActionPlan& action_plan,
                          const CMakeVars::CMakeVarProvider& var_provider,
                          const StatusParagraphs& status_db)
    {
        Cache<Path, Optional<std::string>> grdk_cache;
        for (auto it = action_plan.install_actions.begin(); it != action_plan.install_actions.end(); ++it)
        {
            auto& action = *it;
            if (action.abi_info.has_value()) continue;

            std::vector<AbiEntry> dependency_abis;
            if (!Util::Enum::to_bool(action.build_options.only_downloads))
            {
                for (auto&& pspec : action.package_dependencies)
                {
                    if (pspec == action.spec) continue;

                    auto pred = [&](const InstallPlanAction& ipa) { return ipa.spec == pspec; };
                    auto it2 = std::find_if(action_plan.install_actions.begin(), it, pred);
                    if (it2 == it)
                    {
                        // Finally, look in current installed
                        auto status_it = status_db.find(pspec);
                        if (status_it == status_db.end())
                        {
                            Checks::unreachable(
                                VCPKG_LINE_INFO,
                                fmt::format("Failed to find dependency abi for {} -> {}", action.spec, pspec));
                        }

                        dependency_abis.emplace_back(pspec.name(), status_it->get()->package.abi);
                    }
                    else
                    {
                        dependency_abis.emplace_back(pspec.name(), it2->public_abi());
                    }
                }
            }

            populate_abi_tag(
                paths,
                action,
                std::make_unique<PreBuildInfo>(paths,
                                               action.spec.triplet(),
                                               var_provider.get_tag_vars(action.spec).value_or_exit(VCPKG_LINE_INFO)),
                dependency_abis,
                grdk_cache);
        }
    }

    ExtendedBuildResult build_package(const VcpkgCmdArguments& args,
                                      const VcpkgPaths& paths,
                                      const InstallPlanAction& action,
                                      const IBuildLogsRecorder& build_logs_recorder,
                                      const StatusParagraphs& status_db)
    {
        auto& filesystem = paths.get_filesystem();
        auto& spec = action.spec;
        const std::string& name = action.source_control_file_and_location.value_or_exit(VCPKG_LINE_INFO).to_name();

        std::vector<FeatureSpec> missing_fspecs;
        for (const auto& kv : action.feature_dependencies)
        {
            for (const FeatureSpec& fspec : kv.second)
            {
                if (!status_db.is_installed(fspec) && !(fspec.port() == name && fspec.triplet() == spec.triplet()))
                {
                    missing_fspecs.emplace_back(fspec);
                }
            }
        }

        const bool all_dependencies_satisfied = missing_fspecs.empty();
        if (!all_dependencies_satisfied && !Util::Enum::to_bool(action.build_options.only_downloads))
        {
            return {BuildResult::CascadedDueToMissingDependencies, std::move(missing_fspecs)};
        }

        if (action.build_options.only_downloads == OnlyDownloads::No)
        {
            for (auto&& pspec : action.package_dependencies)
            {
                if (pspec == spec)
                {
                    continue;
                }
                const auto status_it = status_db.find_installed(pspec);
                Checks::check_exit(VCPKG_LINE_INFO, status_it != status_db.end());
            }
        }

        auto& abi_info = action.abi_info.value_or_exit(VCPKG_LINE_INFO);
        ExtendedBuildResult result =
            do_build_package_and_clean_buildtrees(args, paths, action, all_dependencies_satisfied);
        if (abi_info.abi_tag_file)
        {
            auto& abi_file = *abi_info.abi_tag_file.get();
            const auto abi_package_dir = action.package_dir.value_or_exit(VCPKG_LINE_INFO) / "share" / spec.name();
            const auto abi_file_in_package = abi_package_dir / "vcpkg_abi_info.txt";
            build_logs_recorder.record_build_result(paths, spec, result.code);
            filesystem.create_directories(abi_package_dir, VCPKG_LINE_INFO);
            filesystem.copy_file(abi_file, abi_file_in_package, CopyOptions::none, VCPKG_LINE_INFO);
        }

        return result;
    }

    void BuildResultCounts::increment(const BuildResult build_result)
    {
        switch (build_result)
        {
            case BuildResult::Succeeded: ++succeeded; return;
            case BuildResult::BuildFailed: ++build_failed; return;
            case BuildResult::PostBuildChecksFailed: ++post_build_checks_failed; return;
            case BuildResult::FileConflicts: ++file_conflicts; return;
            case BuildResult::CascadedDueToMissingDependencies: ++cascaded_due_to_missing_dependencies; return;
            case BuildResult::Excluded: ++excluded; return;
            case BuildResult::CacheMissing: ++cache_missing; return;
            case BuildResult::Downloaded: ++downloaded; return;
            case BuildResult::Removed: ++removed; return;
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }
    }

    template<class Message>
    static void print_build_result_summary_line(Message build_result_message, int count)
    {
        if (count != 0)
        {
            msg::println(LocalizedString().append_indent().append(
                msgBuildResultSummaryLine, msg::build_result = msg::format(build_result_message), msg::count = count));
        }
    }

    void BuildResultCounts::println(const Triplet& triplet) const
    {
        msg::println(msgBuildResultSummaryHeader, msg::triplet = triplet);
        print_build_result_summary_line(msgBuildResultSucceeded, succeeded);
        print_build_result_summary_line(msgBuildResultBuildFailed, build_failed);
        print_build_result_summary_line(msgBuildResultPostBuildChecksFailed, post_build_checks_failed);
        print_build_result_summary_line(msgBuildResultFileConflicts, file_conflicts);
        print_build_result_summary_line(msgBuildResultCascadeDueToMissingDependencies,
                                        cascaded_due_to_missing_dependencies);
        print_build_result_summary_line(msgBuildResultExcluded, excluded);
        print_build_result_summary_line(msgBuildResultCacheMissing, cache_missing);
        print_build_result_summary_line(msgBuildResultDownloaded, downloaded);
        print_build_result_summary_line(msgBuildResultRemoved, removed);
    }

    StringLiteral to_string_locale_invariant(const BuildResult build_result)
    {
        switch (build_result)
        {
            case BuildResult::Succeeded: return "SUCCEEDED";
            case BuildResult::BuildFailed: return "BUILD_FAILED";
            case BuildResult::PostBuildChecksFailed: return "POST_BUILD_CHECKS_FAILED";
            case BuildResult::FileConflicts: return "FILE_CONFLICTS";
            case BuildResult::CascadedDueToMissingDependencies: return "CASCADED_DUE_TO_MISSING_DEPENDENCIES";
            case BuildResult::Excluded: return "EXCLUDED";
            case BuildResult::CacheMissing: return "CACHE_MISSING";
            case BuildResult::Downloaded: return "DOWNLOADED";
            case BuildResult::Removed: return "REMOVED";
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }
    }

    LocalizedString to_string(const BuildResult build_result)
    {
        switch (build_result)
        {
            case BuildResult::Succeeded: return msg::format(msgBuildResultSucceeded);
            case BuildResult::BuildFailed: return msg::format(msgBuildResultBuildFailed);
            case BuildResult::PostBuildChecksFailed: return msg::format(msgBuildResultPostBuildChecksFailed);
            case BuildResult::FileConflicts: return msg::format(msgBuildResultFileConflicts);
            case BuildResult::CascadedDueToMissingDependencies:
                return msg::format(msgBuildResultCascadeDueToMissingDependencies);
            case BuildResult::Excluded: return msg::format(msgBuildResultExcluded);
            case BuildResult::CacheMissing: return msg::format(msgBuildResultCacheMissing);
            case BuildResult::Downloaded: return msg::format(msgBuildResultDownloaded);
            case BuildResult::Removed: return msg::format(msgBuildResultRemoved);
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }
    }

    LocalizedString create_error_message(const ExtendedBuildResult& build_result, const PackageSpec& spec)
    {
        auto res = msg::format(msgBuildingPackageFailed,
                               msg::spec = spec,
                               msg::build_result = to_string_locale_invariant(build_result.code));

        if (build_result.code == BuildResult::CascadedDueToMissingDependencies)
        {
            res.append_raw('\n').append_indent().append(msgBuildingPackageFailedDueToMissingDeps);

            for (const auto& missing_spec : build_result.unmet_dependencies)
            {
                res.append_raw('\n').append_indent(2).append_raw(missing_spec.to_string());
            }
        }

        return res;
    }

    void append_log(const Path& path, const std::string& log, size_t max_log_length, std::string& out)
    {
        static constexpr StringLiteral details_start = "<details><summary>{}</summary>\n\n```\n";
        static constexpr StringLiteral skipped_msg = "\n...\nSkipped {} lines\n...";
        static constexpr StringLiteral details_end = "\n```\n</details>\n\n";
        const size_t context_size = path.native().size() + details_start.size() + details_end.size() +
                                    skipped_msg.size() + 6 /* digits for skipped count */;
        const size_t minimum_log_size = std::min(size_t{100}, log.size());
        if (max_log_length < context_size + minimum_log_size)
        {
            return;
        }
        max_log_length -= context_size;
        fmt::format_to(std::back_inserter(out), details_start.c_str(), path.native());

        const auto start_block_max_length = max_log_length * 1 / 3;
        const auto end_block_max_length = max_log_length - start_block_max_length;
        if (log.size() > max_log_length)
        {
            auto first_block_end = log.find_last_of('\n', start_block_max_length);
            if (first_block_end == std::string::npos)
            {
                first_block_end = start_block_max_length;
            }

            auto last_block_start = log.find_first_of('\n', log.size() - end_block_max_length);
            if (last_block_start == std::string::npos)
            {
                last_block_start = log.size() - end_block_max_length;
            }

            auto first = log.begin() + first_block_end;
            auto last = log.begin() + last_block_start;
            auto skipped_lines = std::count(first, last, '\n');
            out.append(log.begin(), first);
            fmt::format_to(std::back_inserter(out), skipped_msg.c_str(), skipped_lines);
            out.append(last, log.end());
        }
        else
        {
            out += log;
        }

        while (!out.empty() && out.back() == '\n')
        {
            out.pop_back();
        }
        Strings::append(out, details_end);
    }

    void append_logs(std::vector<std::pair<Path, std::string>>&& logs, size_t max_size, std::string& out)
    {
        if (logs.empty())
        {
            return;
        }
        Util::sort(logs, [](const auto& left, const auto& right) { return left.second.size() < right.second.size(); });
        auto size_per_log = max_size / logs.size();
        size_t maximum = out.size();
        for (const auto& entry : logs)
        {
            maximum += size_per_log;
            const auto available = maximum - out.size();
            append_log(entry.first, entry.second, available, out);
        }
    }

    std::string create_github_issue(const VcpkgCmdArguments& args,
                                    const ExtendedBuildResult& build_result,
                                    const VcpkgPaths& paths,
                                    const InstallPlanAction& action,
                                    bool include_manifest)
    {
        constexpr size_t MAX_ISSUE_SIZE = 65536;
        const auto& fs = paths.get_filesystem();
        std::string issue_body;
        // The logs excerpts are as large as possible. So the issue body will often reach MAX_ISSUE_SIZE.
        issue_body.reserve(MAX_ISSUE_SIZE);
        fmt::format_to(std::back_inserter(issue_body),
                       "Package: {}\n\n**Host Environment**\n\n- Host: {}-{}\n",
                       action.display_name(),
                       to_zstring_view(get_host_processor()),
                       get_host_os_name());

        if (const auto* abi_info = action.abi_info.get())
        {
            if (const auto* compiler_info = abi_info->compiler_info.get())
            {
                fmt::format_to(
                    std::back_inserter(issue_body), "- Compiler: {} {}\n", compiler_info->id, compiler_info->version);
            }
        }

        fmt::format_to(std::back_inserter(issue_body), "-{}\n", paths.get_toolver_diagnostics());
        fmt::format_to(std::back_inserter(issue_body),
                       "**To Reproduce**\n\n`vcpkg {} {}`\n",
                       args.get_command(),
                       Strings::join(" ", args.get_forwardable_arguments()));
        fmt::format_to(std::back_inserter(issue_body),
                       "**Failure logs**\n\n```\n{}\n```\n",
                       paths.get_filesystem().read_contents(build_result.stdoutlog.value_or_exit(VCPKG_LINE_INFO),
                                                            VCPKG_LINE_INFO));

        std::string postfix;
        const auto maybe_manifest = paths.get_manifest();
        if (auto manifest = maybe_manifest.get())
        {
            if (include_manifest || manifest->manifest.contains("builtin-baseline"))
            {
                fmt::format_to(
                    std::back_inserter(postfix),
                    "**Additional context**\n\n<details><summary>vcpkg.json</summary>\n\n```\n{}\n```\n</details>\n",
                    Json::stringify(manifest->manifest));
            }
        }

        if (issue_body.size() + postfix.size() < MAX_ISSUE_SIZE)
        {
            size_t remaining_body_size = MAX_ISSUE_SIZE - issue_body.size() - postfix.size();
            auto logs = Util::fmap(build_result.error_logs, [&](auto&& path) -> std::pair<Path, std::string> {
                return {path, fs.read_contents(path, VCPKG_LINE_INFO)};
            });
            append_logs(std::move(logs), remaining_body_size, issue_body);
        }

        issue_body.append(postfix);

        return issue_body;
    }

    static std::string make_gh_issue_search_url(const std::string& spec_name)
    {
        return "https://github.com/microsoft/vcpkg/issues?q=is%3Aissue+is%3Aopen+in%3Atitle+" + spec_name;
    }

    static std::string make_gh_issue_open_url(StringView spec_name, StringView triplet, StringView path)
    {
        return Strings::concat("https://github.com/microsoft/vcpkg/issues/new?title=[",
                               spec_name,
                               "]+Build+error+on+",
                               triplet,
                               "&body=Copy+issue+body+from+",
                               Strings::percent_encode(path));
    }

    LocalizedString create_user_troubleshooting_message(const InstallPlanAction& action,
                                                        const VcpkgPaths& paths,
                                                        const Optional<Path>& issue_body)
    {
        const auto& spec_name = action.spec.name();
        const auto& triplet_name = action.spec.triplet().to_string();
        LocalizedString result = msg::format(msgBuildTroubleshootingMessage1).append_raw('\n');
        result.append_indent().append_raw(make_gh_issue_search_url(spec_name)).append_raw('\n');
        result.append(msgBuildTroubleshootingMessage2).append_raw('\n');
        if (issue_body.has_value())
        {
            const auto path = issue_body.get()->generic_u8string();
            result.append_indent().append_raw(make_gh_issue_open_url(spec_name, triplet_name, path)).append_raw('\n');
            if (!paths.get_filesystem().find_from_PATH("gh").empty())
            {
                Command gh("gh");
                gh.string_arg("issue").string_arg("create").string_arg("-R").string_arg("microsoft/vcpkg");
                gh.string_arg("--title").string_arg(fmt::format("[{}] Build failure on {}", spec_name, triplet_name));
                gh.string_arg("--body-file").string_arg(path);

                result.append(msgBuildTroubleshootingMessageGH).append_raw('\n');
                result.append_indent().append_raw(gh.command_line());
            }
        }
        else
        {
            result.append_indent()
                .append_raw("https://github.com/microsoft/vcpkg/issues/"
                            "new?template=report-package-build-failure.md&title=[")
                .append_raw(spec_name)
                .append_raw("]+Build+error+on+")
                .append_raw(triplet_name)
                .append_raw("\n");
            result.append(msgBuildTroubleshootingMessage3, msg::package_name = spec_name).append_raw('\n');
            result.append_raw(paths.get_toolver_diagnostics()).append_raw('\n');
        }

        return result;
    }

    static BuildInfo inner_create_buildinfo(StringView origin, Paragraph&& pgh)
    {
        ParagraphParser parser(origin, std::move(pgh));

        BuildInfo build_info;

        {
            std::string crt_linkage_as_string = parser.required_field(BuildInfoRequiredField::CRT_LINKAGE);
            auto crtlinkage = to_linkage_type(crt_linkage_as_string);
            if (const auto p = crtlinkage.get())
            {
                build_info.crt_linkage = *p;
            }
            else
            {
                Checks::msg_exit_with_message(
                    VCPKG_LINE_INFO, msgInvalidLinkage, msg::system_name = "crt", msg::value = crt_linkage_as_string);
            }
        }

        {
            std::string library_linkage_as_string = parser.required_field(BuildInfoRequiredField::LIBRARY_LINKAGE);
            auto liblinkage = to_linkage_type(library_linkage_as_string);
            if (const auto p = liblinkage.get())
            {
                build_info.library_linkage = *p;
            }
            else
            {
                Checks::msg_exit_with_message(VCPKG_LINE_INFO,
                                              msgInvalidLinkage,
                                              msg::system_name = "library",
                                              msg::value = library_linkage_as_string);
            }
        }

        std::string version = parser.optional_field("Version");
        if (!version.empty())
        {
            sanitize_version_string(version);
            build_info.detected_head_version = Version::parse(std::move(version)).value_or_exit(VCPKG_LINE_INFO);
        }

        std::unordered_map<BuildPolicy, bool> policies;
        for (const auto& policy : ALL_POLICIES)
        {
            const auto setting = parser.optional_field(to_string_view(policy));
            if (setting.empty()) continue;
            if (setting == "enabled")
                policies.emplace(policy, true);
            else if (setting == "disabled")
                policies.emplace(policy, false);
            else
                Checks::msg_exit_maybe_upgrade(VCPKG_LINE_INFO,
                                               msgUnknownPolicySetting,
                                               msg::option = setting,
                                               msg::value = to_string_view(policy));
        }

        auto maybe_error = parser.error();
        if (const auto err = maybe_error.get())
        {
            Checks::msg_exit_with_message(VCPKG_LINE_INFO, *err);
        }

        build_info.policies = BuildPolicies(std::move(policies));

        return build_info;
    }

    BuildInfo read_build_info(const ReadOnlyFilesystem& fs, const Path& filepath)
    {
        auto maybe_paragraph = Paragraphs::get_single_paragraph(console_diagnostic_context, fs, filepath);
        if (auto paragraph = maybe_paragraph.get())
        {
            return inner_create_buildinfo(filepath, std::move(*paragraph));
        }

        Checks::exit_fail(VCPKG_LINE_INFO);
    }

    static ExpectedL<bool> from_cmake_bool(StringView value, StringView name)
    {
        if (value == "1" || Strings::case_insensitive_ascii_equals(value, "on") ||
            Strings::case_insensitive_ascii_equals(value, "true"))
        {
            return true;
        }
        else if (value == "0" || Strings::case_insensitive_ascii_equals(value, "off") ||
                 Strings::case_insensitive_ascii_equals(value, "false"))
        {
            return false;
        }
        else
        {
            return msg::format(msgUnknownBooleanSetting, msg::option = name, msg::value = value);
        }
    }

    PreBuildInfo::PreBuildInfo(const VcpkgPaths& paths,
                               Triplet triplet,
                               const std::unordered_map<std::string, std::string>& cmakevars)
        : triplet(triplet), m_paths(paths)
    {
        enum class VcpkgTripletVar
        {
            TARGET_ARCHITECTURE = 0,
            CMAKE_SYSTEM_NAME,
            CMAKE_SYSTEM_VERSION,
            PLATFORM_TOOLSET,
            PLATFORM_TOOLSET_VERSION,
            VISUAL_STUDIO_PATH,
            CHAINLOAD_TOOLCHAIN_FILE,
            BUILD_TYPE,
            ENV_PASSTHROUGH,
            ENV_PASSTHROUGH_UNTRACKED,
            PUBLIC_ABI_OVERRIDE,
            LOAD_VCVARS_ENV,
            DISABLE_COMPILER_TRACKING,
            XBOX_CONSOLE_TARGET,
            Z_VCPKG_GameDKLatest
        };

        static const std::vector<std::pair<std::string, VcpkgTripletVar>> VCPKG_OPTIONS = {
            {"VCPKG_TARGET_ARCHITECTURE", VcpkgTripletVar::TARGET_ARCHITECTURE},
            {"VCPKG_CMAKE_SYSTEM_NAME", VcpkgTripletVar::CMAKE_SYSTEM_NAME},
            {"VCPKG_CMAKE_SYSTEM_VERSION", VcpkgTripletVar::CMAKE_SYSTEM_VERSION},
            {"VCPKG_PLATFORM_TOOLSET", VcpkgTripletVar::PLATFORM_TOOLSET},
            {"VCPKG_PLATFORM_TOOLSET_VERSION", VcpkgTripletVar::PLATFORM_TOOLSET_VERSION},
            {"VCPKG_VISUAL_STUDIO_PATH", VcpkgTripletVar::VISUAL_STUDIO_PATH},
            {"VCPKG_CHAINLOAD_TOOLCHAIN_FILE", VcpkgTripletVar::CHAINLOAD_TOOLCHAIN_FILE},
            {"VCPKG_BUILD_TYPE", VcpkgTripletVar::BUILD_TYPE},
            {"VCPKG_ENV_PASSTHROUGH", VcpkgTripletVar::ENV_PASSTHROUGH},
            {"VCPKG_ENV_PASSTHROUGH_UNTRACKED", VcpkgTripletVar::ENV_PASSTHROUGH_UNTRACKED},
            {"VCPKG_PUBLIC_ABI_OVERRIDE", VcpkgTripletVar::PUBLIC_ABI_OVERRIDE},
            // Note: this value must come after VCPKG_CHAINLOAD_TOOLCHAIN_FILE because its default depends upon it.
            {"VCPKG_LOAD_VCVARS_ENV", VcpkgTripletVar::LOAD_VCVARS_ENV},
            {"VCPKG_DISABLE_COMPILER_TRACKING", VcpkgTripletVar::DISABLE_COMPILER_TRACKING},
            {"VCPKG_XBOX_CONSOLE_TARGET", VcpkgTripletVar::XBOX_CONSOLE_TARGET},
            {"Z_VCPKG_GameDKLatest", VcpkgTripletVar::Z_VCPKG_GameDKLatest},
        };

        const std::string empty;
        for (auto&& kv : VCPKG_OPTIONS)
        {
            const std::string& variable_value = Util::value_or_default(cmakevars, kv.first, empty);
            switch (kv.second)
            {
                case VcpkgTripletVar::TARGET_ARCHITECTURE: target_architecture = variable_value; break;
                case VcpkgTripletVar::CMAKE_SYSTEM_NAME: cmake_system_name = variable_value; break;
                case VcpkgTripletVar::CMAKE_SYSTEM_VERSION: cmake_system_version = variable_value; break;
                case VcpkgTripletVar::PLATFORM_TOOLSET:
                    platform_toolset = variable_value.empty() ? nullopt : Optional<std::string>{variable_value};
                    break;
                case VcpkgTripletVar::PLATFORM_TOOLSET_VERSION:
                    platform_toolset_version = variable_value.empty() ? nullopt : Optional<std::string>{variable_value};
                    break;
                case VcpkgTripletVar::VISUAL_STUDIO_PATH:
                    visual_studio_path = variable_value.empty() ? nullopt : Optional<Path>{variable_value};
                    break;
                case VcpkgTripletVar::CHAINLOAD_TOOLCHAIN_FILE:
                    external_toolchain_file = variable_value.empty() ? nullopt : Optional<std::string>{variable_value};
                    break;
                case VcpkgTripletVar::BUILD_TYPE:
                    if (variable_value.empty())
                        build_type = nullopt;
                    else if (Strings::case_insensitive_ascii_equals(variable_value, "debug"))
                        build_type = ConfigurationType::Debug;
                    else if (Strings::case_insensitive_ascii_equals(variable_value, "release"))
                        build_type = ConfigurationType::Release;
                    else
                        Checks::msg_exit_with_message(
                            VCPKG_LINE_INFO, msgUnknownSettingForBuildType, msg::option = variable_value);
                    break;
                case VcpkgTripletVar::ENV_PASSTHROUGH:
                    passthrough_env_vars_tracked = Strings::split(variable_value, ';');
                    Util::Vectors::append(&passthrough_env_vars, passthrough_env_vars_tracked);
                    break;
                case VcpkgTripletVar::ENV_PASSTHROUGH_UNTRACKED:
                    Util::Vectors::append(&passthrough_env_vars, Strings::split(variable_value, ';'));
                    break;
                case VcpkgTripletVar::PUBLIC_ABI_OVERRIDE:
                    public_abi_override = variable_value.empty() ? nullopt : Optional<std::string>{variable_value};
                    break;
                case VcpkgTripletVar::LOAD_VCVARS_ENV:
                    if (variable_value.empty())
                    {
                        load_vcvars_env = !external_toolchain_file.has_value();
                    }
                    else
                    {
                        load_vcvars_env = from_cmake_bool(variable_value, kv.first).value_or_exit(VCPKG_LINE_INFO);
                    }
                    break;
                case VcpkgTripletVar::DISABLE_COMPILER_TRACKING:
                    if (variable_value.empty())
                    {
                        disable_compiler_tracking = false;
                    }
                    else
                    {
                        disable_compiler_tracking =
                            from_cmake_bool(variable_value, kv.first).value_or_exit(VCPKG_LINE_INFO);
                    }
                    break;
                case VcpkgTripletVar::XBOX_CONSOLE_TARGET:
                    if (!variable_value.empty())
                    {
                        target_is_xbox = true;
                    }
                    break;
                case VcpkgTripletVar::Z_VCPKG_GameDKLatest:
                    if (!variable_value.empty())
                    {
                        gamedk_latest_path.emplace(variable_value);
                    }
                    break;
            }
        }
    }

    ExtendedBuildResult::ExtendedBuildResult(BuildResult code) : code(code) { }
    ExtendedBuildResult::ExtendedBuildResult(BuildResult code,
                                             vcpkg::Path stdoutlog,
                                             std::vector<std::string>&& error_logs)
        : code(code), stdoutlog(stdoutlog), error_logs(error_logs)
    {
    }
    ExtendedBuildResult::ExtendedBuildResult(BuildResult code, std::unique_ptr<BinaryControlFile>&& bcf)
        : code(code), binary_control_file(std::move(bcf))
    {
    }
    ExtendedBuildResult::ExtendedBuildResult(BuildResult code, std::vector<FeatureSpec>&& unmet_deps)
        : code(code), unmet_dependencies(std::move(unmet_deps))
    {
    }

    const IBuildLogsRecorder& null_build_logs_recorder() noexcept { return null_build_logs_recorder_instance; }
}
