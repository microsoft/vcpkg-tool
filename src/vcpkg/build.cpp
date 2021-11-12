#include <vcpkg/base/cache.h>
#include <vcpkg/base/checks.h>
#include <vcpkg/base/chrono.h>
#include <vcpkg/base/hash.h>
#include <vcpkg/base/optional.h>
#include <vcpkg/base/stringliteral.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.print.h>
#include <vcpkg/base/system.process.h>
#include <vcpkg/base/system.proxy.h>
#include <vcpkg/base/util.h>

#include <vcpkg/binarycaching.h>
#include <vcpkg/build.h>
#include <vcpkg/buildenvironment.h>
#include <vcpkg/cmakevars.h>
#include <vcpkg/commands.h>
#include <vcpkg/commands.version.h>
#include <vcpkg/dependencies.h>
#include <vcpkg/globalstate.h>
#include <vcpkg/help.h>
#include <vcpkg/input.h>
#include <vcpkg/metrics.h>
#include <vcpkg/paragraphs.h>
#include <vcpkg/portfileprovider.h>
#include <vcpkg/postbuildlint.h>
#include <vcpkg/statusparagraphs.h>
#include <vcpkg/tools.h>
#include <vcpkg/vcpkglib.h>

using namespace vcpkg;
using vcpkg::Build::BuildResult;
using vcpkg::Parse::ParseControlErrorInfo;
using vcpkg::Parse::ParseExpected;
using vcpkg::PortFileProvider::PathsPortFileProvider;

namespace
{
    using vcpkg::PackageSpec;
    using vcpkg::VcpkgPaths;
    using vcpkg::Build::IBuildLogsRecorder;
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
    using Dependencies::InstallPlanAction;
    using Dependencies::InstallPlanType;

    void Command::perform_and_exit_ex(const VcpkgCmdArguments& args,
                                      const FullPackageSpec& full_spec,
                                      Triplet host_triplet,
                                      const SourceControlFileAndLocation& scfl,
                                      const PathsPortFileProvider& provider,
                                      BinaryCache& binary_cache,
                                      const IBuildLogsRecorder& build_logs_recorder,
                                      const VcpkgPaths& paths)
    {
        Checks::exit_with_code(
            VCPKG_LINE_INFO,
            perform_ex(args, full_spec, host_triplet, scfl, provider, binary_cache, build_logs_recorder, paths));
    }

    const CommandStructure COMMAND_STRUCTURE = {
        create_example_string("build zlib:x64-windows"),
        1,
        1,
        {{}, {}},
        nullptr,
    };

    void Command::perform_and_exit(const VcpkgCmdArguments& args,
                                   const VcpkgPaths& paths,
                                   Triplet default_triplet,
                                   Triplet host_triplet)
    {
        Checks::exit_with_code(VCPKG_LINE_INFO, perform(args, paths, default_triplet, host_triplet));
    }

