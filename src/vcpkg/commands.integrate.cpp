#include <vcpkg/base/fwd/message_sinks.h>

#include <vcpkg/base/checks.h>
#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/parse.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/system.h>
#include <vcpkg/base/system.process.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.integrate.h>
#include <vcpkg/metrics.h>
#include <vcpkg/tools.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

namespace vcpkg
{
    Optional<int> find_targets_file_version(StringView contents)
    {
        constexpr static StringLiteral VERSION_START = "<!-- version ";
        constexpr static StringLiteral VERSION_END = " -->";

        auto first = contents.begin();
        const auto last = contents.end();
        for (;;)
        {
            first = Util::search_and_skip(first, last, VERSION_START);
            if (first == last)
            {
                break;
            }
            auto version_end = Util::search(first, last, VERSION_END);
            if (version_end == last)
            {
                break;
            }
            auto ver = Strings::strto<int>({first, version_end});
            if (ver.has_value() && *ver.get() >= 0)
            {
                return ver;
            }
        }
        return nullopt;
    }

    std::vector<std::string> get_bash_source_completion_lines(StringView contents)
    {
        std::vector<std::string> matches;
        auto first = contents.begin();
        const auto last = contents.end();
        while (first != last)
        {
            const auto end_of_line = std::find(first, last, '\n');
            const auto line = Strings::trim(StringView{first, end_of_line});
            if (line.starts_with("source") && line.ends_with("scripts/vcpkg_completion.bash"))
            {
                matches.emplace_back(line.data(), line.size());
            }
            first = end_of_line == last ? last : end_of_line + 1;
        }
        return matches;
    }

    ZshAutocomplete get_zsh_autocomplete_data(StringView contents)
    {
        constexpr static StringLiteral BASHCOMPINIT = "bashcompinit";
        ZshAutocomplete res{};

        auto first = contents.begin();
        const auto last = contents.end();
        while (first != last)
        {
            const auto end_of_line = std::find(first, last, '\n');
            const auto line = Strings::trim(StringView{first, end_of_line});
            const auto bashcompinit = Strings::search(line, BASHCOMPINIT);

            if (line.starts_with("source") && line.ends_with("scripts/vcpkg_completion.zsh"))
            {
                res.source_completion_lines.emplace_back(line.data(), line.size());
            }
            else if (bashcompinit != line.end())
            {
                if (line.starts_with("autoload"))
                {
                    // autoload[ a-zA-Z0-9-]+bashcompinit
                    if (std::all_of(first, bashcompinit, [](char ch) {
                            return ParserBase::is_word_char(ch) || ch == ' ' || ch == '-';
                        }))
                    {
                        res.has_autoload_bashcompinit = true;
                    }
                }
                else
                {
                    auto line_before_bashcompinit = Strings::trim(StringView{first, bashcompinit});
                    // check this is not commented out,
                    // and that it is either the first element after a && or the beginning
                    if (!line_before_bashcompinit.contains('#') &&
                        (line_before_bashcompinit.empty() || line_before_bashcompinit.ends_with("&&")))
                    {
                        res.has_bashcompinit = true;
                    }
                }
            }

            first = end_of_line == last ? last : end_of_line + 1;
        }

        return res;
    }

#if defined(_WIN32)
    static std::string create_appdata_shortcut(StringView target_path) noexcept
    {
        return fmt::format(R"###(
<Project ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <Import Condition="Exists('{}') and '$(VCPkgLocalAppDataDisabled)' == ''" Project="{}" />
</Project>
)###",
                           target_path,
                           target_path);
    }

    static constexpr StringLiteral SystemTargetsShortcut = R"###(
