#include <vcpkg/base/cache.h>
#include <vcpkg/base/checks.h>
#include <vcpkg/base/chrono.h>
#include <vcpkg/base/hash.h>
#include <vcpkg/base/json.h>
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
#include <vcpkg/build.h>
#include <vcpkg/buildenvironment.h>
#include <vcpkg/cmakevars.h>
#include <vcpkg/commands.h>
#include <vcpkg/commands.version.h>
#include <vcpkg/dependencies.h>
#include <vcpkg/documentation.h>
#include <vcpkg/globalstate.h>
#include <vcpkg/help.h>
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

namespace vcpkg::Build
{
    void perform_and_exit_ex(const VcpkgCmdArguments& args,
                             const FullPackageSpec& full_spec,
                             Triplet host_triplet,
                             const PathsPortFileProvider& provider,
                             BinaryCache& binary_cache,
                             const IBuildLogsRecorder& build_logs_recorder,
                             const VcpkgPaths& paths)
    {
        Checks::exit_with_code(
            VCPKG_LINE_INFO,
            perform_ex(args, full_spec, host_triplet, provider, binary_cache, build_logs_recorder, paths));
    }

    const CommandStructure COMMAND_STRUCTURE = {
        [] { return create_example_string("build zlib:x64-windows"); },
        1,
        1,
        {{}, {}},
        nullptr,
    };

    void perform_and_exit(const VcpkgCmdArguments& args,
                          const VcpkgPaths& paths,
                          Triplet default_triplet,
                          Triplet host_triplet)
    {
        Checks::exit_with_code(VCPKG_LINE_INFO, perform(args, paths, default_triplet, host_triplet));
    }

    int perform_ex(const VcpkgCmdArguments& args,
                   const FullPackageSpec& full_spec,
                   Triplet host_triplet,
                   const PathsPortFileProvider& provider,
                   BinaryCache& binary_cache,
                   const IBuildLogsRecorder& build_logs_recorder,
                   const VcpkgPaths& paths)
    {
        const PackageSpec& spec = full_spec.package_spec;
        auto var_provider_storage = CMakeVars::make_triplet_cmake_var_provider(paths);
        auto& var_provider = *var_provider_storage;
        var_provider.load_dep_info_vars({{spec}}, host_triplet);

        StatusParagraphs status_db = database_load_check(paths.get_filesystem(), paths.installed());
        auto action_plan =
            create_feature_install_plan(provider, var_provider, {&full_spec, 1}, status_db, {host_triplet});

        var_provider.load_tag_vars(action_plan, provider, host_triplet);

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
        const auto& core_paragraph_name = scf.core_paragraph->name;
        if (spec_name != core_paragraph_name)
        {
            Checks::msg_exit_with_error(VCPKG_LINE_INFO,
                                        msgSourceFieldPortNameMismatch,
                                        msg::package_name = core_paragraph_name,
                                        msg::path = spec_name);
        }

        action->build_options = default_build_package_options;
        action->build_options.editable = Editable::YES;
        action->build_options.clean_buildtrees = CleanBuildtrees::NO;
        action->build_options.clean_packages = CleanPackages::NO;

        const ElapsedTimer build_timer;
        const auto result = build_package(args, paths, *action, build_logs_recorder, status_db);
        msg::print(msgElapsedForPackage, msg::spec = spec, msg::elapsed = build_timer);
        if (result.code == BuildResult::CASCADED_DUE_TO_MISSING_DEPENDENCIES)
        {
            LocalizedString errorMsg = msg::format(msg::msgErrorMessage).append(msgBuildDependenciesMissing);
            for (const auto& p : result.unmet_dependencies)
            {
                errorMsg.append_raw('\n').append_indent().append_raw(p.to_string());
            }

            Checks::msg_exit_with_message(VCPKG_LINE_INFO, errorMsg);
        }

        Checks::check_exit(VCPKG_LINE_INFO, result.code != BuildResult::EXCLUDED);

        if (result.code != BuildResult::SUCCEEDED)
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
        binary_cache.push_success(*action, paths.package_dir(action->spec));

        return 0;
    }

    int perform(const VcpkgCmdArguments& args, const VcpkgPaths& paths, Triplet default_triplet, Triplet host_triplet)
    {
        // Build only takes a single package and all dependencies must already be installed
        const ParsedArguments options = args.parse_arguments(COMMAND_STRUCTURE);
        std::string first_arg = options.command_arguments[0];

        BinaryCache binary_cache{args, paths};
        const FullPackageSpec spec = check_and_get_full_package_spec(
            std::move(first_arg), default_triplet, COMMAND_STRUCTURE.get_example_text(), paths);
        print_default_triplet_warning(args, {&options.command_arguments[0], 1});

        auto& fs = paths.get_filesystem();
        auto registry_set = paths.make_registry_set();
        PathsPortFileProvider provider(
            fs, *registry_set, make_overlay_provider(fs, paths.original_cwd, paths.overlay_ports));
        return perform_ex(args, spec, host_triplet, provider, binary_cache, null_build_logs_recorder(), paths);
    }
} // namespace vcpkg::Build

