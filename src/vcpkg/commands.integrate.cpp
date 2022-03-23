#include <vcpkg/base/checks.h>
#include <vcpkg/base/expected.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/system.print.h>
#include <vcpkg/base/system.process.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.integrate.h>
#include <vcpkg/metrics.h>
#include <vcpkg/tools.h>
#include <vcpkg/userconfig.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

namespace vcpkg::Commands::Integrate
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
            if (Strings::starts_with(line, "source") && Strings::ends_with(line, "scripts/vcpkg_completion.bash"))
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

            if (Strings::starts_with(line, "source") && Strings::ends_with(line, "scripts/vcpkg_completion.zsh"))
            {
                res.source_completion_lines.emplace_back(line.data(), line.size());
            }
            else if (bashcompinit != line.end())
            {
                if (Strings::starts_with(line, "autoload"))
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
                    if (!Strings::contains(line_before_bashcompinit, '#') &&
                        (line_before_bashcompinit.empty() || Strings::ends_with(line_before_bashcompinit, "&&")))
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
        return Strings::format(R"###(
<Project ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <Import Condition="Exists('%s') and '$(VCPkgLocalAppDataDisabled)' == ''" Project="%s" />
</Project>
)###",
                               target_path,
                               target_path);
    }
#endif

#if defined(_WIN32)
    static std::string create_system_targets_shortcut() noexcept
    {
        return R"###(
<Project ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <!-- version 1 -->
  <PropertyGroup>
    <VCLibPackagePath Condition="'$(VCLibPackagePath)' == ''">$(LOCALAPPDATA)\vcpkg\vcpkg.user</VCLibPackagePath>
  </PropertyGroup>
  <Import Condition="'$(VCLibPackagePath)' != '' and Exists('$(VCLibPackagePath).props')" Project="$(VCLibPackagePath).props" />
  <Import Condition="'$(VCLibPackagePath)' != '' and Exists('$(VCLibPackagePath).targets')" Project="$(VCLibPackagePath).targets" />
</Project>
)###";
    }
#endif

#if defined(_WIN32)
    static std::string create_nuget_targets_file_contents(const Path& msbuild_vcpkg_targets_file) noexcept
    {
        return Strings::format(R"###(
<Project ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <Import Project="%s" Condition="Exists('%s')" />
  <Target Name="CheckValidPlatform" BeforeTargets="Build">
    <Error Text="Unsupported architecture combination. Remove the 'vcpkg' nuget package." Condition="'$(VCPkgEnabled)' != 'true' and '$(VCPkgDisableError)' == ''"/>
  </Target>
</Project>
)###",
                               msbuild_vcpkg_targets_file,
                               msbuild_vcpkg_targets_file);
    }
#endif

#if defined(_WIN32)
    static std::string create_nuget_props_file_contents() noexcept
    {
        return R"###(
<Project ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <PropertyGroup>
    <VCPkgLocalAppDataDisabled>true</VCPkgLocalAppDataDisabled>
  </PropertyGroup>
</Project>
)###";
    }
#endif

#if defined(_WIN32)
    static std::string get_nuget_id(const Path& vcpkg_root_dir)
    {
        std::string dir_id = vcpkg_root_dir.generic_u8string();
        std::replace(dir_id.begin(), dir_id.end(), '/', '.');
        dir_id.erase(1, 1); // Erasing the ":"

        // NuGet id cannot have invalid characters. We will only use alphanumeric and dot.
        Util::erase_remove_if(dir_id, [](char c) { return !isalnum(static_cast<unsigned char>(c)) && (c != '.'); });

        const std::string nuget_id = "vcpkg." + dir_id;
        return nuget_id;
    }
#endif