    int Command::perform_ex(const VcpkgCmdArguments& args,
                            const FullPackageSpec& full_spec,
                            Triplet host_triplet,
                            const SourceControlFileAndLocation& scfl,
                            const PathsPortFileProvider& provider,
                            BinaryCache& binary_cache,
                            const IBuildLogsRecorder& build_logs_recorder,
                            const VcpkgPaths& paths)
    {
        auto var_provider_storage = CMakeVars::make_triplet_cmake_var_provider(paths);
        auto& var_provider = *var_provider_storage;
        var_provider.load_dep_info_vars({{full_spec.package_spec}}, host_triplet);

        StatusParagraphs status_db = database_load_check(paths);

        auto action_plan = Dependencies::create_feature_install_plan(
            provider, var_provider, std::vector<FullPackageSpec>{full_spec}, status_db, {host_triplet});

        var_provider.load_tag_vars(action_plan, provider, host_triplet);

        const PackageSpec& spec = full_spec.package_spec;
        const SourceControlFile& scf = *scfl.source_control_file;

        Checks::check_maybe_upgrade(
            VCPKG_LINE_INFO,
            spec.name() == scf.core_paragraph->name,
            Strings::format("The Source field inside the CONTROL file does not match the port directory: '%s' != '%s'",
                            scf.core_paragraph->name,
                            spec.name()));

        compute_all_abis(paths, action_plan, var_provider, status_db);

        InstallPlanAction* action = nullptr;
        for (auto& install_action : action_plan.already_installed)
        {
            if (install_action.spec == full_spec.package_spec)
            {
                action = &install_action;
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
        action->build_options = default_build_package_options;
        action->build_options.editable = Editable::YES;
        action->build_options.clean_buildtrees = CleanBuildtrees::NO;
        action->build_options.clean_packages = CleanPackages::NO;

        const auto build_timer = ElapsedTimer::create_started();
        const auto result = Build::build_package(args, paths, *action, binary_cache, build_logs_recorder, status_db);
        print2("Elapsed time for package ", spec, ": ", build_timer, '\n');

        if (result.code == BuildResult::CASCADED_DUE_TO_MISSING_DEPENDENCIES)
        {
            print2(Color::error, "The build command requires all dependencies to be already installed.\n");
            print2("The following dependencies are missing:\n\n");
            for (const auto& p : result.unmet_dependencies)
            {
                print2("    ", p, '\n');
            }
            print2('\n');
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        Checks::check_exit(VCPKG_LINE_INFO, result.code != BuildResult::EXCLUDED);

        if (result.code != BuildResult::SUCCEEDED)
        {
            print2(Color::error, Build::create_error_message(result.code, spec), '\n');
            print2(Build::create_user_troubleshooting_message(*action, paths), '\n');
            return 1;
        }

        return 0;
    }

    int Command::perform(const VcpkgCmdArguments& args,
                         const VcpkgPaths& paths,
                         Triplet default_triplet,
                         Triplet host_triplet)
    {
        // Build only takes a single package and all dependencies must already be installed
        const ParsedArguments options = args.parse_arguments(COMMAND_STRUCTURE);
        std::string first_arg = args.command_arguments[0];

        BinaryCache binary_cache{args};
        const FullPackageSpec spec = Input::check_and_get_full_package_spec(
            std::move(first_arg), default_triplet, COMMAND_STRUCTURE.example_text);

        Input::check_triplet(spec.package_spec.triplet(), paths);

        PathsPortFileProvider provider(paths, args.overlay_ports);
        const auto port_name = spec.package_spec.name();
        const auto* scfl = provider.get_control_file(port_name).get();

        Checks::check_maybe_upgrade(VCPKG_LINE_INFO, scfl != nullptr, "Error: Couldn't find port '%s'", port_name);
        ASSUME(scfl != nullptr);

        return perform_ex(
            args, spec, host_triplet, *scfl, provider, binary_cache, Build::null_build_logs_recorder(), paths);
    }

    void BuildCommand::perform_and_exit(const VcpkgCmdArguments& args,
                                        const VcpkgPaths& paths,
                                        Triplet default_triplet,
                                        Triplet host_triplet) const
    {
        Build::Command::perform_and_exit(args, paths, default_triplet, host_triplet);
    }
}

namespace vcpkg::Build
{
    static const std::string NAME_EMPTY_PACKAGE = "PolicyEmptyPackage";
    static const std::string NAME_DLLS_WITHOUT_LIBS = "PolicyDLLsWithoutLIBs";
    static const std::string NAME_DLLS_WITHOUT_EXPORTS = "PolicyDLLsWithoutExports";
    static const std::string NAME_DLLS_IN_STATIC_LIBRARY = "PolicyDLLsInStaticLibrary";
    static const std::string NAME_MISMATCHED_NUMBER_OF_BINARIES = "PolicyMismatchedNumberOfBinaries";
    static const std::string NAME_ONLY_RELEASE_CRT = "PolicyOnlyReleaseCRT";
    static const std::string NAME_EMPTY_INCLUDE_FOLDER = "PolicyEmptyIncludeFolder";
    static const std::string NAME_ALLOW_OBSOLETE_MSVCRT = "PolicyAllowObsoleteMsvcrt";
    static const std::string NAME_ALLOW_RESTRICTED_HEADERS = "PolicyAllowRestrictedHeaders";
    static const std::string NAME_SKIP_DUMPBIN_CHECKS = "PolicySkipDumpbinChecks";
    static const std::string NAME_SKIP_ARCHITECTURE_CHECK = "PolicySkipArchitectureCheck";
    static const std::string NAME_CMAKE_HELPER_PORT = "PolicyCmakeHelperPort";

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

    const std::string& to_string(BuildPolicy policy)
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
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }
    }

    CStringView to_cmake_variable(BuildPolicy policy)
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
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }
    }

    static const std::string NAME_BUILD_IN_DOWNLOAD = "BUILT_IN";
    static const std::string NAME_ARIA2_DOWNLOAD = "ARIA2";

    const std::string& to_string(DownloadTool tool)
    {
        switch (tool)
        {
            case DownloadTool::BUILT_IN: return NAME_BUILD_IN_DOWNLOAD;
            case DownloadTool::ARIA2: return NAME_ARIA2_DOWNLOAD;
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }
    }

    Optional<LinkageType> to_linkage_type(const std::string& str)
    {
        if (str == "dynamic") return LinkageType::DYNAMIC;
        if (str == "static") return LinkageType::STATIC;
        return nullopt;
    }

    namespace BuildInfoRequiredField
    {
        static const std::string CRT_LINKAGE = "CRTLinkage";
        static const std::string LIBRARY_LINKAGE = "LibraryLinkage";
    }

    static CStringView to_vcvarsall_target(const std::string& cmake_system_name)
    {
        if (cmake_system_name.empty()) return "";
        if (cmake_system_name == "Windows") return "";
        if (cmake_system_name == "WindowsStore") return "store";

        Checks::exit_maybe_upgrade(VCPKG_LINE_INFO,
                                   "Error: Could not map VCPKG_CMAKE_SYSTEM_NAME '%s' to a vcvarsall platform. "
                                   "Supported systems are '', 'Windows' and 'WindowsStore'.",
                                   cmake_system_name);
    }

    static CStringView to_vcvarsall_toolchain(const std::string& target_architecture,
                                              const Toolset& toolset,
                                              View<Toolset> all_toolsets)
    {
#if defined(_WIN32)
        auto maybe_target_arch = to_cpu_architecture(target_architecture);
        Checks::check_maybe_upgrade(
            VCPKG_LINE_INFO, maybe_target_arch.has_value(), "Invalid architecture string: %s", target_architecture);
        auto target_arch = maybe_target_arch.value_or_exit(VCPKG_LINE_INFO);
        auto host_architectures = get_supported_host_architectures();

        for (auto&& host : host_architectures)
        {
            const auto it = Util::find_if(toolset.supported_architectures, [&](const ToolsetArchOption& opt) {
                return host == opt.host_arch && target_arch == opt.target_arch;
            });
            if (it != toolset.supported_architectures.end()) return it->name;
        }

        print2("Error: Unsupported toolchain combination.\n");
        print2("Target was ",
               target_architecture,
               " but the chosen Visual Studio instance supports:\n    ",
               Strings::join(
                   ", ", toolset.supported_architectures, [](const ToolsetArchOption& t) { return t.name.c_str(); }),
               "\nVcpkg selected ",
               toolset.visual_studio_root_path,
               " as the Visual Studio instance.\nDetected instances:\n",
               Strings::join("",
                             all_toolsets,
                             [](const Toolset& t) { return Strings::concat("    ", t.visual_studio_root_path, '\n'); }),
               "\nSee "
               "https://github.com/microsoft/vcpkg/blob/master/docs/users/triplets.md#VCPKG_VISUAL_STUDIO_PATH "
               "for more information.\n");
        Checks::exit_maybe_upgrade(VCPKG_LINE_INFO);
#else
        (void)target_architecture;
        (void)toolset;
        (void)all_toolsets;
        Checks::exit_with_message(VCPKG_LINE_INFO,
                                  "Error: vcvars-based toolchains are only usable on Windows platforms.");
#endif
    }