<Project ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <!-- version 1 -->
  <PropertyGroup>
    <VCLibPackagePath Condition="'$(VCLibPackagePath)' == ''">$(LOCALAPPDATA)\vcpkg\vcpkg.user</VCLibPackagePath>
  </PropertyGroup>
  <Import Condition="'$(VCLibPackagePath)' != '' and Exists('$(VCLibPackagePath).props')" Project="$(VCLibPackagePath).props" />
  <Import Condition="'$(VCLibPackagePath)' != '' and Exists('$(VCLibPackagePath).targets')" Project="$(VCLibPackagePath).targets" />
</Project>
)###";

    static std::string create_nuget_targets_file_contents(const Path& msbuild_vcpkg_targets_file)
    {
        return fmt::format(R"###(
<Project ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <Import Project="{}" Condition="Exists('{}')" />
  <Target Name="CheckValidPlatform" BeforeTargets="Build">
    <Error Text="Unsupported architecture combination. Remove the 'vcpkg' nuget package." Condition="'$(VCPkgEnabled)' != 'true' and '$(VCPkgDisableError)' == ''"/>
  </Target>
</Project>
)###",
                           msbuild_vcpkg_targets_file,
                           msbuild_vcpkg_targets_file);
    }
#endif // ^^^ _WIN32

#if defined(_WIN32)
    static constexpr StringLiteral NUGET_PROPS_FILE_CONTENTS = R"###(
<Project ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <PropertyGroup>
    <VCPkgLocalAppDataDisabled>true</VCPkgLocalAppDataDisabled>
  </PropertyGroup>
</Project>
)###";
#endif // ^^^ _WIN32

#if defined(_WIN32)
    static std::string get_nuget_id(const Path& vcpkg_root_dir)
    {
        std::string dir_id = vcpkg_root_dir.generic_u8string();
        std::replace(dir_id.begin(), dir_id.end(), '/', '.');
        dir_id.erase(1, 1); // Erasing the ":"

        // NuGet id cannot have invalid characters. We will only use alphanumeric and dot.
        Util::erase_remove_if(dir_id, [](char c) { return !isalnum(static_cast<unsigned char>(c)) && (c != '.'); });

        return "vcpkg." + dir_id;
    }
#endif // ^^^ _WIN32

#if defined(_WIN32)
    static std::string create_nuspec_file_contents(const Path& vcpkg_root_dir,
                                                   const std::string& nuget_id,
                                                   const std::string& nupkg_version)
    {
        static constexpr StringLiteral CONTENT_TEMPLATE = R"(
<package>
    <metadata>
        <id>@NUGET_ID@</id>
        <version>@VERSION@</version>
        <authors>vcpkg</authors>
        <description>
            This package imports all libraries currently installed in @VCPKG_DIR@. This package does not contain any libraries and instead refers to the folder directly (like a symlink).
        </description>
    </metadata>
    <files>
        <file src="vcpkg.nuget.props" target="build\native\@NUGET_ID@.props" />
        <file src="vcpkg.nuget.targets" target="build\native\@NUGET_ID@.targets" />
    </files>
</package>
)";

        std::string content = Strings::replace_all(CONTENT_TEMPLATE, "@NUGET_ID@", nuget_id);
        Strings::inplace_replace_all(content, "@VCPKG_DIR@", vcpkg_root_dir);
        Strings::inplace_replace_all(content, "@VERSION@", nupkg_version);
        return content;
    }
#endif // ^^^ _WIN32

#if defined(_WIN32)
    enum class ElevationPromptChoice
    {
        YES,
        NO
    };

    static ElevationPromptChoice elevated_cmd_execute(const std::string& param)
    {
        SHELLEXECUTEINFOW sh_ex_info{};
        sh_ex_info.cbSize = sizeof(sh_ex_info);
        sh_ex_info.fMask = SEE_MASK_NOCLOSEPROCESS;
        sh_ex_info.hwnd = nullptr;
        sh_ex_info.lpVerb = L"runas";
        sh_ex_info.lpFile = L"cmd"; // Application to start

        auto wparam = Strings::to_utf16(param);
        sh_ex_info.lpParameters = wparam.c_str(); // Additional parameters
        sh_ex_info.lpDirectory = nullptr;
        sh_ex_info.nShow = SW_HIDE;
        sh_ex_info.hInstApp = nullptr;

        if (!ShellExecuteExW(&sh_ex_info))
        {
            return ElevationPromptChoice::NO;
        }
        if (sh_ex_info.hProcess == nullptr)
        {
            return ElevationPromptChoice::NO;
        }
        WaitForSingleObject(sh_ex_info.hProcess, INFINITE);
        CloseHandle(sh_ex_info.hProcess);
        return ElevationPromptChoice::YES;
    }
