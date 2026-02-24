#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/system.h>
#include <vcpkg/base/system.process.h>

#include <vcpkg/cmakevars.h>
#include <vcpkg/commands.build.h>
#include <vcpkg/commands.env.h>
#include <vcpkg/installedpaths.h>
#include <vcpkg/portfileprovider.h>
#include <vcpkg/registries.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

using namespace vcpkg;

namespace
{
    constexpr CommandSwitch SWITCHES[] = {
        {SwitchBin,
         [] {
             return msg::format(msgCmdEnvOptions,
                                msg::path = "bin/",
                                msg::env_var = format_environment_variable(EnvironmentVariablePath));
         }},
        {SwitchInclude,
         [] {
             return msg::format(msgCmdEnvOptions,
                                msg::path = "include/",
                                msg::env_var = format_environment_variable(EnvironmentVariableInclude));
         }},
        {SwitchDebugBin,
         [] {
             return msg::format(msgCmdEnvOptions,
                                msg::path = "debug/bin/",
                                msg::env_var = format_environment_variable(EnvironmentVariablePath));
         }},
        {SwitchTools,
         [] {
             return msg::format(msgCmdEnvOptions,
                                msg::path = "tools/*/",
                                msg::env_var = format_environment_variable(EnvironmentVariablePath));
         }},
        {SwitchPython,
         [] {
             return msg::format(msgCmdEnvOptions,
                                msg::path = "python/",
                                msg::env_var = format_environment_variable(EnvironmentVariablePythonPath));
         }},
    };

    void prepend_path_entry(Environment& env, StringLiteral env_var, std::string&& new_entry)
    {
        auto maybe_include_value = env.remove_entry(env_var);
        if (auto include_value = maybe_include_value.get())
        {
            new_entry.push_back(path_separator_char);
            include_value->insert(0, new_entry);
            env.add_entry(env_var, *include_value);
        }
        else
        {
            env.add_entry(env_var, new_entry);
        }
    }
} // unnamed namespace

namespace vcpkg
{
    constexpr CommandMetadata CommandEnvMetadata{
        "env",
        msgHelpEnvCommand,
        {
            "vcpkg env --triplet x64-windows",
            msgCommandEnvExample2,
            "vcpkg env \"ninja --version\" --triplet x64-windows",
        },
        Undocumented,
        AutocompletePriority::Public,
        0,
        1,
        {SWITCHES},
        nullptr,
    };

    // This command should probably optionally take a port
    void command_env_and_exit(const VcpkgCmdArguments& args,
                              const VcpkgPaths& paths,
                              Triplet triplet,
                              Triplet /*host_triplet*/)
    {
        const auto& fs = paths.get_filesystem();

        const ParsedArguments options = args.parse_arguments(CommandEnvMetadata);

        auto registry_set = paths.make_registry_set();
        PathsPortFileProvider provider(*registry_set, make_overlay_provider(fs, paths.overlay_ports));
        auto var_provider_storage = CMakeVars::make_triplet_cmake_var_provider(paths);
        auto& var_provider = *var_provider_storage;

        var_provider.load_generic_triplet_vars(triplet);

        const auto* triplet_vars = var_provider.get_generic_triplet_vars(triplet);
        Checks::check_exit(VCPKG_LINE_INFO, triplet_vars != nullptr);
        const PreBuildInfo pre_build_info(paths, triplet, *triplet_vars);
        const Toolset& toolset = paths.get_toolset(pre_build_info);

        EnvCache env_cache(false);

        ProcessLaunchSettings settings;
        auto& build_env = settings.environment.emplace(env_cache.get_action_env(paths, pre_build_info, toolset));

        const bool add_bin = Util::Sets::contains(options.switches, SwitchBin);
        const bool add_include = Util::Sets::contains(options.switches, SwitchInclude);
        const bool add_debug_bin = Util::Sets::contains(options.switches, SwitchDebugBin);
        const bool add_tools = Util::Sets::contains(options.switches, SwitchTools);
        const bool add_python = Util::Sets::contains(options.switches, SwitchPython);

        std::vector<std::string> path_vars;
        const auto current_triplet_path = paths.installed().triplet_dir(triplet);
        if (add_bin) path_vars.push_back((current_triplet_path / FileBin).native());
        if (add_debug_bin) path_vars.push_back((current_triplet_path / FileDebug / FileBin).native());
        if (add_include)
        {
            prepend_path_entry(build_env, EnvironmentVariableInclude, (current_triplet_path / FileInclude).native());
        }

        if (add_tools)
        {
            auto tools_dir = current_triplet_path / FileTools;
            path_vars.push_back(tools_dir.native());
            for (auto&& tool_dir : fs.get_directories_non_recursive(tools_dir, VCPKG_LINE_INFO))
            {
                path_vars.push_back(tool_dir.native());
            }
        }
        if (add_python)
        {
            build_env.add_entry(EnvironmentVariablePythonPath, (current_triplet_path / "python").native());
        }

        if (!path_vars.empty())
        {
            prepend_path_entry(build_env, EnvironmentVariablePath, Strings::join(path_separator, path_vars));
        }

#if defined(_WIN32)
        auto cmd = Command{"cmd"}.string_arg("/d");
        if (!options.command_arguments.empty())
        {
            cmd.string_arg("/c").raw_arg(options.command_arguments[0]);
        }

        enter_interactive_subprocess();
        auto rc = cmd_execute(cmd, settings);
        exit_interactive_subprocess();
        Checks::exit_with_code(VCPKG_LINE_INFO, static_cast<int>(rc.value_or_exit(VCPKG_LINE_INFO)));
#else  // ^^^ _WIN32 / !_WIN32 vvv
        Checks::msg_exit_with_message(VCPKG_LINE_INFO, msgEnvPlatformNotSupported);
#endif // ^^^ !_WIN32
    }
} // namespace vcpkg