#if defined(_WIN32)
    static std::string create_nuspec_file_contents(const Path& vcpkg_root_dir,
                                                   const std::string& nuget_id,
                                                   const std::string& nupkg_version)
    {
        static constexpr auto CONTENT_TEMPLATE = R"(
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
#endif

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
#endif

#if defined(_WIN32)
    static Path get_appdata_targets_path()
    {
        return get_appdata_local().value_or_exit(VCPKG_LINE_INFO) / "vcpkg\\vcpkg.user.targets";
    }
#endif
#if defined(_WIN32)
    static Path get_appdata_props_path()
    {
        return get_appdata_local().value_or_exit(VCPKG_LINE_INFO) / "vcpkg\\vcpkg.user.props";
    }
#endif

    static Path get_path_txt_path() { return get_user_dir() / "vcpkg.path.txt"; }

#if defined(_WIN32)
    static void integrate_install_msbuild14(Filesystem& fs, const Path& tmp_dir)
    {
        static const std::array<Path, 2> OLD_SYSTEM_TARGET_FILES = {
            get_program_files_32_bit().value_or_exit(VCPKG_LINE_INFO) /
                "MSBuild/14.0/Microsoft.Common.Targets/ImportBefore/vcpkg.nuget.targets",
            get_program_files_32_bit().value_or_exit(VCPKG_LINE_INFO) /
                "MSBuild/14.0/Microsoft.Common.Targets/ImportBefore/vcpkg.system.targets"};
        static const Path SYSTEM_WIDE_TARGETS_FILE =
            get_program_files_32_bit().value_or_exit(VCPKG_LINE_INFO) /
            "MSBuild/Microsoft.Cpp/v4.0/V140/ImportBefore/Default/vcpkg.system.props";

        // TODO: This block of code should eventually be removed
        for (auto&& old_system_wide_targets_file : OLD_SYSTEM_TARGET_FILES)
        {
            if (fs.exists(old_system_wide_targets_file, IgnoreErrors{}))
            {
                const std::string param = Strings::format(R"(/c "DEL "%s" /Q > nul")", old_system_wide_targets_file);
                const ElevationPromptChoice user_choice = elevated_cmd_execute(param);
                switch (user_choice)
                {
                    case ElevationPromptChoice::YES: break;
                    case ElevationPromptChoice::NO:
                        print2(Color::warning, "Warning: Previous integration file was not removed\n");
                        Checks::exit_fail(VCPKG_LINE_INFO);
                    default: Checks::unreachable(VCPKG_LINE_INFO);
                }
            }
        }
        bool should_install_system = true;
        std::error_code ec;
        std::string system_wide_file_contents = fs.read_contents(SYSTEM_WIDE_TARGETS_FILE, ec);
        if (!ec)
        {
            auto opt = find_targets_file_version(system_wide_file_contents);
            if (opt.value_or(0) >= 1)
            {
                should_install_system = false;
            }
        }

        if (should_install_system)
        {
            const auto sys_src_path = tmp_dir / "vcpkg.system.targets";
            fs.write_contents(sys_src_path, create_system_targets_shortcut(), VCPKG_LINE_INFO);

            const std::string param = Strings::format(R"(/c "mkdir "%s" & copy "%s" "%s" /Y > nul")",
                                                      SYSTEM_WIDE_TARGETS_FILE.parent_path(),
                                                      sys_src_path,
                                                      SYSTEM_WIDE_TARGETS_FILE);
            const ElevationPromptChoice user_choice = elevated_cmd_execute(param);
            switch (user_choice)
            {
                case ElevationPromptChoice::YES: break;
                case ElevationPromptChoice::NO:
                    print2(Color::warning, "Warning: integration was not applied\n");
                    Checks::exit_fail(VCPKG_LINE_INFO);
                default: Checks::unreachable(VCPKG_LINE_INFO);
            }

            Checks::check_exit(VCPKG_LINE_INFO,
                               fs.exists(SYSTEM_WIDE_TARGETS_FILE, IgnoreErrors{}),
                               "Error: failed to copy targets file to %s",
                               SYSTEM_WIDE_TARGETS_FILE);
        }
    }
#endif

    static void integrate_install(const VcpkgPaths& paths)
    {
        auto& fs = paths.get_filesystem();

#if defined(_WIN32)
        {
            std::error_code ec;
            const auto tmp_dir = paths.buildsystems / "tmp";
            fs.create_directory(paths.buildsystems, VCPKG_LINE_INFO);
            fs.create_directory(tmp_dir, VCPKG_LINE_INFO);

            integrate_install_msbuild14(fs, tmp_dir);

            const auto appdata_src_path = tmp_dir / "vcpkg.user.targets";
            fs.write_contents(
                appdata_src_path, create_appdata_shortcut(paths.buildsystems_msbuild_targets), VCPKG_LINE_INFO);
            auto appdata_dst_path = get_appdata_targets_path();

            const auto vcpkg_appdata_local = get_appdata_local().value_or_exit(VCPKG_LINE_INFO) / "vcpkg";
            fs.create_directory(vcpkg_appdata_local, VCPKG_LINE_INFO);

            fs.copy_file(appdata_src_path, appdata_dst_path, CopyOptions::overwrite_existing, ec);
            if (ec)
            {
                print2(Color::error, "Error: Failed to copy file: ", appdata_src_path, " -> ", appdata_dst_path, "\n");
                Checks::exit_fail(VCPKG_LINE_INFO);
            }

            const Path appdata_src_path2 = tmp_dir / "vcpkg.user.props";
            fs.write_contents(
                appdata_src_path2, create_appdata_shortcut(paths.buildsystems_msbuild_props), VCPKG_LINE_INFO);
            auto appdata_dst_path2 = get_appdata_props_path();

            fs.copy_file(appdata_src_path2, appdata_dst_path2, CopyOptions::overwrite_existing, ec);
            if (ec)
            {
                print2(
                    Color::error, "Error: Failed to copy file: ", appdata_src_path2, " -> ", appdata_dst_path2, "\n");
                Checks::exit_fail(VCPKG_LINE_INFO);
            }
        }
#endif

        const auto pathtxt = get_path_txt_path();
        std::error_code ec;
        fs.write_contents(pathtxt, paths.root.generic_u8string(), VCPKG_LINE_INFO);

        print2(Color::success, "Applied user-wide integration for this vcpkg root.\n");
        const auto cmake_toolchain = paths.buildsystems / "vcpkg.cmake";
#if defined(_WIN32)
        vcpkg::printf(
            R"(
All MSBuild C++ projects can now #include any installed libraries.
Linking will be handled automatically.
Installing new libraries will make them instantly available.

CMake projects should use: "-DCMAKE_TOOLCHAIN_FILE=%s"
)",
            cmake_toolchain.generic_u8string());
#else
        vcpkg::printf(
            R"(
CMake projects should use: "-DCMAKE_TOOLCHAIN_FILE=%s"
)",
            cmake_toolchain.generic_u8string());