#endif // ^^^ _WIN32

#if defined(_WIN32)
    static bool integrate_install_msbuild14(const Filesystem& fs)
    {
        Path OLD_SYSTEM_TARGET_FILES[] = {
            get_program_files_32_bit().value_or_exit(VCPKG_LINE_INFO) /
                "MSBuild/14.0/Microsoft.Common.Targets/ImportBefore/vcpkg.nuget.targets",
            get_program_files_32_bit().value_or_exit(VCPKG_LINE_INFO) /
                "MSBuild/14.0/Microsoft.Common.Targets/ImportBefore/vcpkg.system.targets"};

        Path SYSTEM_WIDE_TARGETS_FILE = get_program_files_32_bit().value_or_exit(VCPKG_LINE_INFO) /
                                        "MSBuild/Microsoft.Cpp/v4.0/V140/ImportBefore/Default/vcpkg.system.props";

        // TODO: This block of code should eventually be removed
        for (auto&& old_system_wide_targets_file : OLD_SYSTEM_TARGET_FILES)
        {
            if (fs.exists(old_system_wide_targets_file, IgnoreErrors{}))
            {
                const std::string param = fmt::format(R"(/d /c "DEL "{}" /Q > nul")", old_system_wide_targets_file);
                const ElevationPromptChoice user_choice = elevated_cmd_execute(param);
                switch (user_choice)
                {
                    case ElevationPromptChoice::YES: break;
                    case ElevationPromptChoice::NO: msg::println_warning(msgPreviousIntegrationFileRemains); break;
                    default: Checks::unreachable(VCPKG_LINE_INFO);
                }
            }
        }

        std::error_code ec;
        std::string system_wide_file_contents = fs.read_contents(SYSTEM_WIDE_TARGETS_FILE, ec);
        if (!ec)
        {
            auto opt = find_targets_file_version(system_wide_file_contents);
            if (opt.value_or(0) >= 1)
            {
                return true;
            }
        }

        const auto tmp_dir = fs.create_or_get_temp_directory(VCPKG_LINE_INFO);
        const auto sys_src_path = tmp_dir / "vcpkg.system.targets";
        fs.write_contents(sys_src_path, SystemTargetsShortcut, VCPKG_LINE_INFO);

        const std::string param = fmt::format(R"(/d /c "mkdir "{}" & copy "{}" "{}" /Y > nul")",
                                              SYSTEM_WIDE_TARGETS_FILE.parent_path(),
                                              sys_src_path,
                                              SYSTEM_WIDE_TARGETS_FILE);
        elevated_cmd_execute(param);
        fs.remove_all(tmp_dir, VCPKG_LINE_INFO);

        if (!fs.exists(SYSTEM_WIDE_TARGETS_FILE, IgnoreErrors{}))
        {
            msg::println_warning(msg::format(msgSystemTargetsInstallFailed, msg::path = SYSTEM_WIDE_TARGETS_FILE));
            return false;
        }

        return true;
    }