namespace vcpkg
{
    void BuildCommand::perform_and_exit(const VcpkgCmdArguments& args,
                                        const VcpkgPaths& paths,
                                        Triplet default_triplet,
                                        Triplet host_triplet) const
    {
        Build::perform_and_exit(args, paths, default_triplet, host_triplet);
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
            case DownloadTool::BUILT_IN: return NAME_BUILTIN_DOWNLOAD;
            case DownloadTool::ARIA2: return NAME_ARIA2_DOWNLOAD;
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }
    }

    std::string to_string(DownloadTool tool) { return to_string_view(tool).to_string(); }

    Optional<LinkageType> to_linkage_type(StringView str)
    {
        if (str == "dynamic") return LinkageType::DYNAMIC;
        if (str == "static") return LinkageType::STATIC;
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
        msg::println(msg::msgSeeURL, msg::url = docs::vcpkg_visual_studio_path_url);
        Checks::exit_maybe_upgrade(VCPKG_LINE_INFO);
    }
#endif

#if defined(_WIN32)
    const Environment& EnvCache::get_action_env(const VcpkgPaths& paths, const AbiInfo& abi_info)
    {
        auto build_env_cmd =
            make_build_env_cmd(*abi_info.pre_build_info, abi_info.toolset.value_or_exit(VCPKG_LINE_INFO));

        const auto& base_env = envs.get_lazy(abi_info.pre_build_info->passthrough_env_vars, [&]() -> EnvMapEntry {
            std::unordered_map<std::string, std::string> env;

            for (auto&& env_var : abi_info.pre_build_info->passthrough_env_vars)
            {
                auto env_val = get_environment_variable(env_var);

                if (env_val)
                {
                    env[env_var] = env_val.value_or_exit(VCPKG_LINE_INFO);
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
                msg::println(msgUseEnvVar, msg::env_var = "HTTP(S)_PROXY");
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
                                auto protocol = kvp[0];
                                auto address = kvp[1];

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

                                protocol = Strings::concat(Strings::ascii_to_uppercase(protocol.c_str()), "_PROXY");
                                env.emplace(protocol, address);
                                msg::println(msgSettingEnvVar, msg::env_var = protocol, msg::url = address);
                            }
                        }
                    }
                    // Specified http:// prefix
                    else if (Strings::starts_with(server, "http://"))
                    {
                        msg::println(msgSettingEnvVar, msg::env_var = "HTTP_PROXY", msg::url = server);
                        env.emplace("HTTP_PROXY", server);
                    }
                    // Specified https:// prefix
                    else if (Strings::starts_with(server, "https://"))
                    {
                        msg::println(msgSettingEnvVar, msg::env_var = "HTTPS_PROXY", msg::url = server);
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
                        msg::println(msgAutoSettingEnvVar, msg::env_var = "HTTP(S)_PROXY", msg::url = server);

                        env.emplace("HTTP_PROXY", server.c_str());
                        env.emplace("HTTPS_PROXY", server.c_str());
                    }
                }
            }
            return {env};
        });

        return base_env.cmd_cache.get_lazy(build_env_cmd, [&]() {
            const Path& powershell_exe_path = paths.get_tool_exe("powershell-core", stdout_sink);
            auto clean_env = get_modified_clean_environment(base_env.env_map, powershell_exe_path.parent_path());
            if (build_env_cmd.empty())
                return clean_env;
            else
                return cmd_execute_and_capture_environment(build_env_cmd, clean_env);
        });
    }
#else
    const Environment& EnvCache::get_action_env(const VcpkgPaths&, const AbiInfo&) { return get_clean_environment(); }