#endif

        Checks::exit_success(VCPKG_LINE_INFO);
    }

    static void integrate_remove(Filesystem& fs)
    {
        std::error_code ec;
        bool was_deleted = false;

#if defined(_WIN32)
        was_deleted |= fs.remove(get_appdata_targets_path(), ec);
        Checks::check_exit(VCPKG_LINE_INFO, !ec, "Error: Unable to remove user-wide integration: %s", ec.message());

        was_deleted |= fs.remove(get_appdata_props_path(), ec);
        Checks::check_exit(VCPKG_LINE_INFO, !ec, "Error: Unable to remove user-wide integration: %s", ec.message());
#endif

        was_deleted |= fs.remove(get_path_txt_path(), ec);
        Checks::check_exit(VCPKG_LINE_INFO, !ec, "Error: Unable to remove user-wide integration: %s", ec.message());

        if (was_deleted)
        {
            print2(Color::success, "User-wide integration was removed\n");
        }
        else
        {
            print2(Color::success, "User-wide integration is not installed\n");
        }

        Checks::exit_success(VCPKG_LINE_INFO);
    }

#if defined(WIN32)
    static void integrate_project(const VcpkgPaths& paths)
    {
        auto& fs = paths.get_filesystem();

        const Path& nuget_exe = paths.get_tool_exe(Tools::NUGET);

        const Path& buildsystems_dir = paths.buildsystems;
        const auto tmp_dir = buildsystems_dir / "tmp";
        std::error_code ec;
        fs.create_directory(buildsystems_dir, ec);
        fs.create_directory(tmp_dir, ec);

        const auto targets_file_path = tmp_dir / "vcpkg.nuget.targets";
        const auto props_file_path = tmp_dir / "vcpkg.nuget.props";
        const auto nuspec_file_path = tmp_dir / "vcpkg.nuget.nuspec";
        const std::string nuget_id = get_nuget_id(paths.root);
        const std::string nupkg_version = "1.0.0";

        fs.write_contents(
            targets_file_path, create_nuget_targets_file_contents(paths.buildsystems_msbuild_targets), VCPKG_LINE_INFO);
        fs.write_contents(props_file_path, create_nuget_props_file_contents(), VCPKG_LINE_INFO);
        fs.write_contents(
            nuspec_file_path, create_nuspec_file_contents(paths.root, nuget_id, nupkg_version), VCPKG_LINE_INFO);

        // Using all forward slashes for the command line
        auto cmd_line = Command(nuget_exe)
                            .string_arg("pack")
                            .string_arg("-OutputDirectory")
                            .string_arg(buildsystems_dir)
                            .string_arg(nuspec_file_path);

        const int exit_code =
            cmd_execute_and_capture_output(cmd_line, default_working_directory, get_clean_environment()).exit_code;

        const auto nuget_package = buildsystems_dir / Strings::format("%s.%s.nupkg", nuget_id, nupkg_version);
        Checks::check_exit(
            VCPKG_LINE_INFO, exit_code == 0, "Error: NuGet package creation failed with exit code: %d", exit_code);
        Checks::check_exit(VCPKG_LINE_INFO,
                           fs.exists(nuget_package, IgnoreErrors{}),
                           "Error: NuGet package creation \"succeeded\", but no .nupkg was produced. Expected %s",
                           nuget_package);
        print2(Color::success, "Created nupkg: ", nuget_package, '\n');

        auto source_path = Strings::replace_all(buildsystems_dir, "`", "``");

        vcpkg::printf(R"(
With a project open, go to Tools->NuGet Package Manager->Package Manager Console and paste:
    Install-Package %s -Source "%s"

)",
                      nuget_id,
                      source_path);

        Checks::exit_success(VCPKG_LINE_INFO);
    }