#endif // ^^^ _WIN32

    static void integrate_install(const VcpkgPaths& paths)
    {
        auto& fs = paths.get_filesystem();

        const auto cmake_toolchain = paths.buildsystems / "vcpkg.cmake";
        auto message = msg::format(msgCMakeToolChainFile, msg::path = cmake_toolchain.generic_u8string());

        auto& user_configuration_home = get_user_configuration_home().value_or_exit(VCPKG_LINE_INFO);
        fs.create_directories(user_configuration_home, VCPKG_LINE_INFO);
        fs.write_contents(user_configuration_home / FileVcpkgPathTxt, paths.root.generic_u8string(), VCPKG_LINE_INFO);

#if defined(_WIN32)
        fs.write_contents(user_configuration_home / FileVcpkgUserProps,
                          create_appdata_shortcut(paths.buildsystems_msbuild_props),
                          VCPKG_LINE_INFO);
        fs.write_contents(user_configuration_home / FileVcpkgUserTargets,
                          create_appdata_shortcut(paths.buildsystems_msbuild_targets),
                          VCPKG_LINE_INFO);

        if (!integrate_install_msbuild14(fs))
        {
            message.append_raw("\n\n").append(msgAutomaticLinkingForVS2017AndLater);
            msg::println(message);
            Checks::msg_exit_with_message(VCPKG_LINE_INFO, msgIntegrationFailedVS2015);
        }

        message.append_raw("\n\n").append(msgAutomaticLinkingForMSBuildProjects);
#endif // ^^^ _WIN32

        msg::println(Color::success, msgAppliedUserIntegration);
        msg::println(message);
        Checks::exit_success(VCPKG_LINE_INFO);
    }

    static void integrate_remove(const Filesystem& fs)
    {
        bool was_deleted = false;
        auto& user_configuration_home = get_user_configuration_home().value_or_exit(VCPKG_LINE_INFO);
#if defined(_WIN32)
        was_deleted |= fs.remove(user_configuration_home / FileVcpkgUserProps, VCPKG_LINE_INFO);
        was_deleted |= fs.remove(user_configuration_home / FileVcpkgUserTargets, VCPKG_LINE_INFO);
#endif // ^^^ _WIN32
        was_deleted |= fs.remove(user_configuration_home / FileVcpkgPathTxt, VCPKG_LINE_INFO);

        if (was_deleted)
        {
            msg::println(msgUserWideIntegrationRemoved);
        }
        else
        {
            msg::println(msgUserWideIntegrationDeleted);
        }

        Checks::exit_success(VCPKG_LINE_INFO);
    }

    static void integrate_project(const VcpkgPaths& paths)
    {
#if defined(WIN32)
        auto& fs = paths.get_filesystem();

        const Path& nuget_exe = paths.get_tool_exe(Tools::NUGET, out_sink);

        const auto tmp_dir = fs.create_or_get_temp_directory(VCPKG_LINE_INFO);
        const auto targets_file_path = tmp_dir / "vcpkg.nuget.targets";
        const auto props_file_path = tmp_dir / "vcpkg.nuget.props";
        const auto nuspec_file_path = tmp_dir / "vcpkg.nuget.nuspec";
        const std::string nuget_id = get_nuget_id(paths.root);
        const std::string nupkg_version = "1.0.0";

        fs.write_contents(
            targets_file_path, create_nuget_targets_file_contents(paths.buildsystems_msbuild_targets), VCPKG_LINE_INFO);
        fs.write_contents(props_file_path, NUGET_PROPS_FILE_CONTENTS, VCPKG_LINE_INFO);
        fs.write_contents(
            nuspec_file_path, create_nuspec_file_contents(paths.root, nuget_id, nupkg_version), VCPKG_LINE_INFO);

        // Using all forward slashes for the command line
        auto cmd = Command(nuget_exe)
                       .string_arg("pack")
                       .string_arg("-OutputDirectory")
                       .string_arg(paths.original_cwd)
                       .string_arg(nuspec_file_path);
        RedirectedProcessLaunchSettings settings;
        settings.environment = get_clean_environment();

        const auto maybe_nuget_output = flatten(cmd_execute_and_capture_output(cmd, settings), Tools::NUGET);

        if (!maybe_nuget_output)
        {
            Checks::msg_exit_with_message(VCPKG_LINE_INFO,
                                          msg::format(msgCommandFailed, msg::command_line = cmd.command_line())
                                              .append_raw('\n')
                                              .append(maybe_nuget_output.error()));
        }

        fs.remove_all(tmp_dir, VCPKG_LINE_INFO);
        const auto nuget_package = paths.original_cwd / fmt::format("{}.{}.nupkg", nuget_id, nupkg_version);
        Checks::msg_check_exit(VCPKG_LINE_INFO,
                               fs.exists(nuget_package, IgnoreErrors{}),
                               msgNugetPackageFileSucceededButCreationFailed,
                               msg::path = nuget_package);
        msg::println(Color::success, msgCreatedNuGetPackage, msg::path = nuget_package);

        auto source_path = Strings::replace_all(paths.original_cwd, "`", "``");

        msg::println(msgInstallPackageInstruction, msg::value = nuget_id, msg::path = source_path);
        Checks::exit_success(VCPKG_LINE_INFO);
#else  // ^^^ _WIN32 // !_WIN32 vvv
        (void)paths;
        Checks::msg_exit_with_error(
            VCPKG_LINE_INFO, msgIntegrateWindowsOnly, msg::command_line = "vcpkg integrate project");
#endif // ^^^ !_WIN32
    }

    static void integrate_powershell(const VcpkgPaths& paths)
    {
#if defined(_WIN32)
        static constexpr StringLiteral TITLE = "PowerShell Tab-Completion";
        const auto script_path = paths.scripts / "addPoshVcpkgToPowershellProfile.ps1";

        const auto& ps = paths.get_tool_exe("powershell-core", out_sink);
        const auto rc = cmd_execute(Command{ps}
                                        .string_arg("-NoProfile")
                                        .string_arg("-ExecutionPolicy")
                                        .string_arg("Bypass")
                                        .string_arg("-Command")
                                        .string_arg(fmt::format("& {{& '{}' }}", script_path)))
                            .value_or_exit(VCPKG_LINE_INFO);
        if (rc)
        {
            msg::println_error(msg::format(msgCommandFailed, msg::command_line = TITLE)
                                   .append_raw('\n')
                                   .append_raw(script_path.generic_u8string()));
            get_global_metrics_collector().track_string(StringMetric::Title, TITLE);
        }

        Checks::exit_with_code(VCPKG_LINE_INFO, static_cast<int>(rc));
#else  // ^^^ _WIN32 // !_WIN32 vvv
        (void)paths;
        Checks::msg_exit_with_error(
            VCPKG_LINE_INFO, msgIntegrateWindowsOnly, msg::command_line = "vcpkg integrate powershell");
#endif // ^^^ !_WIN32
    }

    static void integrate_bash(const VcpkgPaths& paths)
    {
#if defined(_WIN32)
        (void)paths;
        Checks::msg_exit_with_error(
            VCPKG_LINE_INFO, msgIntegrateNonWindowsOnly, msg::command_line = "vcpkg integrate bash");
#else // ^^^ _WIN32 // !_WIN32 vvv
        const auto home_path = get_environment_variable("HOME").value_or_exit(VCPKG_LINE_INFO);
#if defined(__APPLE__)
        const auto bashrc_path = Path{home_path} / ".bash_profile";
#else
        const auto bashrc_path = Path{home_path} / ".bashrc";
#endif

        auto& fs = paths.get_filesystem();
        const auto completion_script_path = paths.scripts / "vcpkg_completion.bash";

        auto bashrc_content = fs.read_contents(bashrc_path, VCPKG_LINE_INFO);
        auto matches = get_bash_source_completion_lines(bashrc_content);

        if (!matches.empty())
        {
            msg::println(msg::format(msgVcpkgCompletion, msg::value = "bash", msg::path = bashrc_path)
                             .append_raw(Strings::join("\n   ", matches))
                             .append_raw('\n')
                             .append(msgSuggestStartingBashShell));
            Checks::exit_success(VCPKG_LINE_INFO);
        }
        msg::println(msgAddingCompletionEntry, msg::path = bashrc_path);
        bashrc_content.append("\nsource ");
        bashrc_content.append(completion_script_path.native());
        bashrc_content.push_back('\n');
        fs.write_contents(bashrc_path, bashrc_content, VCPKG_LINE_INFO);
        Checks::exit_success(VCPKG_LINE_INFO);
#endif // ^^^ !_WIN32
    }

    static void integrate_zsh(const VcpkgPaths& paths)
    {
#if defined(_WIN32)
        (void)paths;
        Checks::msg_exit_with_error(
            VCPKG_LINE_INFO, msgIntegrateNonWindowsOnly, msg::command_line = "vcpkg integrate zsh");
#else  // ^^^ _WIN32 // !_WIN32 vvv
        const auto home_path = get_environment_variable("HOME").value_or_exit(VCPKG_LINE_INFO);
        const auto zshrc_path = Path{home_path} / ".zshrc";

        auto& fs = paths.get_filesystem();
        const auto completion_script_path = paths.scripts / "vcpkg_completion.zsh";

        auto zshrc_content = fs.read_contents(zshrc_path, VCPKG_LINE_INFO);

        // How to use bash completions in zsh: https://stackoverflow.com/a/8492043/10162645
        auto data = get_zsh_autocomplete_data(zshrc_content);

        if (!data.source_completion_lines.empty())
        {
            msg::println(msg::format(msgVcpkgCompletion, msg::value = "zsh", msg::path = zshrc_path)
                             .append_raw(Strings::join("\n   ", data.source_completion_lines))
                             .append_raw('\n')
                             .append(msgSuggestStartingBashShell));
            Checks::exit_success(VCPKG_LINE_INFO);
        }
        msg::println(msgAddingCompletionEntry, msg::path = zshrc_path);
        if (!data.has_autoload_bashcompinit)
        {
            zshrc_content.append("\nautoload bashcompinit");
        }
        if (!data.has_bashcompinit)
        {
            zshrc_content.append("\nbashcompinit");
        }
        zshrc_content.append("\nsource ");
        zshrc_content.append(completion_script_path.native());
        zshrc_content.push_back('\n');
        fs.write_contents(zshrc_path, zshrc_content, VCPKG_LINE_INFO);
        Checks::exit_success(VCPKG_LINE_INFO);
#endif // ^^^ !_WIN32
    }

    static void integrate_fish(const VcpkgPaths& paths)
    {
#if defined(_WIN32)
        (void)paths;
        Checks::msg_exit_with_error(
            VCPKG_LINE_INFO, msgIntegrateNonWindowsOnly, msg::command_line = "vcpkg integrate x-fish");
#else  // ^^^ _WIN32 // !_WIN32 vvv
        Path fish_completions_path;
        const auto config_path = get_environment_variable("XDG_CONFIG_HOME");
        if (config_path.has_value())
        {
            fish_completions_path = config_path.value_or_exit(VCPKG_LINE_INFO);
        }
        else
        {
            Path home_path = get_environment_variable("HOME").value_or_exit(VCPKG_LINE_INFO);
            fish_completions_path = std::move(home_path) / ".config";
        }

        fish_completions_path /= "fish/completions";

        auto& fs = paths.get_filesystem();

        std::error_code ec;
        fs.create_directories(fish_completions_path, ec);

        fish_completions_path /= "vcpkg.fish";

        if (fs.exists(fish_completions_path, IgnoreErrors{}))
        {
            msg::println(msgFishCompletion, msg::path = fish_completions_path);
            Checks::exit_success(VCPKG_LINE_INFO);
        }

        const auto completion_script_path = paths.scripts / "vcpkg_completion.fish";
        msg::println(msgAddingCompletionEntry, msg::path = fish_completions_path);
        fs.create_symlink(completion_script_path, fish_completions_path, VCPKG_LINE_INFO);
        Checks::exit_success(VCPKG_LINE_INFO);
#endif // ^^^ !_WIN32
    }

    static constexpr StringLiteral INSTALL = "install";
    static constexpr StringLiteral REMOVE = "remove";
    static constexpr StringLiteral PROJECT = "project";
    static constexpr StringLiteral POWERSHELL = "powershell";
    static constexpr StringLiteral BASH = "bash";
    static constexpr StringLiteral ZSH = "zsh";
    static constexpr StringLiteral FISH = "x-fish";

    static std::vector<std::string> valid_arguments(const VcpkgPaths&)
    {
        // Note that help lists all supported args, but we only want to autocomplete the ones valid on this platform
        return {
            INSTALL.to_string(),
            REMOVE.to_string(),
#if defined(_WIN32)
            PROJECT.to_string(),
            POWERSHELL.to_string(),
#else
            BASH.to_string(),
            FISH.to_string(),
            ZSH.to_string()
#endif
        };
    }

    constexpr CommandMetadata CommandIntegrateMetadata{
        "integrate",
        msgCmdIntegrateSynopsis,
        {[] {
            HelpTableFormatter table;
#if defined(_WIN32)
            table.format("vcpkg integrate install", msg::format(msgIntegrateInstallHelpWindows));
#else  // ^^^ defined(_WIN32) // !defined(_WIN32) vvv
            table.format("vcpkg integrate install", msg::format(msgIntegrateInstallHelpLinux));
#endif // ^^^ !defined(_WIN32)
            table.format("vcpkg integrate remove", msg::format(msgIntegrateRemoveHelp));
            table.blank();
            table.format("vcpkg integrate project", msg::format(msgIntegrateProjectHelp));
            table.blank();
            table.format("vcpkg integrate bash", msg::format(msgIntegrateBashHelp));
            table.format("vcpkg integrate x-fish", msg::format(msgIntegrateFishHelp));
            table.format("vcpkg integrate powershell", msg::format(msgIntegratePowerShellHelp));
            table.format("vcpkg integrate zsh", msg::format(msgIntegrateZshHelp));
            return LocalizedString::from_raw("\n").append_raw(std::move(table.m_str));
        }},
        "https://learn.microsoft.com/vcpkg/commands/integrate",
        AutocompletePriority::Public,
        1,
        1,
        {},
        &valid_arguments,
    };

    void command_integrate_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        const auto parsed = args.parse_arguments(CommandIntegrateMetadata);

        if (Strings::case_insensitive_ascii_equals(parsed.command_arguments[0], INSTALL))
        {
            return integrate_install(paths);
        }

        if (Strings ::case_insensitive_ascii_equals(parsed.command_arguments[0], REMOVE))
        {
            return integrate_remove(paths.get_filesystem());
        }

        if (Strings ::case_insensitive_ascii_equals(parsed.command_arguments[0], PROJECT))
        {
            return integrate_project(paths);
        }

        if (Strings ::case_insensitive_ascii_equals(parsed.command_arguments[0], POWERSHELL))
        {
            return integrate_powershell(paths);
        }

        if (Strings ::case_insensitive_ascii_equals(parsed.command_arguments[0], BASH))
        {
            return integrate_bash(paths);
        }

        if (Strings ::case_insensitive_ascii_equals(parsed.command_arguments[0], ZSH))
        {
            return integrate_zsh(paths);
        }

        if (Strings ::case_insensitive_ascii_equals(parsed.command_arguments[0], FISH))
        {
            return integrate_fish(paths);
        }

        Checks::msg_exit_maybe_upgrade(
            VCPKG_LINE_INFO, msgUnknownParameterForIntegrate, msg::value = parsed.command_arguments[0]);
    }
}
