#include <vcpkg/base/cache.h>
#include <vcpkg/base/checks.h>
#include <vcpkg/base/chrono.h>
#include <vcpkg/base/contractual-constants.h>
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

#include <iterator>

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
    constexpr const IBuildLogsRecorder& null_build_logs_recorder = null_build_logs_recorder_instance;

    CiBuildLogsRecorder::CiBuildLogsRecorder(const Path& base_path_, int64_t minimum_last_write_time_)
        : base_path(base_path_), minimum_last_write_time(minimum_last_write_time_)
    {
    }

    void CiBuildLogsRecorder::record_build_result(const VcpkgPaths& paths,
                                                  const PackageSpec& spec,
                                                  BuildResult result) const
    {
        if (result == BuildResult::Succeeded)
        {
            return;
        }

        auto& filesystem = paths.get_filesystem();
        const auto source_path = paths.build_dir(spec);
        auto children = filesystem.get_regular_files_non_recursive(source_path, IgnoreErrors{});
        Util::erase_remove_if(children, NotExtensionCaseInsensitive{".log"});
        if (minimum_last_write_time > 0)
        {
            Util::erase_remove_if(children, [&](Path& path) {
                return filesystem.last_write_time(path, VCPKG_LINE_INFO) < minimum_last_write_time;
            });
        }
        auto target_path = base_path / spec.name();
        (void)filesystem.create_directories(target_path, VCPKG_LINE_INFO);
        if (children.empty())
        {
            auto message =
                fmt::format("There are no build logs for {} build.\n"
                            "This is usually because the build failed early and outside of a task that is logged.\n"
                            "See the console output logs from vcpkg for more information on the failure.\n",
                            spec);
            filesystem.write_contents(target_path / FileReadmeDotLog, message, VCPKG_LINE_INFO);
        }
        else
        {
            for (const Path& p : children)
            {
                filesystem.copy_file(p, target_path / p.filename(), CopyOptions::overwrite_existing, VCPKG_LINE_INFO);
            }
        }
    }

    PackagesDirAssigner::PackagesDirAssigner(const Path& packages_dir) : m_packages_dir(packages_dir) { }

    Path PackagesDirAssigner::generate(const PackageSpec& spec)
    {
        auto dir = spec.dir();
        auto& next_count = m_next_dir_count[dir];
        if (next_count != 0)
        {
            dir += fmt::format("_{}", next_count);
        }

        ++next_count;
        return m_packages_dir / dir;
    }

    bool is_package_dir_match(StringView filename, StringView spec_dir)
    {
        if (filename.size() < spec_dir.size() || StringView{filename.data(), spec_dir.size()} != spec_dir)
        {
            return false;
        }

        auto first = filename.begin() + spec_dir.size();
        const auto last = filename.end();
        if (first == last)
        {
            // exact match is a match
            return true;
        }

        if (*first != '_')
        {
            // no _ means no match
            return false;
        }

        ++first;
        if (first == last)
        {
            // there must be at least one number if we saw _, so no match
            return false;
        }

        do
        {
            if (!ParserBase::is_ascii_digit(*first))
            {
                // anything that isn't a number means no match
                return false;
            }
        } while (++first != last);
        return true;
    }

    void purge_packages_dirs(const VcpkgPaths& paths, View<std::string> spec_dirs)
    {
        auto& fs = paths.get_filesystem();
        for (const auto& package_dir : fs.get_directories_non_recursive(paths.packages(), VCPKG_LINE_INFO))
        {
            auto filename = package_dir.filename();
            if (Util::any_of(spec_dirs,
                             [&](const std::string& spec_dir) { return is_package_dir_match(filename, spec_dir); }))
            {
                fs.remove_all(package_dir, VCPKG_LINE_INFO);
            }
        }
    }

    void command_build_and_exit_ex(const VcpkgCmdArguments& args,
                                   const VcpkgPaths& paths,
                                   Triplet host_triplet,
                                   const BuildPackageOptions& build_options,
                                   const FullPackageSpec& full_spec,
                                   const PathsPortFileProvider& provider,
                                   const IBuildLogsRecorder& build_logs_recorder)
    {
        Checks::exit_with_code(
            VCPKG_LINE_INFO,
            command_build_ex(args, paths, host_triplet, build_options, full_spec, provider, build_logs_recorder));
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
        static constexpr BuildPackageOptions build_command_build_package_options{
            BuildMissing::Yes,
            AllowDownloads::Yes,
            OnlyDownloads::No,
            CleanBuildtrees::No,
            CleanPackages::No,
            CleanDownloads::No,
            BackcompatFeatures::Allow,
        };

        const FullPackageSpec spec =
            check_and_get_full_package_spec(options.command_arguments[0], default_triplet, paths.get_triplet_db())
                .value_or_exit(VCPKG_LINE_INFO);

        auto& fs = paths.get_filesystem();
        auto registry_set = paths.make_registry_set();
        PathsPortFileProvider provider(*registry_set, make_overlay_provider(fs, paths.overlay_ports));
        Checks::exit_with_code(VCPKG_LINE_INFO,
                               command_build_ex(args,
                                                paths,
                                                host_triplet,
                                                build_command_build_package_options,
                                                spec,
                                                provider,
                                                null_build_logs_recorder));
    }

    int command_build_ex(const VcpkgCmdArguments& args,
                         const VcpkgPaths& paths,
                         Triplet host_triplet,
                         const BuildPackageOptions& build_options,
                         const FullPackageSpec& full_spec,
                         const PathsPortFileProvider& provider,
                         const IBuildLogsRecorder& build_logs_recorder)
    {
        const PackageSpec& spec = full_spec.package_spec;
        auto var_provider_storage = CMakeVars::make_triplet_cmake_var_provider(paths);
        auto& var_provider = *var_provider_storage;
        var_provider.load_dep_info_vars({{spec}}, host_triplet);

        auto& fs = paths.get_filesystem();
        StatusParagraphs status_db = database_load_collapse(fs, paths.installed());
        PackagesDirAssigner packages_dir_assigner{paths.packages()};
        auto action_plan = create_feature_install_plan(
            provider,
            var_provider,
            {&full_spec, 1},
            status_db,
            packages_dir_assigner,
            {nullptr, host_triplet, UnsupportedPortAction::Error, UseHeadVersion::No, Editable::Yes});

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

        BinaryCache binary_cache(fs);
        if (!binary_cache.install_providers(args, paths, out_sink))
        {
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        const ElapsedTimer build_timer;
        const auto result =
            build_package(args, paths, host_triplet, build_options, *action, build_logs_recorder, status_db);
        msg::print(msgElapsedForPackage, msg::spec = full_spec, msg::elapsed = build_timer);
        switch (result.code)
        {
            case BuildResult::Succeeded: binary_cache.push_success(build_options.clean_packages, *action); return 0;
            case BuildResult::CascadedDueToMissingDependencies:
            {
                LocalizedString errorMsg = msg::format_error(msgBuildDependenciesMissing);
                for (const auto& p : result.unmet_dependencies)
                {
                    errorMsg.append_raw('\n').append_indent().append_raw(p.to_string());
                }

                Checks::msg_exit_with_message(VCPKG_LINE_INFO, errorMsg);
            }
            case BuildResult::BuildFailed:
            case BuildResult::PostBuildChecksFailed:
            case BuildResult::FileConflicts:
            case BuildResult::CacheMissing:
            case BuildResult::Downloaded:
            case BuildResult::Removed:
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
                msg::print(create_user_troubleshooting_message(*action, args.detected_ci(), paths, {}, nullopt));
                return 1;
            }
            case BuildResult::Excluded:
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }
    }

    StringLiteral to_string_view(BuildPolicy policy)
    {
        switch (policy)
        {
            case BuildPolicy::EMPTY_PACKAGE: return PolicyEmptyPackage;
            case BuildPolicy::DLLS_WITHOUT_LIBS: return PolicyDllsWithoutLibs;
            case BuildPolicy::DLLS_WITHOUT_EXPORTS: return PolicyDllsWithoutExports;
            case BuildPolicy::DLLS_IN_STATIC_LIBRARY: return PolicyDllsInStaticLibrary;
            case BuildPolicy::MISMATCHED_NUMBER_OF_BINARIES: return PolicyMismatchedNumberOfBinaries;
            case BuildPolicy::ONLY_RELEASE_CRT: return PolicyOnlyReleaseCrt;
            case BuildPolicy::EMPTY_INCLUDE_FOLDER: return PolicyEmptyIncludeFolder;
            case BuildPolicy::ALLOW_OBSOLETE_MSVCRT: return PolicyAllowObsoleteMsvcrt;
            case BuildPolicy::ALLOW_RESTRICTED_HEADERS: return PolicyAllowRestrictedHeaders;
            case BuildPolicy::SKIP_DUMPBIN_CHECKS: return PolicySkipDumpbinChecks;
            case BuildPolicy::SKIP_ARCHITECTURE_CHECK: return PolicySkipArchitectureCheck;
            case BuildPolicy::CMAKE_HELPER_PORT: return PolicyCMakeHelperPort;
            case BuildPolicy::SKIP_ABSOLUTE_PATHS_CHECK: return PolicySkipAbsolutePathsCheck;
            case BuildPolicy::SKIP_ALL_POST_BUILD_CHECKS: return PolicySkipAllPostBuildChecks;
            case BuildPolicy::SKIP_APPCONTAINER_CHECK: return PolicySkipAppcontainerCheck;
            case BuildPolicy::SKIP_CRT_LINKAGE_CHECK: return PolicySkipCrtLinkageCheck;
            case BuildPolicy::SKIP_MISPLACED_CMAKE_FILES_CHECK: return PolicySkipMisplacedCMakeFilesCheck;
            case BuildPolicy::SKIP_LIB_CMAKE_MERGE_CHECK: return PolicySkipLibCMakeMergeCheck;
            case BuildPolicy::ALLOW_DLLS_IN_LIB: return PolicyAllowDllsInLib;
            case BuildPolicy::SKIP_MISPLACED_REGULAR_FILES_CHECK: return PolicySkipMisplacedRegularFilesCheck;
            case BuildPolicy::SKIP_COPYRIGHT_CHECK: return PolicySkipCopyrightCheck;
            case BuildPolicy::ALLOW_KERNEL32_FROM_XBOX: return PolicyAllowKernel32FromXBox;
            case BuildPolicy::ALLOW_EXES_IN_BIN: return PolicyAllowExesInBin;
            case BuildPolicy::SKIP_USAGE_INSTALL_CHECK: return PolicySkipUsageInstallCheck;
            case BuildPolicy::ALLOW_EMPTY_FOLDERS: return PolicyAllowEmptyFolders;
            case BuildPolicy::ALLOW_DEBUG_INCLUDE: return PolicyAllowDebugInclude;
            case BuildPolicy::ALLOW_DEBUG_SHARE: return PolicyAllowDebugShare;
            case BuildPolicy::SKIP_PKGCONFIG_CHECK: return PolicySkipPkgConfigCheck;
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }
    }
    std::string to_string(BuildPolicy policy) { return to_string_view(policy).to_string(); }

    StringLiteral to_cmake_variable(BuildPolicy policy)
    {
        switch (policy)
        {
            case BuildPolicy::EMPTY_PACKAGE: return CMakeVariablePolicyEmptyPackage;
            case BuildPolicy::DLLS_WITHOUT_LIBS: return CMakeVariablePolicyDllsWithoutLibs;
            case BuildPolicy::DLLS_WITHOUT_EXPORTS: return CMakeVariablePolicyDllsWithoutExports;
            case BuildPolicy::DLLS_IN_STATIC_LIBRARY: return CMakeVariablePolicyDllsInStaticLibrary;
            case BuildPolicy::MISMATCHED_NUMBER_OF_BINARIES: return CMakeVariablePolicyMismatchedNumberOfBinaries;
            case BuildPolicy::ONLY_RELEASE_CRT: return CMakeVariablePolicyOnlyReleaseCrt;
            case BuildPolicy::EMPTY_INCLUDE_FOLDER: return CMakeVariablePolicyEmptyIncludeFolder;
            case BuildPolicy::ALLOW_OBSOLETE_MSVCRT: return CMakeVariablePolicyAllowObsoleteMsvcrt;
            case BuildPolicy::ALLOW_RESTRICTED_HEADERS: return CMakeVariablePolicyAllowRestrictedHeaders;
            case BuildPolicy::SKIP_DUMPBIN_CHECKS: return CMakeVariablePolicySkipDumpbinChecks;
            case BuildPolicy::SKIP_ARCHITECTURE_CHECK: return CMakeVariablePolicySkipArchitectureCheck;
            case BuildPolicy::CMAKE_HELPER_PORT: return CMakeVariablePolicyCMakeHelperPort;
            case BuildPolicy::SKIP_ABSOLUTE_PATHS_CHECK: return CMakeVariablePolicySkipAbsolutePathsCheck;
            case BuildPolicy::SKIP_ALL_POST_BUILD_CHECKS: return CMakeVariablePolicySkipAllPostBuildChecks;
            case BuildPolicy::SKIP_APPCONTAINER_CHECK: return CMakeVariablePolicySkipAppcontainerCheck;
            case BuildPolicy::SKIP_CRT_LINKAGE_CHECK: return CMakeVariablePolicySkipCrtLinkageCheck;
            case BuildPolicy::SKIP_MISPLACED_CMAKE_FILES_CHECK: return CMakeVariablePolicySkipMisplacedCMakeFilesCheck;
            case BuildPolicy::SKIP_LIB_CMAKE_MERGE_CHECK: return CMakeVariablePolicySkipLibCMakeMergeCheck;
            case BuildPolicy::ALLOW_DLLS_IN_LIB: return CMakeVariablePolicyAllowDllsInLib;
            case BuildPolicy::SKIP_MISPLACED_REGULAR_FILES_CHECK:
                return CMakeVariablePolicySkipMisplacedRegularFilesCheck;
            case BuildPolicy::SKIP_COPYRIGHT_CHECK: return CMakeVariablePolicySkipCopyrightCheck;
            case BuildPolicy::ALLOW_KERNEL32_FROM_XBOX: return CMakeVariablePolicyAllowKernel32FromXBox;
            case BuildPolicy::ALLOW_EXES_IN_BIN: return CMakeVariablePolicyAllowExesInBin;
            case BuildPolicy::SKIP_USAGE_INSTALL_CHECK: return CMakeVariablePolicySkipUsageInstallCheck;
            case BuildPolicy::ALLOW_EMPTY_FOLDERS: return CMakeVariablePolicyAllowEmptyFolders;
            case BuildPolicy::ALLOW_DEBUG_INCLUDE: return CMakeVariablePolicyAllowDebugInclude;
            case BuildPolicy::ALLOW_DEBUG_SHARE: return CMakeVariablePolicyAllowDebugShare;
            case BuildPolicy::SKIP_PKGCONFIG_CHECK: return CMakeVariablePolicySkipPkgConfigCheck;
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }
    }

    Optional<LinkageType> to_linkage_type(StringView str)
    {
        if (str == "dynamic") return LinkageType::Dynamic;
        if (str == "static") return LinkageType::Static;
        return nullopt;
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
            msg::println_error(msgInvalidArchitectureValue,
                               msg::value = target_architecture,
                               msg::expected = all_comma_separated_cpu_architectures());
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
                EnvironmentVariableVcpkgCommand,
                EnvironmentVariableVcpkgForceSystemBinaries,
                EnvironmentVariableXVcpkgRecursiveData,
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
            bool proxy_from_env = (get_environment_variable(EnvironmentVariableHttpProxy).has_value() ||
                                   get_environment_variable(EnvironmentVariableHttpsProxy).has_value());

            if (proxy_from_env)
            {
                msg::println(msgUseEnvVar, msg::env_var = format_environment_variable("HTTP(S)_PROXY"));
            }
            else
            {
                auto ieProxy = get_windows_ie_proxy_server();
                if (ieProxy.has_value() && !proxy_from_env)
                {
                    std::string server_storage = Strings::to_utf8(ieProxy.get()->server);
                    StringView server = server_storage;

                    // Separate settings in IE Proxy Settings, which is rare?
                    // Python implementation:
                    // https://github.com/python/cpython/blob/7215d1ae25525c92b026166f9d5cac85fb1defe1/Lib/urllib/request.py#L2655
                    if (server.contains("="))
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
                                auto& emplaced = *env.emplace(std::move(protocol), std::move(address)).first;
                                msg::println(msgSettingEnvVar,
                                             msg::env_var = format_environment_variable(emplaced.first),
                                             msg::url = emplaced.second);
                            }
                        }
                    }
                    // Specified http:// prefix
                    else if (server.starts_with("http://"))
                    {
                        msg::println(msgSettingEnvVar,
                                     msg::env_var = format_environment_variable(EnvironmentVariableHttpProxy),
                                     msg::url = server);
                        env.emplace(EnvironmentVariableHttpProxy, std::move(server_storage));
                    }
                    // Specified https:// prefix
                    else if (server.starts_with("https://"))
                    {
                        msg::println(msgSettingEnvVar,
                                     msg::env_var = format_environment_variable(EnvironmentVariableHttpsProxy),
                                     msg::url = server);
                        env.emplace(EnvironmentVariableHttpsProxy, std::move(server_storage));
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

                        env.emplace(EnvironmentVariableHttpProxy, server_storage);
                        env.emplace(EnvironmentVariableHttpsProxy, std::move(server_storage));
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

        auto find_itr = action.feature_dependencies.find(FeatureNameCore.to_string());
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
        const auto binary_control_file = package_dir / FileControl;
        fs.write_contents(binary_control_file, start, VCPKG_LINE_INFO);
    }

    static void get_generic_cmake_build_args(const VcpkgPaths& paths,
                                             Triplet triplet,
                                             const Toolset& toolset,
                                             std::vector<CMakeVariable>& out_vars)
    {
        out_vars.emplace_back(CMakeVariableCmd, "BUILD");
        out_vars.emplace_back(CMakeVariableDownloads, paths.downloads);
        out_vars.emplace_back(CMakeVariableTargetTriplet, triplet.canonical_name());
        out_vars.emplace_back(CMakeVariableTargetTripletFile, paths.get_triplet_db().get_triplet_file_path(triplet));
        out_vars.emplace_back(CMakeVariableBaseVersion, VCPKG_BASE_VERSION_AS_STRING);
        out_vars.emplace_back(CMakeVariableConcurrency, std::to_string(get_concurrency()));
        out_vars.emplace_back(CMakeVariablePlatformToolset, toolset.version);
        // Make sure GIT could be found
        out_vars.emplace_back(CMakeVariableGit, paths.get_tool_exe(Tools::GIT, out_sink));
    }

    static CompilerInfo load_compiler_info(const VcpkgPaths& paths,
                                           const PreBuildInfo& pre_build_info,
                                           const Toolset& toolset)
    {
        auto& triplet = pre_build_info.triplet;
        msg::println(msgDetectCompilerHash, msg::triplet = triplet);
        auto buildpath = paths.buildtrees() / FileDetectCompiler;

        std::vector<CMakeVariable> cmake_args{
            {CMakeVariableCurrentPortDir, paths.scripts / FileDetectCompiler},
            {CMakeVariableCurrentBuildtreesDir, buildpath},
            {CMakeVariableCurrentPackagesDir,
             paths.packages() / fmt::format("{}_{}", FileDetectCompiler, triplet.canonical_name())},
            // The detect_compiler "port" doesn't depend on the host triplet, so always natively compile
            {CMakeVariableHostTriplet, triplet.canonical_name()},
            {CMakeVariableCompilerCacheFile, paths.installed().compiler_hash_cache_file()},
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

        Optional<WriteFilePointer> out_file_storage = fs.open_for_write(stdoutlog, VCPKG_LINE_INFO);
        auto& out_file = out_file_storage.value_or_exit(VCPKG_LINE_INFO);
        auto rc = cmd_execute_and_stream_lines(cmd, settings, [&](StringView s) {
            if (s.starts_with(MarkerCompilerHash))
            {
                compiler_info.hash = s.substr(MarkerCompilerHash.size()).to_string();
            }
            if (s.starts_with(MarkerCompilerCxxVersion))
            {
                compiler_info.version = s.substr(MarkerCompilerCxxVersion.size()).to_string();
            }
            if (s.starts_with(MarkerCompilerCxxId))
            {
                compiler_info.id = s.substr(MarkerCompilerCxxId.size()).to_string();
            }
            static constexpr StringLiteral s_path_marker = "#COMPILER_CXX_PATH#";
            if (s.starts_with(s_path_marker))
            {
                const auto compiler_cxx_path = s.substr(s_path_marker.size());
                compiler_info.path.assign(compiler_cxx_path.data(), compiler_cxx_path.size());
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

        out_file_storage.clear();
        if (compiler_info.hash.empty() || !succeeded(rc))
        {
            Debug::println("Compiler information tracking can be disabled by passing --",
                           SwitchFeatureFlags,
                           "=-",
                           FeatureFlagCompilertracking);

            msg::println_error(msgErrorDetectingCompilerInfo, msg::path = stdoutlog);
            msg::write_unlocalized_text(Color::none, buf);
            Checks::msg_exit_with_error(VCPKG_LINE_INFO, msgErrorUnableToDetectCompilerInfo);
        }

        Debug::println("Detected compiler hash for triplet ", triplet, ": ", compiler_info.hash);
        if (!compiler_info.path.empty())
        {
            msg::println(msgCompilerPath, msg::path = compiler_info.path);
        }
        return compiler_info;
    }

    static std::vector<CMakeVariable> get_cmake_build_args(const VcpkgCmdArguments& args,
                                                           const VcpkgPaths& paths,
                                                           Triplet host_triplet,
                                                           const BuildPackageOptions& build_options,
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

        auto& post_portfile_includes = action.pre_build_info(VCPKG_LINE_INFO).post_portfile_includes;
        std::string all_post_portfile_includes =
            Strings::join(";", Util::fmap(post_portfile_includes, [](const Path& p) { return p.generic_u8string(); }));

        std::vector<CMakeVariable> variables{
            {CMakeVariableAllFeatures, all_features},
            {CMakeVariableCurrentBuildtreesDir, paths.build_dir(port_name)},
            {CMakeVariableCurrentPackagesDir, action.package_dir.value_or_exit(VCPKG_LINE_INFO)},
            {CMakeVariableCurrentPortDir, scfl.port_directory()},
            {CMakeVariableHostTriplet, host_triplet.canonical_name()},
            {CMakeVariableFeatures, Strings::join(";", action.feature_list)},
            {CMakeVariablePort, port_name},
            {CMakeVariableVersion, scf.to_version().text},
            {CMakeVariableUseHeadVersion, Util::Enum::to_bool(action.use_head_version) ? "1" : "0"},
            {CMakeVariableEditable, Util::Enum::to_bool(action.editable) ? "1" : "0"},
            {CMakeVariableNoDownloads, !Util::Enum::to_bool(build_options.allow_downloads) ? "1" : "0"},
            {CMakeVariableZChainloadToolchainFile, action.pre_build_info(VCPKG_LINE_INFO).toolchain_file()},
            {CMakeVariableZPostPortfileIncludes, all_post_portfile_includes},
        };

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

        if (build_options.backcompat_features == BackcompatFeatures::Prohibit)
        {
            variables.emplace_back(CMakeVariableProhibitBackcompatFeatures, "1");
        }

        get_generic_cmake_build_args(
            paths,
            action.spec.triplet(),
            action.abi_info.value_or_exit(VCPKG_LINE_INFO).toolset.value_or_exit(VCPKG_LINE_INFO),
            variables);

        if (Util::Enum::to_bool(build_options.only_downloads))
        {
            variables.emplace_back(CMakeVariableDownloadMode, "true");
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
            variables.emplace_back(CMakeVariablePortConfigs, Strings::join(";", port_configs));
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
        else if (cmake_system_name == "SunOS")
        {
            return m_paths.scripts / "toolchains/solaris.cmake";
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
        else if (cmake_system_name == "tvOS")
        {
            return m_paths.scripts / "toolchains/ios.cmake";
        }
        else if (cmake_system_name == "watchOS")
        {
            return m_paths.scripts / "toolchains/ios.cmake";
        }
        else if (cmake_system_name == "visionOS")
        {
            return m_paths.scripts / "toolchains/ios.cmake";
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
                           std::vector<Json::Object> heuristic_resources)
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
        const auto& package_dir = action.package_dir.value_or_exit(VCPKG_LINE_INFO);

        const auto json_path = package_dir / FileShare / action.spec.name() / FileVcpkgSpdxJson;
        // Gather all the files in the package directory
        // Note: For packages with many files, this sequential hashing may be slow
        std::vector<Path> package_files;
        std::vector<std::string> package_hashes;
        {
            auto maybe_relative_package_files = fs.try_get_regular_files_recursive_lexically_proximate(package_dir);
            if (auto relative_package_files = maybe_relative_package_files.get())
            {
                package_files.reserve(relative_package_files->size());
                package_hashes.reserve(relative_package_files->size());
                for (auto& file : *relative_package_files)
                {
                    auto maybe_hash = Hash::get_file_hash(fs, package_dir / file, Hash::Algorithm::Sha256);
                    if (auto hash = maybe_hash.get())
                    {
                        package_files.push_back(std::move(file));
                        package_hashes.push_back(std::move(*hash));
                    }
                }
            }
        } // destroy maybe_relative_package_files
        fs.write_contents_and_dirs(json_path,
                                   create_spdx_sbom(action,
                                                    abi.relative_port_files,
                                                    abi.relative_port_hashes,
                                                    package_files,
                                                    package_hashes,
                                                    now,
                                                    doc_ns,
                                                    std::move(heuristic_resources)),
                                   VCPKG_LINE_INFO);
    }

    static ExtendedBuildResult do_build_package(const VcpkgCmdArguments& args,
                                                const VcpkgPaths& paths,
                                                Triplet host_triplet,
                                                const BuildPackageOptions& build_options,
                                                const InstallPlanAction& action,
                                                bool all_dependencies_satisfied)
    {
        const auto& pre_build_info = action.pre_build_info(VCPKG_LINE_INFO);

        auto& fs = paths.get_filesystem();
        auto&& scfl = action.source_control_file_and_location.value_or_exit(VCPKG_LINE_INFO);

        Triplet triplet = action.spec.triplet();
        const auto& triplet_db = paths.get_triplet_db();
        const auto& triplet_file_path = triplet_db.get_triplet_file_path(triplet);

        if (triplet_db.is_community_triplet_path(triplet_file_path))
        {
            msg::print(LocalizedString::from_raw(triplet_file_path)
                           .append_raw(": ")
                           .append_raw(InfoPrefix)
                           .append(msgLoadedCommunityTriplet)
                           .append_raw('\n'));
        }
        else if (triplet_db.is_overlay_triplet_path(triplet_file_path))
        {
            msg::print(LocalizedString::from_raw(triplet_file_path)
                           .append_raw(": ")
                           .append_raw(InfoPrefix)
                           .append(msgLoadedOverlayTriplet)
                           .append_raw('\n'));
        }

        switch (scfl.kind)
        {
            case PortSourceKind::Unknown:
            case PortSourceKind::Builtin:
                // intentionally no output for these
                break;
            case PortSourceKind::Overlay:
                msg::print(LocalizedString::from_raw(scfl.port_directory())
                               .append_raw(": ")
                               .append_raw(InfoPrefix)
                               .append(msgInstallingOverlayPort)
                               .append_raw('\n'));
                break;
            case PortSourceKind::Git:
                msg::print(LocalizedString::from_raw(scfl.port_directory())
                               .append_raw(": ")
                               .append_raw(InfoPrefix)
                               .append(msgInstallingFromGitRegistry)
                               .append_raw(' ')
                               .append_raw(scfl.spdx_location)
                               .append_raw('\n'));
                break;
            case PortSourceKind::Filesystem:
                msg::print(LocalizedString::from_raw(scfl.port_directory())
                               .append_raw(": ")
                               .append_raw(InfoPrefix)
                               .append(msgInstallingFromFilesystemRegistry)
                               .append_raw('\n'));
                break;
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }

        const auto& abi_info = action.abi_info.value_or_exit(VCPKG_LINE_INFO);

        const ElapsedTimer timer;
        auto cmd = vcpkg::make_cmake_cmd(
            paths, paths.ports_cmake, get_cmake_build_args(args, paths, host_triplet, build_options, action));

        RedirectedProcessLaunchSettings settings;
        auto& env = settings.environment.emplace(
            paths.get_action_env(*abi_info.pre_build_info, abi_info.toolset.value_or_exit(VCPKG_LINE_INFO)));

        auto buildpath = paths.build_dir(action.spec);
        fs.create_directory(buildpath, VCPKG_LINE_INFO);
        env.add_entry(EnvironmentVariableGitCeilingDirectories, fs.absolute(buildpath.parent_path(), VCPKG_LINE_INFO));
        auto stdoutlog = buildpath / ("stdout-" + action.spec.triplet().canonical_name() + ".log");
        Optional<WriteFilePointer> out_file_storage = fs.open_for_write(stdoutlog, VCPKG_LINE_INFO);
        auto& out_file = out_file_storage.value_or_exit(VCPKG_LINE_INFO);
        auto return_code = cmd_execute_and_stream_data(cmd, settings, [&](StringView sv) {
            msg::write_unlocalized_text(Color::none, sv);
            Checks::msg_check_exit(VCPKG_LINE_INFO,
                                   out_file.write(sv.data(), 1, sv.size()) == sv.size(),
                                   msgErrorWhileWriting,
                                   msg::path = stdoutlog);
        });

        out_file_storage.clear();
        const auto buildtimeus = timer.microseconds();
        const auto spec_string = action.spec.to_string();
        const bool build_failed = !succeeded(return_code);
        MetricsSubmission metrics;
        if (build_failed)
        {
            // With the exception of empty or helper ports, builds in "Download Mode" result in failure.
            if (build_options.only_downloads == OnlyDownloads::Yes)
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

        const BuildInfo build_info =
            read_build_info(fs, action.package_dir.value_or_exit(VCPKG_LINE_INFO) / FileBuildInfo);
        size_t error_count = 0;
        {
            FileSink file_sink{fs, stdoutlog, Append::YES};
            TeeSink combo_sink{out_sink, file_sink};
            error_count = perform_post_build_lint_checks(action, paths, pre_build_info, build_info, combo_sink);
        };
        if (error_count != 0 && build_options.backcompat_features == BackcompatFeatures::Prohibit)
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
                                                                     Triplet host_triplet,
                                                                     const BuildPackageOptions& build_options,
                                                                     const InstallPlanAction& action,
                                                                     bool all_dependencies_satisfied)
    {
        auto result = do_build_package(args, paths, host_triplet, build_options, action, all_dependencies_satisfied);

        if (build_options.clean_buildtrees == CleanBuildtrees::Yes && result.code == BuildResult::Succeeded)
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
                AbiTagPublicAbiOverride,
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
            abi_tag_entries.emplace_back(AbiTagGrdkH, grdk_hash(fs, grdk_cache, pre_build_info));
        }
    }

    static void populate_abi_tag(const VcpkgPaths& paths,
                                 InstallPlanAction& action,
                                 std::unique_ptr<PreBuildInfo>&& proto_pre_build_info,
                                 Span<const AbiEntry> dependency_abis,
                                 PortDirAbiInfoCache& port_dir_cache,
                                 Cache<Path, Optional<std::string>>& grdk_cache)
    {
        Checks::check_exit(VCPKG_LINE_INFO, static_cast<bool>(proto_pre_build_info));
        const auto& pre_build_info = *proto_pre_build_info;
        const auto& toolset = paths.get_toolset(pre_build_info);
        auto& abi_info = action.abi_info.emplace();
        abi_info.pre_build_info = std::move(proto_pre_build_info);
        abi_info.toolset.emplace(toolset);

        if (action.use_head_version == UseHeadVersion::Yes)
        {
            Debug::print("Binary caching for package ", action.spec, " is disabled due to --head\n");
            return;
        }
        if (action.editable == Editable::Yes)
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
        abi_tag_entries.emplace_back(AbiTagTriplet, triplet_canonical_name);
        abi_tag_entries.emplace_back(AbiTagTripletAbi, triplet_abi);
        auto& fs = paths.get_filesystem();
        abi_entries_from_pre_build_info(fs, grdk_cache, pre_build_info, abi_tag_entries);

        auto&& port_dir = action.source_control_file_and_location.value_or_exit(VCPKG_LINE_INFO).port_directory();
        const auto& port_dir_cache_entry = port_dir_cache.get_lazy(port_dir, [&]() {
            PortDirAbiInfoCacheEntry port_dir_cache_entry;

            std::string portfile_cmake_contents;
            {
                auto rel_port_files = fs.get_regular_files_recursive_lexically_proximate(port_dir, VCPKG_LINE_INFO);
                Util::erase_remove_if(rel_port_files,
                                      [](const Path& port_file) { return port_file.filename() == FileDotDsStore; });
                // If there is an unusually large number of files in the port then
                // something suspicious is going on.
                constexpr int max_port_file_count = 100;
                if (rel_port_files.size() > max_port_file_count)
                {
                    msg::println_warning(msgHashPortManyFiles,
                                         msg::package_name = action.spec.name(),
                                         msg::count = rel_port_files.size());
                }
                port_dir_cache_entry.files = std::move(rel_port_files);
            }
            const auto& rel_port_files = port_dir_cache_entry.files;
            // Technically the pre_build_info is not part of the port_dir cache key, but a given port_dir is only going
            // to be associated with 1 port
            for (size_t i = 0; i < abi_info.pre_build_info->hash_additional_files.size(); ++i)
            {
                const auto& file = abi_info.pre_build_info->hash_additional_files[i];
                if (file.is_relative() || !fs.is_regular_file(file))
                {
                    Checks::msg_exit_with_message(
                        VCPKG_LINE_INFO, msgInvalidValueHashAdditionalFiles, msg::path = file);
                }
                abi_tag_entries.emplace_back(
                    fmt::format("additional_file_{}", i),
                    Hash::get_file_hash(fs, file, Hash::Algorithm::Sha256).value_or_exit(VCPKG_LINE_INFO));
            }

            for (const Path& rel_port_file : rel_port_files)
            {
                const Path abs_port_file = port_dir / rel_port_file;

                if (rel_port_file.extension() == ".cmake")
                {
                    const auto contents = fs.read_contents(abs_port_file, VCPKG_LINE_INFO);
                    portfile_cmake_contents += contents;
                    port_dir_cache_entry.hashes.push_back(vcpkg::Hash::get_string_sha256(contents));
                }
                else
                {
                    port_dir_cache_entry.hashes.push_back(
                        vcpkg::Hash::get_file_hash(fs, abs_port_file, Hash::Algorithm::Sha256)
                            .value_or_exit(VCPKG_LINE_INFO));
                }
                port_dir_cache_entry.abi_entries.emplace_back(rel_port_file, port_dir_cache_entry.hashes.back());
            }

            auto& scf = action.source_control_file_and_location.value_or_exit(VCPKG_LINE_INFO).source_control_file;
            port_dir_cache_entry.heuristic_resources =
                run_resource_heuristics(portfile_cmake_contents, scf->core_paragraph->version.text);

            auto& helpers = paths.get_cmake_script_hashes();
            for (auto&& helper : helpers)
            {
                if (Strings::case_insensitive_ascii_contains(portfile_cmake_contents, helper.first))
                {
                    port_dir_cache_entry.abi_entries.emplace_back(helper.first, helper.second);
                }
            }

            return port_dir_cache_entry;
        });

        Util::Vectors::append(abi_tag_entries, port_dir_cache_entry.abi_entries);

        {
            size_t i = 0;
            for (auto& filestr : abi_info.pre_build_info->hash_additional_files)
            {
                Path file(filestr);
                if (file.is_relative() || !fs.is_regular_file(file))
                {
                    Checks::msg_exit_with_message(
                        VCPKG_LINE_INFO, msgInvalidValueHashAdditionalFiles, msg::path = file);
                }
                const auto hash =
                    vcpkg::Hash::get_file_hash(fs, file, Hash::Algorithm::Sha256).value_or_exit(VCPKG_LINE_INFO);
                abi_tag_entries.emplace_back(fmt::format("additional_file_{}", i++), hash);
            }
        }

        for (size_t i = 0; i < abi_info.pre_build_info->post_portfile_includes.size(); ++i)
        {
            auto& file = abi_info.pre_build_info->post_portfile_includes[i];
            if (file.is_relative() || !fs.is_regular_file(file) || file.extension() != ".cmake")
            {
                Checks::msg_exit_with_message(VCPKG_LINE_INFO, msgInvalidValuePostPortfileIncludes, msg::path = file);
            }

            abi_tag_entries.emplace_back(
                fmt::format("post_portfile_include_{}", i),
                Hash::get_file_hash(fs, file, Hash::Algorithm::Sha256).value_or_exit(VCPKG_LINE_INFO));
        }

        abi_tag_entries.emplace_back(AbiTagCMake, paths.get_tool_version(Tools::CMAKE, out_sink));

        // This #ifdef is mirrored in tools.cpp's PowershellProvider
#if defined(_WIN32)
        abi_tag_entries.emplace_back(AbiTagPowershell, paths.get_tool_version("powershell-core", out_sink));
#endif

        abi_tag_entries.emplace_back(AbiTagPortsDotCMake, paths.get_ports_cmake_hash().to_string());
        abi_tag_entries.emplace_back(AbiTagPostBuildChecks, "2");
        abi_tag_entries.emplace_back(AbiTagSbomInfo, "1");
        InternalFeatureSet sorted_feature_list = action.feature_list;
        // Check that no "default" feature is present. Default features must be resolved before attempting to calculate
        // a package ABI, so the "default" should not have made it here.
        const bool has_no_pseudo_features = std::none_of(sorted_feature_list.begin(),
                                                         sorted_feature_list.end(),
                                                         [](StringView s) { return s == FeatureNameDefault; });
        Checks::check_exit(VCPKG_LINE_INFO, has_no_pseudo_features);
        Util::sort_unique_erase(sorted_feature_list);

        // Check that the "core" feature is present. After resolution into InternalFeatureSet "core" meaning "not
        // default" should have already been handled so "core" should be here.
        Checks::check_exit(VCPKG_LINE_INFO,
                           std::binary_search(sorted_feature_list.begin(), sorted_feature_list.end(), FeatureNameCore));

        abi_tag_entries.emplace_back(AbiTagFeatures, Strings::join(";", sorted_feature_list));

        Util::sort(abi_tag_entries);

        const std::string full_abi_info =
            Strings::join("", abi_tag_entries, [](const AbiEntry& p) { return p.key + " " + p.value + "\n"; });

        if (Debug::g_debugging)
        {
            std::string message = Strings::concat("[DEBUG] <abientries for ", action.spec, ">\n");
            for (const auto& entry : abi_tag_entries)
            {
                Strings::append(message, "[DEBUG]   ", entry.key, "|", entry.value, "\n");
            }
            Strings::append(message, "[DEBUG] </abientries>\n");
            msg::write_unlocalized_text(Color::none, message);
        }

        const auto abi_tag_entries_missing =
            Util::filter(abi_tag_entries, [](const AbiEntry& p) { return p.value.empty(); });
        if (!abi_tag_entries_missing.empty())
        {
            Debug::println("Warning: abi keys are missing values:\n",
                           Strings::join("\n", abi_tag_entries_missing, [](const AbiEntry& e) -> const std::string& {
                               return e.key;
                           }));
            return;
        }

        Path abi_file_path = paths.build_dir(action.spec) / (triplet_canonical_name + ".vcpkg_abi_info.txt");
        fs.write_contents_and_dirs(abi_file_path, full_abi_info, VCPKG_LINE_INFO);
        abi_info.package_abi = Hash::get_string_sha256(full_abi_info);
        abi_info.abi_tag_file.emplace(std::move(abi_file_path));
        abi_info.relative_port_files = port_dir_cache_entry.files;
        abi_info.relative_port_hashes = port_dir_cache_entry.hashes;
        abi_info.heuristic_resources.push_back(port_dir_cache_entry.heuristic_resources);
    }

    void compute_all_abis(const VcpkgPaths& paths,
                          ActionPlan& action_plan,
                          const CMakeVars::CMakeVarProvider& var_provider,
                          const StatusParagraphs& status_db)
    {
        PortDirAbiInfoCache port_dir_cache;
        compute_all_abis(paths, action_plan, var_provider, status_db, port_dir_cache);
    }

    void compute_all_abis(const VcpkgPaths& paths,
                          ActionPlan& action_plan,
                          const CMakeVars::CMakeVarProvider& var_provider,
                          const StatusParagraphs& status_db,
                          PortDirAbiInfoCache& port_dir_cache)
    {
        Cache<Path, Optional<std::string>> grdk_cache;
        for (auto it = action_plan.install_actions.begin(); it != action_plan.install_actions.end(); ++it)
        {
            auto& action = *it;
            if (action.abi_info.has_value()) continue;

            std::vector<AbiEntry> dependency_abis;
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

            populate_abi_tag(
                paths,
                action,
                std::make_unique<PreBuildInfo>(paths,
                                               action.spec.triplet(),
                                               var_provider.get_tag_vars(action.spec).value_or_exit(VCPKG_LINE_INFO)),
                dependency_abis,
                port_dir_cache,
                grdk_cache);
        }
    }

    ExtendedBuildResult build_package(const VcpkgCmdArguments& args,
                                      const VcpkgPaths& paths,
                                      Triplet host_triplet,
                                      const BuildPackageOptions& build_options,
                                      const InstallPlanAction& action,
                                      const IBuildLogsRecorder& build_logs_recorder,
                                      const StatusParagraphs& status_db)
    {
        auto& filesystem = paths.get_filesystem();
        auto& spec = action.spec;
        const std::string& name = action.source_control_file_and_location.value_or_exit(VCPKG_LINE_INFO).to_name();

        std::map<PackageSpec, std::set<std::string>> missing_fspecs;
        for (const auto& kv : action.feature_dependencies)
        {
            for (const FeatureSpec& fspec : kv.second)
            {
                if (!status_db.is_installed(fspec) && !(fspec.port() == name && fspec.triplet() == spec.triplet()))
                {
                    missing_fspecs[fspec.spec()].insert(fspec.feature());
                }
            }
        }

        const bool all_dependencies_satisfied = missing_fspecs.empty();
        if (build_options.only_downloads == OnlyDownloads::No)
        {
            if (!all_dependencies_satisfied)
            {
                return {BuildResult::CascadedDueToMissingDependencies,
                        Util::fmap(std::move(missing_fspecs),
                                   [](std::pair<PackageSpec, std::set<std::string>>&& missing_features) {
                                       return FullPackageSpec{
                                           std::move(missing_features.first),
                                           InternalFeatureSet{std::make_move_iterator(missing_features.second.begin()),
                                                              std::make_move_iterator(missing_features.second.end())}};
                                   })};
            }

            // assert that all_dependencies_satisfied is accurate above by checking that they're all installed
            for (auto&& pspec : action.package_dependencies)
            {
                if (pspec == spec)
                {
                    continue;
                }

                if (status_db.find_installed(pspec) == status_db.end())
                {
                    Checks::msg_exit_with_error(VCPKG_LINE_INFO, msgCorruptedDatabase);
                }
            }
        }

        auto& abi_info = action.abi_info.value_or_exit(VCPKG_LINE_INFO);
        ExtendedBuildResult result = do_build_package_and_clean_buildtrees(
            args, paths, host_triplet, build_options, action, all_dependencies_satisfied);
        if (abi_info.abi_tag_file)
        {
            auto& abi_file = *abi_info.abi_tag_file.get();
            const auto abi_package_dir = action.package_dir.value_or_exit(VCPKG_LINE_INFO) / FileShare / spec.name();
            const auto abi_file_in_package = abi_package_dir / FileVcpkgAbiInfo;
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
    static void append_build_result_summary_line(Message build_result_message, int count, LocalizedString& str)
    {
        if (count != 0)
        {
            str.append_indent()
                .append(msgBuildResultSummaryLine,
                        msg::build_result = msg::format(build_result_message),
                        msg::count = count)
                .append_raw('\n');
        }
    }

    LocalizedString BuildResultCounts::format(const Triplet& triplet) const
    {
        LocalizedString str;
        str.append(msgBuildResultSummaryHeader, msg::triplet = triplet).append_raw('\n');
        append_build_result_summary_line(msgBuildResultSucceeded, succeeded, str);
        append_build_result_summary_line(msgBuildResultBuildFailed, build_failed, str);
        append_build_result_summary_line(msgBuildResultPostBuildChecksFailed, post_build_checks_failed, str);
        append_build_result_summary_line(msgBuildResultFileConflicts, file_conflicts, str);
        append_build_result_summary_line(
            msgBuildResultCascadeDueToMissingDependencies, cascaded_due_to_missing_dependencies, str);
        append_build_result_summary_line(msgBuildResultExcluded, excluded, str);
        append_build_result_summary_line(msgBuildResultCacheMissing, cache_missing, str);
        append_build_result_summary_line(msgBuildResultDownloaded, downloaded, str);
        append_build_result_summary_line(msgBuildResultRemoved, removed, str);
        return str;
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

        return res.append_raw('\n').append(msgSeeURL, msg::url = docs::troubleshoot_build_failures_url);
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
                       get_host_processor(),
                       get_host_os_name());

        if (const auto* abi_info = action.abi_info.get())
        {
            if (const auto* compiler_info = abi_info->compiler_info.get())
            {
                fmt::format_to(
                    std::back_inserter(issue_body), "- Compiler: {} {}\n", compiler_info->id, compiler_info->version);
            }
        }
        fmt::format_to(
            std::back_inserter(issue_body), "- CMake Version: {}\n", paths.get_tool_version(Tools::CMAKE, null_sink));

        fmt::format_to(std::back_inserter(issue_body), "-{}\n", paths.get_toolver_diagnostics());
        fmt::format_to(std::back_inserter(issue_body),
                       "**To Reproduce**\n\n`vcpkg {} {}`\n\n",
                       args.get_command(),
                       Strings::join(" ", args.get_forwardable_arguments()));
        fmt::format_to(std::back_inserter(issue_body),
                       "**Failure logs**\n\n```\n{}\n```\n\n",
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

    static std::string make_gh_issue_open_url(StringView spec_name, StringView triplet, StringView body)
    {
        auto title = fmt::format("[{}] build error on {}", spec_name, triplet);
        return Strings::concat("https://github.com/microsoft/vcpkg/issues/new?title=",
                               Strings::percent_encode(title),
                               "&body=",
                               Strings::percent_encode(body));
    }

    static bool is_collapsible_ci_kind(CIKind kind)
    {
        switch (kind)
        {
            case CIKind::GithubActions:
            case CIKind::GitLabCI:
            case CIKind::AzurePipelines: return true;
            case CIKind::None:
            case CIKind::AppVeyor:
            case CIKind::AwsCodeBuild:
            case CIKind::CircleCI:
            case CIKind::HerokuCI:
            case CIKind::JenkinsCI:
            case CIKind::TeamCityCI:
            case CIKind::TravisCI:
            case CIKind::Generic: return false;
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }
    }

    static void append_file_collapsible(LocalizedString& output,
                                        CIKind kind,
                                        const ReadOnlyFilesystem& fs,
                                        const Path& file)
    {
        auto title = file.filename();
        auto contents = fs.read_contents(file, VCPKG_LINE_INFO);
        switch (kind)
        {
            case CIKind::GithubActions:
                // https://docs.github.com/en/actions/writing-workflows/choosing-what-your-workflow-does/workflow-commands-for-github-actions#grouping-log-lines
                output.append_raw("::group::")
                    .append_raw(title)
                    .append_raw('\n')
                    .append_raw(contents)
                    .append_raw("::endgroup::\n");
                break;
            case CIKind::GitLabCI:
            {
                // https://docs.gitlab.com/ee/ci/jobs/job_logs.html#custom-collapsible-sections
                using namespace std::chrono;
                std::string section_name;
                std::copy_if(title.begin(), title.end(), std::back_inserter(section_name), [](char c) {
                    return c == '.' || ParserBase::is_alphanum(c);
                });
                const auto timestamp = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
                output
                    .append_raw(
                        fmt::format("\\e[0Ksection_start:{}:{}[collapsed=true]\r\\e[0K", timestamp, section_name))
                    .append_raw(title)
                    .append_raw('\n')
                    .append_raw(contents)
                    .append_raw(fmt::format("\\e[0Ksection_end:{}:{}\r\\e[0K\n", timestamp, section_name));
            }
            break;
            case CIKind::AzurePipelines:
                // https://learn.microsoft.com/en-us/azure/devops/pipelines/scripts/logging-commands?view=azure-devops&tabs=bash#formatting-commands
                output.append_raw("##vso[task.uploadfile]")
                    .append_raw(file)
                    .append_raw('\n')
                    .append_raw("##[group]")
                    .append_raw(title)
                    .append_raw('\n')
                    .append_raw(contents)
                    .append_raw("##[endgroup]\n");
                break;
            case CIKind::None:
            case CIKind::AppVeyor:
            case CIKind::AwsCodeBuild:
            case CIKind::CircleCI:
            case CIKind::HerokuCI:
            case CIKind::JenkinsCI:
            case CIKind::TeamCityCI:
            case CIKind::TravisCI:
            case CIKind::Generic: Checks::unreachable(VCPKG_LINE_INFO, "CIKind not collapsible");
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }
    }

    LocalizedString create_user_troubleshooting_message(const InstallPlanAction& action,
                                                        CIKind detected_ci,
                                                        const VcpkgPaths& paths,
                                                        const std::vector<std::string>& error_logs,
                                                        const Optional<Path>& maybe_issue_body)
    {
        const auto& spec_name = action.spec.name();
        const auto& triplet_name = action.spec.triplet().to_string();
        LocalizedString result = msg::format(msgBuildTroubleshootingMessage1).append_raw('\n');
        result.append_indent().append_raw(make_gh_issue_search_url(spec_name)).append_raw('\n');
        result.append(msgBuildTroubleshootingMessage2).append_raw('\n').append_indent();

        if (auto issue_body = maybe_issue_body.get())
        {
            auto& fs = paths.get_filesystem();
            // The 'body' content is not localized because it becomes part of the posted GitHub issue
            // rather than instructions for the current user of vcpkg.
            if (is_collapsible_ci_kind(detected_ci))
            {
                auto body = fmt::format("Copy issue body from collapsed section \"{}\" in the ci log output",
                                        issue_body->filename());
                result.append_raw(make_gh_issue_open_url(spec_name, triplet_name, body)).append_raw('\n');
                append_file_collapsible(result, detected_ci, fs, *issue_body);
                for (Path error_log_path : error_logs)
                {
                    append_file_collapsible(result, detected_ci, fs, error_log_path);
                }
            }
            else
            {
                const auto path = issue_body->generic_u8string();
                auto body = fmt::format("Copy issue body from {}", path);
                result.append_raw(make_gh_issue_open_url(spec_name, triplet_name, body)).append_raw('\n');
                auto gh_path = fs.find_from_PATH("gh");
                if (!gh_path.empty())
                {
                    Command gh(gh_path[0]);
                    gh.string_arg("issue").string_arg("create").string_arg("-R").string_arg("microsoft/vcpkg");
                    gh.string_arg("--title").string_arg(
                        fmt::format("[{}] Build failure on {}", spec_name, triplet_name));
                    gh.string_arg("--body-file").string_arg(path);
                    result.append(msgBuildTroubleshootingMessageGH).append_raw('\n');
                    result.append_indent().append_raw(gh.command_line());
                }
            }
        }
        else
        {
            result
                .append_raw("https://github.com/microsoft/vcpkg/issues/"
                            "new?template=report-package-build-failure.md&title=%5B")
                .append_raw(spec_name)
                .append_raw("%5D+Build+error+on+")
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
            std::string crt_linkage_as_string = parser.required_field(ParagraphIdCrtLinkage);
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
            std::string library_linkage_as_string = parser.required_field(ParagraphIdLibraryLinkage);
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

        std::string version = parser.optional_field_or_empty(ParagraphIdVersion);
        if (!version.empty())
        {
            sanitize_version_string(version);
            build_info.detected_head_version = Version::parse(std::move(version)).value_or_exit(VCPKG_LINE_INFO);
        }

        std::unordered_map<BuildPolicy, bool> policies;
        for (size_t policy_idx = 0; policy_idx < static_cast<size_t>(BuildPolicy::COUNT); ++policy_idx)
        {
            auto policy = static_cast<BuildPolicy>(policy_idx);
            const auto setting = parser.optional_field_or_empty(to_string_view(policy));
            if (setting.empty()) continue;
            if (setting == "enabled")
                policies.emplace(policy, true);
            else if (setting == "disabled")
                policies.emplace(policy, false);
            else
                Checks::msg_exit_with_error(VCPKG_LINE_INFO,
                                            msgUnknownPolicySetting,
                                            msg::value = setting,
                                            msg::cmake_var = to_cmake_variable(policy));
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
        auto maybe_paragraph = Paragraphs::get_single_paragraph(fs, filepath);
        if (auto paragraph = maybe_paragraph.get())
        {
            return inner_create_buildinfo(filepath, std::move(*paragraph));
        }

        Checks::msg_exit_maybe_upgrade(VCPKG_LINE_INFO, msgInvalidBuildInfo, msg::error_msg = maybe_paragraph.error());
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
        Util::assign_if_set_and_nonempty(target_architecture, cmakevars, CMakeVariableTargetArchitecture);
        Util::assign_if_set_and_nonempty(cmake_system_name, cmakevars, CMakeVariableCMakeSystemName);
        Util::assign_if_set_and_nonempty(cmake_system_version, cmakevars, CMakeVariableCMakeSystemVersion);
        Util::assign_if_set_and_nonempty(platform_toolset, cmakevars, CMakeVariablePlatformToolset);
        Util::assign_if_set_and_nonempty(platform_toolset_version, cmakevars, CMakeVariablePlatformToolsetVersion);
        Util::assign_if_set_and_nonempty(visual_studio_path, cmakevars, CMakeVariableVisualStudioPath);
        Util::assign_if_set_and_nonempty(external_toolchain_file, cmakevars, CMakeVariableChainloadToolchainFile);
        if (auto value = Util::value_if_set_and_nonempty(cmakevars, CMakeVariableBuildType))
        {
            if (Strings::case_insensitive_ascii_equals(*value, "debug"))
                build_type = ConfigurationType::Debug;
            else if (Strings::case_insensitive_ascii_equals(*value, "release"))
                build_type = ConfigurationType::Release;
            else
                Checks::msg_exit_with_message(VCPKG_LINE_INFO, msgUnknownSettingForBuildType, msg::option = *value);
        }

        if (auto value = Util::value_if_set_and_nonempty(cmakevars, CMakeVariableEnvPassthrough))
        {
            passthrough_env_vars_tracked = Strings::split(*value, ';');
            passthrough_env_vars = passthrough_env_vars_tracked;
        }

        // Note that this must come after CMakeVariableEnvPassthrough since the leading values come from there
        if (auto value = Util::value_if_set_and_nonempty(cmakevars, CMakeVariableEnvPassthroughUntracked))
        {
            Util::Vectors::append(passthrough_env_vars, Strings::split(*value, ';'));
        }

        Util::assign_if_set_and_nonempty(public_abi_override, cmakevars, CMakeVariablePublicAbiOverride);
        if (auto value = Util::value_if_set_and_nonempty(cmakevars, CMakeVariableHashAdditionalFiles))
        {
            hash_additional_files =
                Util::fmap(Strings::split(*value, ';'), [](auto&& str) { return Path(std::move(str)); });
        }

        if (auto value = Util::value_if_set_and_nonempty(cmakevars, CMakeVariablePostPortfileIncludes))
        {
            post_portfile_includes =
                Util::fmap(Strings::split(*value, ';'), [](auto&& str) { return Path(std::move(str)); });
        }

        // Note that this value must come after CMakeVariableChainloadToolchainFile because its default depends upon it
        load_vcvars_env = !external_toolchain_file.has_value();
        if (auto value = Util::value_if_set_and_nonempty(cmakevars, CMakeVariableLoadVcvarsEnv))
        {
            load_vcvars_env = from_cmake_bool(*value, CMakeVariableLoadVcvarsEnv).value_or_exit(VCPKG_LINE_INFO);
        }

        if (auto value = Util::value_if_set_and_nonempty(cmakevars, CMakeVariableDisableCompilerTracking))
        {
            disable_compiler_tracking =
                from_cmake_bool(*value, CMakeVariableDisableCompilerTracking).value_or_exit(VCPKG_LINE_INFO);
        }

        if (Util::value_if_set_and_nonempty(cmakevars, CMakeVariableXBoxConsoleTarget) != nullptr)
        {
            target_is_xbox = true;
        }

        Util::assign_if_set_and_nonempty(gamedk_latest_path, cmakevars, CMakeVariableZVcpkgGameDKLatest);
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
    ExtendedBuildResult::ExtendedBuildResult(BuildResult code, std::vector<FullPackageSpec>&& unmet_deps)
        : code(code), unmet_dependencies(std::move(unmet_deps))
    {
    }
}