#endif

#if defined(_WIN32)
    static void integrate_powershell(const VcpkgPaths& paths)
    {
        static constexpr StringLiteral TITLE = "PowerShell Tab-Completion";
        const auto script_path = paths.scripts / "addPoshVcpkgToPowershellProfile.ps1";

        const auto& ps = paths.get_tool_exe("powershell-core");
        auto cmd = Command(ps)
                       .string_arg("-NoProfile")
                       .string_arg("-ExecutionPolicy")
                       .string_arg("Bypass")
                       .string_arg("-Command")
                       .string_arg(Strings::format("& {& '%s' }", script_path));
        const int rc = cmd_execute(cmd);
        if (rc)
        {
            vcpkg::printf(Color::error,
                          "%s\n"
                          "Could not run:\n"
                          "    '%s'\n",
                          TITLE,
                          script_path.generic_u8string());

            {
                auto locked_metrics = LockGuardPtr<Metrics>(g_metrics);
                locked_metrics->track_property("error", "powershell script failed");
                locked_metrics->track_property("title", TITLE);
            }
        }

        Checks::exit_with_code(VCPKG_LINE_INFO, rc);
    }
#else
    static void integrate_bash(const VcpkgPaths& paths)
    {
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
            vcpkg::printf("vcpkg bash completion is already imported to your %s file.\n"
                          "The following entries were found:\n"
                          "    %s\n"
                          "Please make sure you have started a new bash shell for the changes to take effect.\n",
                          bashrc_path,
                          Strings::join("\n    ", matches));
            Checks::exit_success(VCPKG_LINE_INFO);
        }

        vcpkg::printf("Adding vcpkg completion entry to %s\n", bashrc_path);
        bashrc_content.append("\nsource ");
        bashrc_content.append(completion_script_path.native());
        bashrc_content.push_back('\n');
        fs.write_contents(bashrc_path, bashrc_content, VCPKG_LINE_INFO);
        Checks::exit_success(VCPKG_LINE_INFO);
    }

    static void integrate_zsh(const VcpkgPaths& paths)
    {
        const auto home_path = get_environment_variable("HOME").value_or_exit(VCPKG_LINE_INFO);
        const auto zshrc_path = Path{home_path} / ".zshrc";

        auto& fs = paths.get_filesystem();
        const auto completion_script_path = paths.scripts / "vcpkg_completion.zsh";

        auto zshrc_content = fs.read_contents(zshrc_path, VCPKG_LINE_INFO);

        // How to use bash completions in zsh: https://stackoverflow.com/a/8492043/10162645
        auto data = get_zsh_autocomplete_data(zshrc_content);

        if (!data.source_completion_lines.empty())
        {
            printf("vcpkg zsh completion is already imported to your %s file.\n"
                   "The following entries were found:\n"
                   "    %s\n"
                   "Please make sure you have started a new zsh shell for the changes to take effect.\n",
                   zshrc_path,
                   Strings::join("\n    ", data.source_completion_lines));
            Checks::exit_success(VCPKG_LINE_INFO);
        }

        printf("Adding vcpkg completion entry to %s\n", zshrc_path);
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
    }

    static void integrate_fish(const VcpkgPaths& paths)
    {
        Path fish_completions_path;
        const auto config_path = get_environment_variable("XDG_CONFIG_HOME");
        if (config_path.has_value())
        {
            fish_completions_path = config_path.value_or_exit(VCPKG_LINE_INFO);
        }
        else
        {
            const Path home_path = get_environment_variable("HOME").value_or_exit(VCPKG_LINE_INFO);
            fish_completions_path = home_path / ".config";
        }

        fish_completions_path = fish_completions_path / "fish/completions";

        auto& fs = paths.get_filesystem();

        std::error_code ec;
        fs.create_directories(fish_completions_path, ec);

        if (ec)
        {
            print2(Color::error,
                   "Error: Failed to create fish completions directory: ",
                   fish_completions_path,
                   ": ",
                   ec.message(),
                   "\n");
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        fish_completions_path = fish_completions_path / "vcpkg.fish";

        if (fs.exists(fish_completions_path, IgnoreErrors{}))
        {
            vcpkg::printf("vcpkg fish completion is already added at %s.\n", fish_completions_path);
            Checks::exit_success(VCPKG_LINE_INFO);
        }

        const auto completion_script_path = paths.scripts / "vcpkg_completion.fish";
        vcpkg::printf("Adding vcpkg completion entry at %s.\n", fish_completions_path);
        fs.create_symlink(completion_script_path, fish_completions_path, VCPKG_LINE_INFO);
        Checks::exit_success(VCPKG_LINE_INFO);
    }
#endif

    void append_helpstring(HelpTableFormatter& table)
    {
#if defined(_WIN32)
        table.format("vcpkg integrate install",
                     "Make installed packages available user-wide. Requires admin privileges on first use");
        table.format("vcpkg integrate remove", "Remove user-wide integration");
        table.format("vcpkg integrate project", "Generate a referencing nuget package for individual VS project use");
        table.format("vcpkg integrate powershell", "Enable PowerShell tab-completion");
#else  // ^^^ defined(_WIN32) // !defined(_WIN32) vvv
        table.format("vcpkg integrate install", "Make installed packages available user-wide");
        table.format("vcpkg integrate remove", "Remove user-wide integration");
        table.format("vcpkg integrate bash", "Enable bash tab-completion");
        table.format("vcpkg integrate zsh", "Enable zsh tab-completion");
        table.format("vcpkg integrate x-fish", "Enable fish tab-completion");
#endif // ^^^ !defined(_WIN32)
    }

    std::string get_helpstring()
    {
        HelpTableFormatter table;
        append_helpstring(table);
        return std::move(table.m_str);
    }

    namespace Subcommand
    {
        static const std::string INSTALL = "install";
        static const std::string REMOVE = "remove";
        static const std::string PROJECT = "project";
        static const std::string POWERSHELL = "powershell";
        static const std::string BASH = "bash";
        static const std::string ZSH = "zsh";
        static const std::string FISH = "x-fish";
    }

    static std::vector<std::string> valid_arguments(const VcpkgPaths&)
    {
        return
        {
            Subcommand::INSTALL, Subcommand::REMOVE,
#if defined(_WIN32)
                Subcommand::PROJECT, Subcommand::POWERSHELL,
#else
                Subcommand::BASH, Subcommand::FISH,
#endif
        };
    }

    const CommandStructure COMMAND_STRUCTURE = {
        "Commands:\n" + get_helpstring(),
        1,
        1,
        {},
        &valid_arguments,
    };

    void perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        (void)args.parse_arguments(COMMAND_STRUCTURE);

        if (args.command_arguments[0] == Subcommand::INSTALL)
        {
            return integrate_install(paths);
        }
        if (args.command_arguments[0] == Subcommand::REMOVE)
        {
            return integrate_remove(paths.get_filesystem());
        }
#if defined(_WIN32)
        if (args.command_arguments[0] == Subcommand::PROJECT)
        {
            return integrate_project(paths);
        }
        if (args.command_arguments[0] == Subcommand::POWERSHELL)
        {
            return integrate_powershell(paths);
        }
#else
        if (args.command_arguments[0] == Subcommand::BASH)
        {
            return integrate_bash(paths);
        }
        if (args.command_arguments[0] == Subcommand::ZSH)
        {
            return integrate_zsh(paths);
        }
        if (args.command_arguments[0] == Subcommand::FISH)
        {
            return integrate_fish(paths);
        }
#endif

        Checks::exit_maybe_upgrade(VCPKG_LINE_INFO, "Unknown parameter %s for integrate", args.command_arguments[0]);
    }

    void IntegrateCommand::perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths) const
    {
        Integrate::perform_and_exit(args, paths);
    }
}