#endif

    static CompilerInfo load_compiler_info(const VcpkgPaths& paths, const AbiInfo& abi_info);

    static const std::string& get_toolchain_cache(Cache<Path, std::string>& cache,
                                                  const Path& tcfile,
                                                  const Filesystem& fs)
    {
        return cache.get_lazy(tcfile, [&]() {
            return Hash::get_file_hash(fs, tcfile, Hash::Algorithm::Sha256).value_or_exit(VCPKG_LINE_INFO);
        });
    }

    const EnvCache::TripletMapEntry& EnvCache::get_triplet_cache(const Filesystem& fs, const Path& p) const
    {
        return m_triplet_cache.get_lazy(p, [&]() -> TripletMapEntry {
            return TripletMapEntry{Hash::get_file_hash(fs, p, Hash::Algorithm::Sha256).value_or_exit(VCPKG_LINE_INFO)};
        });
    }

    const CompilerInfo& EnvCache::get_compiler_info(const VcpkgPaths& paths, const AbiInfo& abi_info)
    {
        Checks::check_exit(VCPKG_LINE_INFO, abi_info.pre_build_info != nullptr);
        if (!m_compiler_tracking || abi_info.pre_build_info->disable_compiler_tracking)
        {
            static CompilerInfo empty_ci;
            return empty_ci;
        }

        const auto& fs = paths.get_filesystem();

        const auto triplet_file_path = paths.get_triplet_file_path(abi_info.pre_build_info->triplet);

        auto&& toolchain_hash = get_toolchain_cache(m_toolchain_cache, abi_info.pre_build_info->toolchain_file(), fs);

        auto&& triplet_entry = get_triplet_cache(fs, triplet_file_path);

        return triplet_entry.compiler_info.get_lazy(toolchain_hash, [&]() -> CompilerInfo {
            if (m_compiler_tracking)
            {
                return load_compiler_info(paths, abi_info);
            }
            else
            {
                return CompilerInfo{};
            }
        });
    }

    const std::string& EnvCache::get_triplet_info(const VcpkgPaths& paths, const AbiInfo& abi_info)
    {
        const auto& fs = paths.get_filesystem();
        Checks::check_exit(VCPKG_LINE_INFO, abi_info.pre_build_info != nullptr);
        const auto triplet_file_path = paths.get_triplet_file_path(abi_info.pre_build_info->triplet);

        auto&& toolchain_hash = get_toolchain_cache(m_toolchain_cache, abi_info.pre_build_info->toolchain_file(), fs);

        auto&& triplet_entry = get_triplet_cache(fs, triplet_file_path);

        if (m_compiler_tracking && !abi_info.pre_build_info->disable_compiler_tracking)
        {
            return triplet_entry.triplet_infos.get_lazy(toolchain_hash, [&]() -> std::string {
                auto& compiler_info = get_compiler_info(paths, abi_info);
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

        return vcpkg::Command{"cmd"}.string_arg("/c").raw_arg(fmt::format(R"("{}" {} {} {} {} 2>&1 <NUL)",
                                                                          toolset.vcvarsall,
                                                                          Strings::join(" ", toolset.vcvarsall_options),
                                                                          arch,
                                                                          target,
                                                                          tonull));
#endif
    }

    static std::unique_ptr<BinaryControlFile> create_binary_control_file(
        const SourceParagraph& source_paragraph,
        Triplet triplet,
        const BuildInfo& build_info,
        const std::string& abi_tag,
        const std::vector<FeatureSpec>& core_dependencies)
    {
        auto bcf = std::make_unique<BinaryControlFile>();
        BinaryParagraph bpgh(source_paragraph, triplet, abi_tag, core_dependencies);
        if (const auto p_ver = build_info.version.get())
        {
            bpgh.version = *p_ver;
        }

        bcf->core_paragraph = std::move(bpgh);
        return bcf;
    }

    static void write_binary_control_file(const VcpkgPaths& paths, const BinaryControlFile& bcf)
    {
        std::string start = Strings::serialize(bcf.core_paragraph);
        for (auto&& feature : bcf.features)
        {
            start.push_back('\n');
            start += Strings::serialize(feature);
        }
        const auto binary_control_file = paths.package_dir(bcf.core_paragraph.spec) / "CONTROL";
        paths.get_filesystem().write_contents(binary_control_file, start, VCPKG_LINE_INFO);
    }

    static void get_generic_cmake_build_args(const VcpkgPaths& paths,
                                             Triplet triplet,
                                             const Toolset& toolset,
                                             std::vector<CMakeVariable>& out_vars)
    {
        Util::Vectors::append(&out_vars,
                              std::initializer_list<CMakeVariable>{
                                  {"CMD", "BUILD"},
                                  {"DOWNLOADS", paths.downloads},
                                  {"TARGET_TRIPLET", triplet.canonical_name()},
                                  {"TARGET_TRIPLET_FILE", paths.get_triplet_file_path(triplet)},
                                  {"VCPKG_BASE_VERSION", VCPKG_BASE_VERSION_AS_STRING},
                                  {"VCPKG_CONCURRENCY", std::to_string(get_concurrency())},
                                  {"VCPKG_PLATFORM_TOOLSET", toolset.version.c_str()},
                              });
        // Make sure GIT could be found
        const Path& git_exe_path = paths.get_tool_exe(Tools::GIT, stdout_sink);
        out_vars.emplace_back("GIT", git_exe_path);
    }

    static CompilerInfo load_compiler_info(const VcpkgPaths& paths, const AbiInfo& abi_info)
    {
        auto triplet = abi_info.pre_build_info->triplet;
        msg::println(msgDetectCompilerHash, msg::triplet = triplet);
        auto buildpath = paths.buildtrees() / "detect_compiler";

        std::vector<CMakeVariable> cmake_args{
            {"CURRENT_PORT_DIR", paths.scripts / "detect_compiler"},
            {"CURRENT_BUILDTREES_DIR", buildpath},
            {"CURRENT_PACKAGES_DIR", paths.packages() / ("detect_compiler_" + triplet.canonical_name())},
            // The detect_compiler "port" doesn't depend on the host triplet, so always natively compile
            {"_HOST_TRIPLET", triplet.canonical_name()},
        };
        get_generic_cmake_build_args(paths, triplet, abi_info.toolset.value_or_exit(VCPKG_LINE_INFO), cmake_args);

        auto command = vcpkg::make_cmake_cmd(paths, paths.ports_cmake, std::move(cmake_args));

        const auto& env = paths.get_action_env(abi_info);
        auto& fs = paths.get_filesystem();
        fs.create_directory(buildpath, VCPKG_LINE_INFO);
        auto stdoutlog = buildpath / ("stdout-" + triplet.canonical_name() + ".log");
        CompilerInfo compiler_info;
        std::string buf;

        ExpectedL<int> rc = LocalizedString();
        {
            const auto out_file = fs.open_for_write(stdoutlog, VCPKG_LINE_INFO);
            rc = cmd_execute_and_stream_lines(
                command,
                [&](StringView s) {
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
                },
                default_working_directory,
                env);
        } // close out_file

        if (compiler_info.hash.empty() || !succeeded(rc))
        {
            Debug::println("Compiler information tracking can be disabled by passing --",
                           VcpkgCmdArguments::FEATURE_FLAGS_ARG,
                           "=-",
                           VcpkgCmdArguments::COMPILER_TRACKING_FEATURE);

            msg::println_error(msgErrorDetectingCompilerInfo, msg::path = stdoutlog);
            msg::write_unlocalized_text_to_stdout(Color::none, buf);
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

        std::string all_features;
        for (auto& feature : scf.feature_paragraphs)
        {
            all_features.append(feature->name + ";");
        }

        std::vector<CMakeVariable> variables{
            {"ALL_FEATURES", all_features},
            {"CURRENT_PORT_DIR", scfl.source_location},
            {"_HOST_TRIPLET", action.host_triplet.canonical_name()},
            {"FEATURES", Strings::join(";", action.feature_list)},
            {"PORT", scf.core_paragraph->name},
            {"VERSION", scf.core_paragraph->raw_version},
            {"VCPKG_USE_HEAD_VERSION", Util::Enum::to_bool(action.build_options.use_head_version) ? "1" : "0"},
            {"_VCPKG_DOWNLOAD_TOOL", to_string_view(action.build_options.download_tool)},
            {"_VCPKG_EDITABLE", Util::Enum::to_bool(action.build_options.editable) ? "1" : "0"},
            {"_VCPKG_NO_DOWNLOADS", !Util::Enum::to_bool(action.build_options.allow_downloads) ? "1" : "0"},
            {"Z_VCPKG_CHAINLOAD_TOOLCHAIN_FILE", action.pre_build_info(VCPKG_LINE_INFO).toolchain_file()},
        };

        if (action.build_options.download_tool == DownloadTool::ARIA2)
        {
            variables.emplace_back("ARIA2", paths.get_tool_exe(Tools::ARIA2, stdout_sink));
        }

        for (const auto& cmake_arg : args.cmake_args)
        {
            variables.emplace_back(cmake_arg);
        }

        if (action.build_options.backcompat_features == BackcompatFeatures::PROHIBIT)
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

        const Filesystem& fs = paths.get_filesystem();

        std::vector<std::string> port_configs;
        for (const PackageSpec& dependency : action.package_dependencies)
        {
            const Path port_config_path = paths.installed().vcpkg_port_config_cmake(dependency);

            if (fs.is_regular_file(port_config_path))
            {
                port_configs.emplace_back(port_config_path.native());
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
            // HACK: remove once we have fully shipped a uwp toolchain
            static bool have_uwp_triplet =
                m_paths.get_filesystem().exists(m_paths.scripts / "toolchains/uwp.cmake", IgnoreErrors{});
            if (have_uwp_triplet)
            {
                return m_paths.scripts / "toolchains/uwp.cmake";
            }
            else
            {
                return m_paths.scripts / "toolchains/windows.cmake";
            }
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
                                      scf.core_paragraph->name,
                                      '-',
                                      action.spec.triplet(),
                                      '-',
                                      scf.to_version(),
                                      '-',
                                      generate_random_UUID());

        const auto now = CTime::now_string();
        const auto& abi = action.abi_info.value_or_exit(VCPKG_LINE_INFO);

        const auto json_path = paths.package_dir(action.spec) / "share" / action.spec.name() / "vcpkg.spdx.json";
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
        const auto& triplet_file_path = paths.get_triplet_file_path(triplet);

        if (Strings::starts_with(triplet_file_path, paths.community_triplets))
        {
            msg::println_warning(msgUsingCommunityTriplet, msg::triplet = triplet.canonical_name());
            msg::println(msgLoadingCommunityTriplet, msg::path = triplet_file_path);
        }
        else if (!Strings::starts_with(triplet_file_path, paths.triplets))
        {
            msg::println(msgLoadingOverlayTriplet, msg::path = triplet_file_path);
        }

        if (!Strings::starts_with(scfl.source_location, paths.builtin_ports_directory()))
        {
            msg::println(msgInstallingFromLocation, msg::path = scfl.source_location);
        }

        const ElapsedTimer timer;
        auto command = vcpkg::make_cmake_cmd(paths, paths.ports_cmake, get_cmake_build_args(args, paths, action));

        const auto& abi_info = action.abi_info.value_or_exit(VCPKG_LINE_INFO);
        auto env = paths.get_action_env(abi_info);

        auto buildpath = paths.build_dir(action.spec);
        fs.create_directory(buildpath, VCPKG_LINE_INFO);
        env.add_entry("GIT_CEILING_DIRECTORIES", fs.absolute(buildpath.parent_path(), VCPKG_LINE_INFO));
        auto stdoutlog = buildpath / ("stdout-" + action.spec.triplet().canonical_name() + ".log");
        ExpectedL<int> return_code = LocalizedString();
        {
            auto out_file = fs.open_for_write(stdoutlog, VCPKG_LINE_INFO);
            return_code = cmd_execute_and_stream_data(
                command,
                [&](StringView sv) {
                    msg::write_unlocalized_text_to_stdout(Color::none, sv);
                    Checks::msg_check_exit(VCPKG_LINE_INFO,
                                           out_file.write(sv.data(), 1, sv.size()) == sv.size(),
                                           msgErrorWhileWriting,
                                           msg::path = stdoutlog);
                },
                default_working_directory,
                env);
        } // close out_file

        const auto buildtimeus = timer.microseconds();
        const auto spec_string = action.spec.to_string();
        const bool build_failed = !succeeded(return_code);
        MetricsSubmission metrics;
        if (build_failed)
        {
            // With the exception of empty or helper ports, builds in "Download Mode" result in failure.
            if (action.build_options.only_downloads == OnlyDownloads::YES)
            {
                // TODO: Capture executed command output and evaluate whether the failure was intended.
                // If an unintended error occurs then return a BuildResult::DOWNLOAD_FAILURE status.
                return ExtendedBuildResult{BuildResult::DOWNLOADED};
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
            return ExtendedBuildResult{BuildResult::DOWNLOADED};
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
            return ExtendedBuildResult{BuildResult::BUILD_FAILED, stdoutlog, std::move(error_logs)};
        }

        const BuildInfo build_info = read_build_info(fs, paths.build_info_file_path(action.spec));
        size_t error_count = 0;
        {
            FileSink file_sink{fs, stdoutlog, Append::YES};
            CombiningSink combo_sink{stdout_sink, file_sink};
            error_count = perform_post_build_lint_checks(
                action.spec, paths, pre_build_info, build_info, scfl.source_location, combo_sink);
        };

        auto find_itr = action.feature_dependencies.find("core");
        Checks::check_exit(VCPKG_LINE_INFO, find_itr != action.feature_dependencies.end());

        std::unique_ptr<BinaryControlFile> bcf = create_binary_control_file(
            *scfl.source_control_file->core_paragraph, triplet, build_info, action.public_abi(), find_itr->second);

        if (error_count != 0 && action.build_options.backcompat_features == BackcompatFeatures::PROHIBIT)
        {
            return ExtendedBuildResult{BuildResult::POST_BUILD_CHECKS_FAILED};
        }

        for (auto&& feature : action.feature_list)
        {
            for (auto&& f_pgh : scfl.source_control_file->feature_paragraphs)
            {
                if (f_pgh->name == feature)
                {
                    find_itr = action.feature_dependencies.find(feature);
                    Checks::check_exit(VCPKG_LINE_INFO, find_itr != action.feature_dependencies.end());

                    bcf->features.emplace_back(
                        *scfl.source_control_file->core_paragraph, *f_pgh, triplet, find_itr->second);
                }
            }
        }

        write_sbom(paths, action, abi_info.heuristic_resources);
        write_binary_control_file(paths, *bcf);
        return {BuildResult::SUCCEEDED, std::move(bcf)};
    }

    static ExtendedBuildResult do_build_package_and_clean_buildtrees(const VcpkgCmdArguments& args,
                                                                     const VcpkgPaths& paths,
                                                                     const InstallPlanAction& action,
                                                                     bool all_dependencies_satisfied)
    {
        auto result = do_build_package(args, paths, action, all_dependencies_satisfied);

        if (action.build_options.clean_buildtrees == CleanBuildtrees::YES)
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

    static void abi_entries_from_abi_info(const AbiInfo& abi_info, std::vector<AbiEntry>& abi_tag_entries)
    {
        const auto& pre_build_info = *abi_info.pre_build_info;
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
    }

    struct AbiTagAndFiles
    {
        const std::string* triplet_abi;
        std::string tag;
        Path tag_file;

        std::vector<Path> files;
        std::vector<std::string> hashes;
        Json::Value heuristic_resources;
    };

    static Optional<AbiTagAndFiles> compute_abi_tag(const VcpkgPaths& paths,
                                                    const InstallPlanAction& action,
                                                    Span<const AbiEntry> dependency_abis)
    {
        auto& fs = paths.get_filesystem();
        Triplet triplet = action.spec.triplet();

        if (action.build_options.use_head_version == UseHeadVersion::YES)
        {
            Debug::print("Binary caching for package ", action.spec, " is disabled due to --head\n");
            return nullopt;
        }
        if (action.build_options.editable == Editable::YES)
        {
            Debug::print("Binary caching for package ", action.spec, " is disabled due to --editable\n");
            return nullopt;
        }
        for (auto&& dep_abi : dependency_abis)
        {
            if (dep_abi.value.empty())
            {
                Debug::print("Binary caching for package ",
                             action.spec,
                             " is disabled due to missing abi info for ",
                             dep_abi.key,
                             '\n');
                return nullopt;
            }
        }

        std::vector<AbiEntry> abi_tag_entries(dependency_abis.begin(), dependency_abis.end());

        const auto& abi_info = action.abi_info.value_or_exit(VCPKG_LINE_INFO);
        const auto& triplet_abi = paths.get_triplet_info(abi_info);
        abi_tag_entries.emplace_back("triplet", triplet.canonical_name());
        abi_tag_entries.emplace_back("triplet_abi", triplet_abi);
        abi_entries_from_abi_info(abi_info, abi_tag_entries);

        // If there is an unusually large number of files in the port then
        // something suspicious is going on.  Rather than hash all of them
        // just mark the port as no-hash
        constexpr int max_port_file_count = 100;

        std::string portfile_cmake_contents;
        std::vector<Path> files;
        std::vector<std::string> hashes;
        auto&& port_dir = action.source_control_file_and_location.value_or_exit(VCPKG_LINE_INFO).source_location;
        size_t port_file_count = 0;
        Path abs_port_file;
        for (auto& port_file : fs.get_regular_files_recursive_lexically_proximate(port_dir, VCPKG_LINE_INFO))
        {
            if (port_file.filename() == ".DS_Store")
            {
                continue;
            }
            abs_port_file = port_dir;
            abs_port_file /= port_file;

            if (port_file.extension() == ".cmake")
            {
                portfile_cmake_contents += fs.read_contents(abs_port_file, VCPKG_LINE_INFO);
            }

            auto hash =
                vcpkg::Hash::get_file_hash(fs, abs_port_file, Hash::Algorithm::Sha256).value_or_exit(VCPKG_LINE_INFO);
            abi_tag_entries.emplace_back(port_file, hash);
            files.push_back(port_file);
            hashes.push_back(std::move(hash));

            ++port_file_count;
            if (port_file_count > max_port_file_count)
            {
                abi_tag_entries.emplace_back("no_hash_max_portfile", "");
                break;
            }
        }

        abi_tag_entries.emplace_back("cmake", paths.get_tool_version(Tools::CMAKE, stdout_sink));

        // This #ifdef is mirrored in tools.cpp's PowershellProvider
#if defined(_WIN32)
        abi_tag_entries.emplace_back("powershell", paths.get_tool_version("powershell-core", stdout_sink));
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
        static constexpr auto default_literal = StringLiteral{"default"};
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
            msg::write_unlocalized_text_to_stdout(Color::none, message);
        }

        auto abi_tag_entries_missing = Util::filter(abi_tag_entries, [](const AbiEntry& p) { return p.value.empty(); });

        if (abi_tag_entries_missing.empty())
        {
            auto current_build_tree = paths.build_dir(action.spec);
            fs.create_directory(current_build_tree, VCPKG_LINE_INFO);
            const auto abi_file_path = current_build_tree / (triplet.canonical_name() + ".vcpkg_abi_info.txt");
            fs.write_contents(abi_file_path, full_abi_info, VCPKG_LINE_INFO);

            return AbiTagAndFiles{
                &triplet_abi,
                Hash::get_file_hash(fs, abi_file_path, Hash::Algorithm::Sha256).value_or_exit(VCPKG_LINE_INFO),
                abi_file_path,
                std::move(files),
                std::move(hashes),
                run_resource_heuristics(portfile_cmake_contents)};
        }

        Debug::println(
            "Warning: abi keys are missing values:\n",
            Strings::join("", abi_tag_entries_missing, [](const AbiEntry& e) { return "    " + e.key + '\n'; }));

        return nullopt;
    }

    void compute_all_abis(const VcpkgPaths& paths,
                          ActionPlan& action_plan,
                          const CMakeVars::CMakeVarProvider& var_provider,
                          const StatusParagraphs& status_db)
    {
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

            action.abi_info = AbiInfo();
            auto& abi_info = action.abi_info.value_or_exit(VCPKG_LINE_INFO);

            abi_info.pre_build_info = std::make_unique<PreBuildInfo>(
                paths, action.spec.triplet(), var_provider.get_tag_vars(action.spec).value_or_exit(VCPKG_LINE_INFO));
            abi_info.toolset = paths.get_toolset(*abi_info.pre_build_info);

            auto maybe_abi_tag_and_file = compute_abi_tag(paths, action, dependency_abis);
            if (auto p = maybe_abi_tag_and_file.get())
            {
                abi_info.compiler_info = paths.get_compiler_info(abi_info);
                abi_info.triplet_abi = *p->triplet_abi;
                abi_info.package_abi = std::move(p->tag);
                abi_info.abi_tag_file = std::move(p->tag_file);
                abi_info.relative_port_files = std::move(p->files);
                abi_info.relative_port_hashes = std::move(p->hashes);
                abi_info.heuristic_resources.push_back(std::move(p->heuristic_resources));
            }
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
        const std::string& name = action.source_control_file_and_location.value_or_exit(VCPKG_LINE_INFO)
                                      .source_control_file->core_paragraph->name;

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
            return {BuildResult::CASCADED_DUE_TO_MISSING_DEPENDENCIES, std::move(missing_fspecs)};
        }

        if (action.build_options.only_downloads == OnlyDownloads::NO)
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
            const auto abi_package_dir = paths.package_dir(spec) / "share" / spec.name();
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
            case BuildResult::SUCCEEDED: ++succeeded; return;
            case BuildResult::BUILD_FAILED: ++build_failed; return;
            case BuildResult::POST_BUILD_CHECKS_FAILED: ++post_build_checks_failed; return;
            case BuildResult::FILE_CONFLICTS: ++file_conflicts; return;
            case BuildResult::CASCADED_DUE_TO_MISSING_DEPENDENCIES: ++cascaded_due_to_missing_dependencies; return;
            case BuildResult::EXCLUDED: ++excluded; return;
            case BuildResult::CACHE_MISSING: ++cache_missing; return;
            case BuildResult::DOWNLOADED: ++downloaded; return;
            case BuildResult::REMOVED: ++removed; return;
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
            case BuildResult::SUCCEEDED: return "SUCCEEDED";
            case BuildResult::BUILD_FAILED: return "BUILD_FAILED";
            case BuildResult::POST_BUILD_CHECKS_FAILED: return "POST_BUILD_CHECKS_FAILED";
            case BuildResult::FILE_CONFLICTS: return "FILE_CONFLICTS";
            case BuildResult::CASCADED_DUE_TO_MISSING_DEPENDENCIES: return "CASCADED_DUE_TO_MISSING_DEPENDENCIES";
            case BuildResult::EXCLUDED: return "EXCLUDED";
            case BuildResult::CACHE_MISSING: return "CACHE_MISSING";
            case BuildResult::DOWNLOADED: return "DOWNLOADED";
            case BuildResult::REMOVED: return "REMOVED";
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }
    }

    LocalizedString to_string(const BuildResult build_result)
    {
        switch (build_result)
        {
            case BuildResult::SUCCEEDED: return msg::format(msgBuildResultSucceeded);
            case BuildResult::BUILD_FAILED: return msg::format(msgBuildResultBuildFailed);
            case BuildResult::POST_BUILD_CHECKS_FAILED: return msg::format(msgBuildResultPostBuildChecksFailed);
            case BuildResult::FILE_CONFLICTS: return msg::format(msgBuildResultFileConflicts);
            case BuildResult::CASCADED_DUE_TO_MISSING_DEPENDENCIES:
                return msg::format(msgBuildResultCascadeDueToMissingDependencies);
            case BuildResult::EXCLUDED: return msg::format(msgBuildResultExcluded);
            case BuildResult::CACHE_MISSING: return msg::format(msgBuildResultCacheMissing);
            case BuildResult::DOWNLOADED: return msg::format(msgBuildResultDownloaded);
            case BuildResult::REMOVED: return msg::format(msgBuildResultRemoved);
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }
    }

    LocalizedString create_error_message(const ExtendedBuildResult& build_result, const PackageSpec& spec)
    {
        auto res = msg::format(msgBuildingPackageFailed,
                               msg::spec = spec,
                               msg::build_result = to_string_locale_invariant(build_result.code));

        if (build_result.code == BuildResult::CASCADED_DUE_TO_MISSING_DEPENDENCIES)
        {
            res.append_raw('\n').append_indent().append(msgBuildingPackageFailedDueToMissingDeps);

            for (const auto& missing_spec : build_result.unmet_dependencies)
            {
                res.append_raw('\n').append_indent(2).append_raw(missing_spec.to_string());
            }
        }

        return res;
    }

    std::string create_github_issue(const VcpkgCmdArguments& args,
                                    const ExtendedBuildResult& build_result,
                                    const VcpkgPaths& paths,
                                    const InstallPlanAction& action)
    {
        const auto& fs = paths.get_filesystem();
        const auto create_log_details = [&fs](vcpkg::Path&& path) {
            static constexpr auto MAX_LOG_LENGTH = 50'000;
            static constexpr auto START_BLOCK_LENGTH = 3'000;
            static constexpr auto START_BLOCK_MAX_LENGTH = 5'000;
            static constexpr auto END_BLOCK_LENGTH = 43'000;
            static constexpr auto END_BLOCK_MAX_LENGTH = 45'000;
            auto log = fs.read_contents(path, VCPKG_LINE_INFO);
            if (log.size() > MAX_LOG_LENGTH)
            {
                auto first_block_end = log.find_first_of('\n', START_BLOCK_LENGTH);
                if (first_block_end == std::string::npos || first_block_end > START_BLOCK_MAX_LENGTH)
                    first_block_end = START_BLOCK_LENGTH;

                auto last_block_end = log.find_last_of('\n', log.size() - END_BLOCK_LENGTH);
                if (last_block_end == std::string::npos || last_block_end < log.size() - END_BLOCK_MAX_LENGTH)
                    last_block_end = log.size() - END_BLOCK_LENGTH;

                auto skipped_lines = std::count(log.begin() + first_block_end, log.begin() + last_block_end, '\n');
                log = log.substr(0, first_block_end) + "\n...\nSkipped " + std::to_string(skipped_lines) +
                      " lines\n...\n" + log.substr(last_block_end);
            }
            while (!log.empty() && log.back() == '\n')
                log.pop_back();
            return Strings::concat(
                "<details><summary>", path.native(), "</summary>\n\n```\n", log, "\n```\n</details>");
        };
        const auto manifest = paths.get_manifest()
                                  .map([](const ManifestAndPath& manifest) {
                                      return Strings::concat("<details><summary>vcpkg.json</summary>\n\n```\n",
                                                             Json::stringify(manifest.manifest),
                                                             "\n```\n</details>\n");
                                  })
                                  .value_or("");

        const auto& abi_info = action.abi_info.value_or_exit(VCPKG_LINE_INFO);
        const auto& compiler_info = abi_info.compiler_info.value_or_exit(VCPKG_LINE_INFO);
        return Strings::concat(
            "Package: ",
            action.displayname(),
            " -> ",
            action.source_control_file_and_location.value_or_exit(VCPKG_LINE_INFO).to_version(),
            "\n\n**Host Environment**",
            "\n\n- Host: ",
            to_zstring_view(get_host_processor()),
            '-',
            get_host_os_name(),
            "\n- Compiler: ",
            compiler_info.id,
            " ",
            compiler_info.version,
            "\n-",
            paths.get_toolver_diagnostics(),
            "\n**To Reproduce**\n\n",
            Strings::concat(
                "`vcpkg ", args.get_command(), " ", Strings::join(" ", args.get_forwardable_arguments()), "`\n"),
            "\n**Failure logs**\n\n```\n",
            paths.get_filesystem().read_contents(build_result.stdoutlog.value_or_exit(VCPKG_LINE_INFO),
                                                 VCPKG_LINE_INFO),
            "\n```\n",
            Strings::join("\n", Util::fmap(build_result.error_logs, create_log_details)),
            "\n\n**Additional context**\n\n",
            manifest);
    }

    static std::string make_gh_issue_search_url(StringView spec_name)
    {
        return "https://github.com/microsoft/vcpkg/issues?q=is%3Aissue+is%3Aopen+in%3Atitle+" + spec_name.to_string();
    }

    static std::string make_gh_issue_open_url(StringView spec_name, const Path& path)
    {
        return Strings::concat("https://github.com/microsoft/vcpkg/issues/new?title=[",
                               spec_name,
                               "]+Build+error&body=Copy+issue+body+from+",
                               Strings::percent_encode(path));
    }

    LocalizedString create_user_troubleshooting_message(const InstallPlanAction& action,
                                                        const VcpkgPaths& paths,
                                                        const Optional<Path>& issue_body)
    {
        std::string package = action.displayname();
        if (auto scfl = action.source_control_file_and_location.get())
        {
            Strings::append(package, " -> ", scfl->to_version());
        }
        const auto& spec_name = action.spec.name();
        LocalizedString result = msg::format(msgBuildTroubleshootingMessage1).append_raw('\n');
        result.append_indent().append_raw(make_gh_issue_search_url(spec_name)).append_raw('\n');
        result.append(msgBuildTroubleshootingMessage2).append_raw('\n');
        if (issue_body.has_value())
        {
            auto path = issue_body.get()->generic_u8string();
            result.append_indent().append_raw(make_gh_issue_open_url(spec_name, path)).append_raw("\n");
            if (!paths.get_filesystem().find_from_PATH("gh").empty())
            {
                Command gh("gh");
                gh.string_arg("issue").string_arg("create").string_arg("-R").string_arg("microsoft/vcpkg");
                gh.string_arg("--title").string_arg(fmt::format("[{}] Build failure", spec_name));
                gh.string_arg("--body-file").string_arg(path);

                result.append(msgBuildTroubleshootingMessageGH).append_raw('\n');
                result.append_indent().append_raw(gh.command_line());
            }
        }
        else
        {
            result.append_indent()
                .append_raw(
                    "https://github.com/microsoft/vcpkg/issues/new?template=report-package-build-failure.md&title=[")
                .append_raw(spec_name)
                .append_raw("]+Build+error\n");
            result.append(msgBuildTroubleshootingMessage3, msg::package_name = spec_name).append_raw('\n');
            result.append_raw(paths.get_toolver_diagnostics()).append_raw('\n');
        }

        return result;
    }

    static BuildInfo inner_create_buildinfo(Paragraph pgh)
    {
        ParagraphParser parser(std::move(pgh));

        BuildInfo build_info;

        {
            std::string crt_linkage_as_string;
            parser.required_field(BuildInfoRequiredField::CRT_LINKAGE, crt_linkage_as_string);

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
            std::string library_linkage_as_string;
            parser.required_field(BuildInfoRequiredField::LIBRARY_LINKAGE, library_linkage_as_string);
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
        if (!version.empty()) build_info.version = std::move(version);

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

        if (const auto err = parser.error_info("PostBuildInformation"))
        {
            print_error_message(err);
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        build_info.policies = BuildPolicies(std::move(policies));

        return build_info;
    }

    BuildInfo read_build_info(const Filesystem& fs, const Path& filepath)
    {
        auto pghs = Paragraphs::get_single_paragraph(fs, filepath);
        if (!pghs)
        {
            Checks::msg_exit_maybe_upgrade(VCPKG_LINE_INFO, msgInvalidBuildInfo, msg::error_msg = pghs.error());
        }

        return inner_create_buildinfo(*pghs.get());
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
        };

        std::string empty;
        for (auto&& kv : VCPKG_OPTIONS)
        {
            const std::string& variable_value = [&]() -> const std::string& {
                auto find_itr = cmakevars.find(kv.first);
                if (find_itr == cmakevars.end())
                {
                    return empty;
                }
                else
                {
                    return find_itr->second;
                }
            }();

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
                        build_type = ConfigurationType::DEBUG;
                    else if (Strings::case_insensitive_ascii_equals(variable_value, "release"))
                        build_type = ConfigurationType::RELEASE;
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