#if defined(_WIN32)
    const Environment& EnvCache::get_action_env(const VcpkgPaths& paths, const AbiInfo& abi_info)
    {
        auto build_env_cmd =
            make_build_env_cmd(*abi_info.pre_build_info, abi_info.toolset.value_or_exit(VCPKG_LINE_INFO), paths);

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

            for (auto var : s_extra_vars)
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
                print2("-- Using HTTP(S)_PROXY in environment variables.\n");
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
                                print2("-- Setting ", protocol, " environment variables to ", address, "\n");
                            }
                        }
                    }
                    // Specified http:// prefix
                    else if (Strings::starts_with(server, "http://"))
                    {
                        print2("-- Setting HTTP_PROXY environment variables to ", server, "\n");
                        env.emplace("HTTP_PROXY", server);
                    }
                    // Specified https:// prefix
                    else if (Strings::starts_with(server, "https://"))
                    {
                        print2("-- Setting HTTPS_PROXY environment variables to ", server, "\n");
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
                        print2("-- Automatically setting HTTP(S)_PROXY environment variables to ", server, "\n");

                        env.emplace("HTTP_PROXY", server.c_str());
                        env.emplace("HTTPS_PROXY", server.c_str());
                    }
                }
            }
            return {env};
        });

        return base_env.cmd_cache.get_lazy(build_env_cmd, [&]() {
            const Path& powershell_exe_path = paths.get_tool_exe("powershell-core");
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
        return cache.get_lazy(
            tcfile, [&]() { return Hash::get_file_hash(VCPKG_LINE_INFO, fs, tcfile, Hash::Algorithm::Sha256); });
    }

    const EnvCache::TripletMapEntry& EnvCache::get_triplet_cache(const Filesystem& fs, const Path& p)
    {
        return m_triplet_cache.get_lazy(p, [&]() -> TripletMapEntry {
            return TripletMapEntry{Hash::get_file_hash(VCPKG_LINE_INFO, fs, p, Hash::Algorithm::Sha256)};
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

    vcpkg::Command make_build_env_cmd(const PreBuildInfo& pre_build_info,
                                      const Toolset& toolset,
                                      const VcpkgPaths& paths)
    {
        if (!pre_build_info.using_vcvars()) return {};

        const char* tonull = " >nul";
        if (Debug::g_debugging)
        {
            tonull = "";
        }

        const auto arch = to_vcvarsall_toolchain(pre_build_info.target_architecture, toolset, paths.get_all_toolsets());
        const auto target = to_vcvarsall_target(pre_build_info.cmake_system_name);

        return vcpkg::Command{"cmd"}.string_arg("/c").raw_arg(
            Strings::format(R"("%s" %s %s %s %s 2>&1 <NUL)",
                            toolset.vcvarsall,
                            Strings::join(" ", toolset.vcvarsall_options),
                            arch,
                            target,
                            tonull));
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
            start += "\n" + Strings::serialize(feature);
        }
        const auto binary_control_file = paths.packages() / bcf.core_paragraph.dir() / "CONTROL";
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
                                  {"VCPKG_BASE_VERSION", Commands::Version::base_version()},
                                  {"VCPKG_CONCURRENCY", std::to_string(get_concurrency())},
                                  {"VCPKG_PLATFORM_TOOLSET", toolset.version.c_str()},
                              });
        if (!get_environment_variable("VCPKG_FORCE_SYSTEM_BINARIES").has_value())
        {
            const Path& git_exe_path = paths.get_tool_exe(Tools::GIT);
            out_vars.push_back({"GIT", git_exe_path});
        }
    }

    static CompilerInfo load_compiler_info(const VcpkgPaths& paths, const AbiInfo& abi_info)
    {
        auto triplet = abi_info.pre_build_info->triplet;
        print2("Detecting compiler hash for triplet ", triplet, "...\n");
        auto buildpath = paths.buildtrees() / "detect_compiler";

#if !defined(_WIN32)
        // TODO: remove when vcpkg.exe is in charge for acquiring tools. Change introduced in vcpkg v0.0.107.
        // bootstrap should have already downloaded ninja, but making sure it is present in case it was deleted.
        (void)(paths.get_tool_exe(Tools::NINJA));
#endif
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
        if (!fs.exists(buildpath, IgnoreErrors{}))
        {
            std::error_code err;
            fs.create_directory(buildpath, err);
            Checks::check_exit(
                VCPKG_LINE_INFO, !err.value(), "Failed to create directory '%s', code: %d", buildpath, err.value());
        }
        auto stdoutlog = buildpath / ("stdout-" + triplet.canonical_name() + ".log");
        CompilerInfo compiler_info;
        std::string buf;

        int rc;
        {
            const auto out_file = fs.open_for_write(stdoutlog, VCPKG_LINE_INFO);
            rc = cmd_execute_and_stream_lines(
                command,
                [&](StringView s) {
                    static const StringLiteral s_hash_marker = "#COMPILER_HASH#";
                    if (Strings::starts_with(s, s_hash_marker))
                    {
                        compiler_info.hash = s.substr(s_hash_marker.size()).to_string();
                    }
                    static const StringLiteral s_version_marker = "#COMPILER_CXX_VERSION#";
                    if (Strings::starts_with(s, s_version_marker))
                    {
                        compiler_info.version = s.substr(s_version_marker.size()).to_string();
                    }
                    static const StringLiteral s_id_marker = "#COMPILER_CXX_ID#";
                    if (Strings::starts_with(s, s_id_marker))
                    {
                        compiler_info.id = s.substr(s_id_marker.size()).to_string();
                    }
                    Debug::print(s, '\n');
                    const auto old_buf_size = buf.size();
                    Strings::append(buf, s, '\n');
                    const auto write_size = buf.size() - old_buf_size;
                    Checks::check_exit(VCPKG_LINE_INFO,
                                       out_file.write(buf.c_str() + old_buf_size, 1, write_size) == write_size,
                                       "Error occurred while writing '%s'",
                                       stdoutlog);
                },
                env);
        } // close out_file

        if (compiler_info.hash.empty() || rc != 0)
        {
            Debug::print("Compiler information tracking can be disabled by passing --",
                         VcpkgCmdArguments::FEATURE_FLAGS_ARG,
                         "=-",
                         VcpkgCmdArguments::COMPILER_TRACKING_FEATURE,
                         "\n");

            print2("Error: while detecting compiler information:\nThe log content at ", stdoutlog, " is:\n", buf);
            Checks::exit_with_message(VCPKG_LINE_INFO,
                                      "Error: vcpkg was unable to detect the active compiler's information. See above "
                                      "for the CMake failure output.");
        }

        Debug::print("Detected compiler hash for triplet ", triplet, ": ", compiler_info.hash, "\n");
        return compiler_info;
    }

    static std::vector<CMakeVariable> get_cmake_build_args(const VcpkgCmdArguments& args,
                                                           const VcpkgPaths& paths,
                                                           const Dependencies::InstallPlanAction& action)
    {
#if !defined(_WIN32)
        // TODO: remove when vcpkg.exe is in charge for acquiring tools. Change introduced in vcpkg v0.0.107.
        // bootstrap should have already downloaded ninja, but making sure it is present in case it was deleted.
        (void)(paths.get_tool_exe(Tools::NINJA));
#endif
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
            {"VCPKG_USE_HEAD_VERSION", Util::Enum::to_bool(action.build_options.use_head_version) ? "1" : "0"},
            {"_VCPKG_DOWNLOAD_TOOL", to_string(action.build_options.download_tool)},
            {"_VCPKG_EDITABLE", Util::Enum::to_bool(action.build_options.editable) ? "1" : "0"},
            {"_VCPKG_NO_DOWNLOADS", !Util::Enum::to_bool(action.build_options.allow_downloads) ? "1" : "0"},
        };

        if (action.build_options.download_tool == DownloadTool::ARIA2)
        {
            variables.push_back({"ARIA2", paths.get_tool_exe(Tools::ARIA2)});
        }

        for (auto cmake_arg : args.cmake_args)
        {
            variables.push_back(CMakeVariable{cmake_arg});
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
            variables.push_back({"VCPKG_DOWNLOAD_MODE", "true"});
        }

        const Filesystem& fs = paths.get_filesystem();

        std::vector<std::string> port_configs;
        for (const PackageSpec& dependency : action.package_dependencies)
        {
            const Path port_config_path = paths.installed() / dependency.triplet().canonical_name() / "share" /
                                          dependency.name() / "vcpkg-port-config.cmake";

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
        else if (cmake_system_name.empty() || cmake_system_name == "Windows" || cmake_system_name == "WindowsStore")
        {
            return m_paths.scripts / "toolchains/windows.cmake";
        }
        else
        {
            Checks::exit_maybe_upgrade(VCPKG_LINE_INFO,
                                       "Unable to determine toolchain to use for triplet %s with CMAKE_SYSTEM_NAME %s; "
                                       "maybe you meant to use VCPKG_CHAINLOAD_TOOLCHAIN_FILE?",
                                       triplet,
                                       cmake_system_name);
        }
    }

    static ExtendedBuildResult do_build_package(const VcpkgCmdArguments& args,
                                                const VcpkgPaths& paths,
                                                const Dependencies::InstallPlanAction& action)
    {
        const auto& pre_build_info = action.pre_build_info(VCPKG_LINE_INFO);

        auto& fs = paths.get_filesystem();
        auto&& scfl = action.source_control_file_and_location.value_or_exit(VCPKG_LINE_INFO);

        Triplet triplet = action.spec.triplet();
        const auto& triplet_file_path = paths.get_triplet_file_path(triplet);

        if (Strings::starts_with(triplet_file_path, paths.community_triplets))
        {
            vcpkg::printf(vcpkg::Color::warning,
                          "-- Using community triplet %s. This triplet configuration is not guaranteed to succeed.\n",
                          triplet.canonical_name());
            vcpkg::printf("-- [COMMUNITY] Loading triplet configuration from: %s\n", triplet_file_path);
        }
        else if (!Strings::starts_with(triplet_file_path, paths.triplets))
        {
            vcpkg::printf("-- [OVERLAY] Loading triplet configuration from: %s\n", triplet_file_path);
        }

        if (!Strings::starts_with(scfl.source_location, paths.builtin_ports_directory()))
        {
            vcpkg::printf("-- Installing port from location: %s\n", scfl.source_location);
        }

        const auto timer = ElapsedTimer::create_started();

        auto command = vcpkg::make_cmake_cmd(paths, paths.ports_cmake, get_cmake_build_args(args, paths, action));

        const auto& env = paths.get_action_env(action.abi_info.value_or_exit(VCPKG_LINE_INFO));

        auto buildpath = paths.buildtrees() / action.spec.name();
        if (!fs.exists(buildpath, IgnoreErrors{}))
        {
            std::error_code err;
            fs.create_directory(buildpath, err);
            Checks::check_exit(
                VCPKG_LINE_INFO, !err.value(), "Failed to create directory '%s', code: %d", buildpath, err.value());
        }
        auto stdoutlog = buildpath / ("stdout-" + action.spec.triplet().canonical_name() + ".log");
        int return_code;
        {
            auto out_file = fs.open_for_write(stdoutlog, VCPKG_LINE_INFO);
            return_code = cmd_execute_and_stream_data(
                command,
                [&](StringView sv) {
                    print2(sv);
                    Checks::check_exit(VCPKG_LINE_INFO,
                                       out_file.write(sv.data(), 1, sv.size()) == sv.size(),
                                       "Error occurred while writing '%s'",
                                       stdoutlog);
                },
                env);
        } // close out_file

        // With the exception of empty packages, builds in "Download Mode" always result in failure.
        if (action.build_options.only_downloads == Build::OnlyDownloads::YES)
        {
            // TODO: Capture executed command output and evaluate whether the failure was intended.
            // If an unintended error occurs then return a BuildResult::DOWNLOAD_FAILURE status.
            return BuildResult::DOWNLOADED;
        }

        const auto buildtimeus = timer.microseconds();
        const auto spec_string = action.spec.to_string();

        {
            LockGuardPtr<Metrics> metrics(g_metrics);
            metrics->track_buildtime(Hash::get_string_hash(spec_string, Hash::Algorithm::Sha256) + ":[" +
                                         Strings::join(",",
                                                       action.feature_list,
                                                       [](const std::string& feature) {
                                                           return Hash::get_string_hash(feature,
                                                                                        Hash::Algorithm::Sha256);
                                                       }) +
                                         "]",
                                     buildtimeus);
            if (return_code != 0)
            {
                metrics->track_property("error", "build failed");
                metrics->track_property("build_error", spec_string);
                return BuildResult::BUILD_FAILED;
            }
        }

        const BuildInfo build_info = read_build_info(fs, paths.build_info_file_path(action.spec));
        const size_t error_count =
            PostBuildLint::perform_all_checks(action.spec, paths, pre_build_info, build_info, scfl.source_location);

        auto find_itr = action.feature_dependencies.find("core");
        Checks::check_exit(VCPKG_LINE_INFO, find_itr != action.feature_dependencies.end());

        std::unique_ptr<BinaryControlFile> bcf = create_binary_control_file(*scfl.source_control_file->core_paragraph,
                                                                            triplet,
                                                                            build_info,
                                                                            action.public_abi(),
                                                                            std::move(find_itr->second));

        if (error_count != 0 && action.build_options.backcompat_features == BackcompatFeatures::PROHIBIT)
        {
            return BuildResult::POST_BUILD_CHECKS_FAILED;
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
                        *scfl.source_control_file->core_paragraph, *f_pgh, triplet, std::move(find_itr->second));
                }
            }
        }

        write_binary_control_file(paths, *bcf);
        return {BuildResult::SUCCEEDED, std::move(bcf)};
    }

    static ExtendedBuildResult do_build_package_and_clean_buildtrees(const VcpkgCmdArguments& args,
                                                                     const VcpkgPaths& paths,
                                                                     const Dependencies::InstallPlanAction& action)
    {
        auto result = do_build_package(args, paths, action);

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

    struct AbiTagAndFile
    {
        const std::string* triplet_abi;
        std::string tag;
        Path tag_file;
    };

    static Optional<AbiTagAndFile> compute_abi_tag(const VcpkgPaths& paths,
                                                   const Dependencies::InstallPlanAction& action,
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
        const int max_port_file_count = 100;

        auto&& port_dir = action.source_control_file_and_location.value_or_exit(VCPKG_LINE_INFO).source_location;
        size_t port_file_count = 0;
        for (auto& port_file : fs.get_regular_files_recursive(port_dir, VCPKG_LINE_INFO))
        {
            abi_tag_entries.emplace_back(
                port_file.filename(),
                vcpkg::Hash::get_file_hash(VCPKG_LINE_INFO, fs, port_file, Hash::Algorithm::Sha256));

            ++port_file_count;
            if (port_file_count > max_port_file_count)
            {
                abi_tag_entries.emplace_back("no_hash_max_portfile", "");
                break;
            }
        }

        abi_tag_entries.emplace_back("cmake", paths.get_tool_version(Tools::CMAKE));

#if defined(_WIN32)
        abi_tag_entries.emplace_back("powershell", paths.get_tool_version("powershell-core"));
#endif

        auto& helpers = paths.get_cmake_script_hashes();
        auto portfile_contents = fs.read_contents(port_dir / "portfile.cmake", VCPKG_LINE_INFO);
        for (auto&& helper : helpers)
        {
            if (Strings::case_insensitive_ascii_contains(portfile_contents, helper.first))
            {
                abi_tag_entries.emplace_back(helper.first, helper.second);
            }
        }

        abi_tag_entries.emplace_back("ports.cmake", paths.get_ports_cmake_hash().to_string());
        abi_tag_entries.emplace_back("post_build_checks", "2");
        std::vector<std::string> sorted_feature_list = action.feature_list;
        Util::sort(sorted_feature_list);
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
            print2(message);
        }

        auto abi_tag_entries_missing = Util::filter(abi_tag_entries, [](const AbiEntry& p) { return p.value.empty(); });

        if (abi_tag_entries_missing.empty())
        {
            auto current_build_tree = paths.build_dir(action.spec);
            fs.create_directory(current_build_tree, VCPKG_LINE_INFO);
            const auto abi_file_path = current_build_tree / (triplet.canonical_name() + ".vcpkg_abi_info.txt");
            fs.write_contents(abi_file_path, full_abi_info, VCPKG_LINE_INFO);

            return AbiTagAndFile{&triplet_abi,
                                 Hash::get_file_hash(VCPKG_LINE_INFO, fs, abi_file_path, Hash::Algorithm::Sha256),
                                 abi_file_path};
        }

        Debug::print(
            "Warning: abi keys are missing values:\n",
            Strings::join("", abi_tag_entries_missing, [](const AbiEntry& e) { return "    " + e.key + "\n"; }),
            "\n");

        return nullopt;
    }

    void compute_all_abis(const VcpkgPaths& paths,
                          Dependencies::ActionPlan& action_plan,
                          const CMakeVars::CMakeVarProvider& var_provider,
                          const StatusParagraphs& status_db)
    {
        using Dependencies::InstallPlanAction;
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
                            Checks::exit_maybe_upgrade(
                                VCPKG_LINE_INFO, "Failed to find dependency abi for %s -> %s", action.spec, pspec);
                        }

                        dependency_abis.emplace_back(AbiEntry{pspec.name(), status_it->get()->package.abi});
                    }
                    else
                    {
                        dependency_abis.emplace_back(AbiEntry{pspec.name(), it2->public_abi()});
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
            }
        }
    }

    ExtendedBuildResult build_package(const VcpkgCmdArguments& args,
                                      const VcpkgPaths& paths,
                                      const Dependencies::InstallPlanAction& action,
                                      BinaryCache& binary_cache,
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

        if (!missing_fspecs.empty() && !Util::Enum::to_bool(action.build_options.only_downloads))
        {
            return {BuildResult::CASCADED_DUE_TO_MISSING_DEPENDENCIES, std::move(missing_fspecs)};
        }

        std::vector<AbiEntry> dependency_abis;
        for (auto&& pspec : action.package_dependencies)
        {
            if (pspec == spec || Util::Enum::to_bool(action.build_options.only_downloads))
            {
                continue;
            }
            const auto status_it = status_db.find_installed(pspec);
            Checks::check_exit(VCPKG_LINE_INFO, status_it != status_db.end());
            dependency_abis.emplace_back(
                AbiEntry{status_it->get()->package.spec.name(), status_it->get()->package.abi});
        }

        auto& abi_info = action.abi_info.value_or_exit(VCPKG_LINE_INFO);
        if (!abi_info.abi_tag_file)
        {
            return do_build_package_and_clean_buildtrees(args, paths, action);
        }

        auto& abi_file = *abi_info.abi_tag_file.get();

        const auto abi_package_dir = paths.package_dir(spec) / "share" / spec.name();
        const auto abi_file_in_package = abi_package_dir / "vcpkg_abi_info.txt";

        ExtendedBuildResult result = do_build_package_and_clean_buildtrees(args, paths, action);
        build_logs_recorder.record_build_result(paths, spec, result.code);

        std::error_code ec;
        filesystem.create_directories(abi_package_dir, ec);
        if (ec)
        {
            Checks::exit_with_message(
                VCPKG_LINE_INFO,
                Strings::format(
                    "Could not create %s: %s (%d)", abi_package_dir.c_str(), ec.message().c_str(), ec.value()));
        }

        filesystem.copy_file(abi_file, abi_file_in_package, CopyOptions::none, ec);
        if (ec)
        {
            Checks::exit_with_message(VCPKG_LINE_INFO,
                                      Strings::format("Could not copy %s -> %s: %s (%d)",
                                                      abi_file,
                                                      abi_file_in_package,
                                                      ec.message().c_str(),
                                                      ec.value()));
        }

        if (result.code == BuildResult::SUCCEEDED)
        {
            binary_cache.push_success(paths, action);
        }

        return result;
    }

    const std::string& to_string(const BuildResult build_result)
    {
        static const std::string NULLVALUE_STRING = "vcpkg::Commands::Build::BuildResult_NULLVALUE";
        static const std::string SUCCEEDED_STRING = "SUCCEEDED";
        static const std::string BUILD_FAILED_STRING = "BUILD_FAILED";
        static const std::string FILE_CONFLICTS_STRING = "FILE_CONFLICTS";
        static const std::string POST_BUILD_CHECKS_FAILED_STRING = "POST_BUILD_CHECKS_FAILED";
        static const std::string CASCADED_DUE_TO_MISSING_DEPENDENCIES_STRING = "CASCADED_DUE_TO_MISSING_DEPENDENCIES";
        static const std::string EXCLUDED_STRING = "EXCLUDED";
        static const std::string CACHE_MISSING_STRING = "CACHE_MISSING";
        static const std::string DOWNLOADED_STRING = "DOWNLOADED";

        switch (build_result)
        {
            case BuildResult::NULLVALUE: return NULLVALUE_STRING;
            case BuildResult::SUCCEEDED: return SUCCEEDED_STRING;
            case BuildResult::BUILD_FAILED: return BUILD_FAILED_STRING;
            case BuildResult::POST_BUILD_CHECKS_FAILED: return POST_BUILD_CHECKS_FAILED_STRING;
            case BuildResult::FILE_CONFLICTS: return FILE_CONFLICTS_STRING;
            case BuildResult::CASCADED_DUE_TO_MISSING_DEPENDENCIES: return CASCADED_DUE_TO_MISSING_DEPENDENCIES_STRING;
            case BuildResult::EXCLUDED: return EXCLUDED_STRING;
            case BuildResult::CACHE_MISSING: return CACHE_MISSING_STRING;
            case BuildResult::DOWNLOADED: return DOWNLOADED_STRING;
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }
    }

    std::string create_error_message(const BuildResult build_result, const PackageSpec& spec)
    {
        return Strings::format("Error: Building package %s failed with: %s", spec, Build::to_string(build_result));
    }

    std::string create_user_troubleshooting_message(const InstallPlanAction& action, const VcpkgPaths& paths)
    {
#if defined(_WIN32)
        auto vcpkg_update_cmd = ".\\vcpkg";
#else
        auto vcpkg_update_cmd = "./vcpkg";
#endif

        std::string package = action.displayname();
        if (auto scfl = action.source_control_file_and_location.get())
        {
            Strings::append(package, " -> ", scfl->to_versiont());
        }
        auto description = paths.git_describe_head();
        return Strings::format("Please ensure you're using the latest portfiles with `git pull` and `%s update`, then\n"
                               "submit an issue at https://github.com/Microsoft/vcpkg/issues including:\n"
                               "  package: %s\n"
                               "  vcpkg version: %s\n"
                               "  vcpkg-tool version: %s\n"
                               "\n"
                               "Additionally, attach any relevant sections from the log files above.",
                               vcpkg_update_cmd,
                               package,
                               description.has_value() ? description.value_or_exit(VCPKG_LINE_INFO)
                                                       : "Failed to get HEAD: " + description.error(),
                               Commands::Version::version());
    }

    static BuildInfo inner_create_buildinfo(Parse::Paragraph pgh)
    {
        Parse::ParagraphParser parser(std::move(pgh));

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
                Checks::exit_with_message(VCPKG_LINE_INFO, "Invalid crt linkage type: [%s]", crt_linkage_as_string);
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
                Checks::exit_with_message(
                    VCPKG_LINE_INFO, "Invalid library linkage type: [%s]", library_linkage_as_string);
            }
        }

        std::string version = parser.optional_field("Version");
        if (!version.empty()) build_info.version = std::move(version);

        std::map<BuildPolicy, bool> policies;
        for (auto policy : ALL_POLICIES)
        {
            const auto setting = parser.optional_field(to_string(policy));
            if (setting.empty()) continue;
            if (setting == "enabled")
                policies.emplace(policy, true);
            else if (setting == "disabled")
                policies.emplace(policy, false);
            else
                Checks::exit_maybe_upgrade(
                    VCPKG_LINE_INFO, "Unknown setting for policy '%s': %s", to_string(policy), setting);
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
        const ExpectedS<Parse::Paragraph> pghs = Paragraphs::get_single_paragraph(fs, filepath);
        Checks::check_maybe_upgrade(
            VCPKG_LINE_INFO, pghs.get() != nullptr, "Invalid BUILD_INFO file for package: %s", pghs.error());
        return inner_create_buildinfo(*pghs.get());
    }

    static ExpectedS<bool> from_cmake_bool(StringView value, StringView name)
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
            return Strings::concat("Error: Unknown boolean setting for ",
                                   name,
                                   ": \"",
                                   value,
                                   "\". Valid settings are '', '1', '0', 'ON', 'OFF', 'TRUE', and 'FALSE'.");
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
                        Checks::exit_with_message(
                            VCPKG_LINE_INFO,
                            "Unknown setting for VCPKG_BUILD_TYPE: %s. Valid settings are '', 'debug' and 'release'.",
                            variable_value);
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
