#include <vcpkg/base/strings.h>
#include <vcpkg/base/system.print.h>
#include <vcpkg/base/system.process.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.edit.h>
#include <vcpkg/help.h>
#include <vcpkg/paragraphs.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

#include <limits.h>

#if defined(_WIN32)
namespace
{
    std::vector<vcpkg::path> find_from_registry()
    {
        std::vector<vcpkg::path> output;

        struct RegKey
        {
            HKEY root;
            vcpkg::StringLiteral subkey;
        } REGKEYS[] = {
            {HKEY_LOCAL_MACHINE,
             R"(SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\{C26E74D1-022E-4238-8B9D-1E7564A36CC9}_is1)"},
            {HKEY_LOCAL_MACHINE,
             R"(SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{1287CAD5-7C8D-410D-88B9-0D1EE4A83FF2}_is1)"},
            {HKEY_LOCAL_MACHINE,
             R"(SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\{F8A2A208-72B3-4D61-95FC-8A65D340689B}_is1)"},
            {HKEY_CURRENT_USER,
             R"(Software\Microsoft\Windows\CurrentVersion\Uninstall\{771FD6B0-FA20-440A-A002-3B3BAC16DC50}_is1)"},
            {HKEY_LOCAL_MACHINE,
             R"(SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{EA457B21-F73E-494C-ACAB-524FDE069978}_is1)"},
        };

        for (auto&& keypath : REGKEYS)
        {
            const vcpkg::Optional<std::string> code_installpath =
                vcpkg::get_registry_string(keypath.root, keypath.subkey, "InstallLocation");
            if (const auto c = code_installpath.get())
            {
                const auto install_path = vcpkg::u8path(*c);
                output.push_back(install_path / vcpkg::u8path("Code - Insiders.exe"));
                output.push_back(install_path / vcpkg::u8path("Code.exe"));
            }
        }
        return output;
    }

    std::string expand_environment_strings(const std::string& input)
    {
        const auto widened = vcpkg::Strings::to_utf16(input);
        std::wstring result;
        result.resize(result.capacity());
        bool done;
        do
        {
            if (result.size() == ULONG_MAX)
            {
                vcpkg::Checks::exit_fail(VCPKG_LINE_INFO); // integer overflow
            }

            const auto required_size =
                ExpandEnvironmentStringsW(widened.c_str(), &result[0], static_cast<unsigned long>(result.size() + 1));
            if (required_size == 0)
            {
                vcpkg::print2(vcpkg::Color::error, "Error: could not expand the environment string:\n");
                vcpkg::print2(vcpkg::Color::error, input);
                vcpkg::Checks::exit_fail(VCPKG_LINE_INFO);
            }

            done = required_size <= result.size() + 1;
            result.resize(required_size - 1);
        } while (!done);
        return vcpkg::Strings::to_utf8(result);
    }
}
#endif

namespace vcpkg::Commands::Edit
{
    static constexpr StringLiteral OPTION_BUILDTREES = "buildtrees";

    static constexpr StringLiteral OPTION_ALL = "all";

    static std::vector<std::string> valid_arguments(const VcpkgPaths& paths)
    {
        auto sources_and_errors = Paragraphs::try_load_all_registry_ports(paths);

        return Util::fmap(sources_and_errors.paragraphs, Paragraphs::get_name_of_control_file);
    }

    static constexpr std::array<CommandSwitch, 2> EDIT_SWITCHES = {
        {{OPTION_BUILDTREES, "Open editor into the port-specific buildtree subfolder"},
         {OPTION_ALL, "Open editor into the port as well as the port-specific buildtree subfolder"}}};

    const CommandStructure COMMAND_STRUCTURE = {
        create_example_string("edit zlib"),
        1,
        10,
        {EDIT_SWITCHES, {}},
        &valid_arguments,
    };

    static std::vector<std::string> create_editor_arguments(const VcpkgPaths& paths,
                                                            const ParsedArguments& options,
                                                            const std::vector<std::string>& ports)
    {
        if (Util::Sets::contains(options.switches, OPTION_ALL))
        {
            const auto& fs = paths.get_filesystem();
            auto packages = fs.get_files_non_recursive(paths.packages);

            // TODO: Support edit for --overlay-ports
            return Util::fmap(ports, [&](const std::string& port_name) -> std::string {
                const auto portpath = paths.builtin_ports_directory() / port_name;
                const auto portfile = portpath / "portfile.cmake";
                const auto buildtrees_current_dir = paths.build_dir(port_name);
                const auto pattern = port_name + "_";

                std::string package_paths;
                for (auto&& package : packages)
                {
                    if (Strings::case_insensitive_ascii_starts_with(vcpkg::u8string(package.filename()), pattern))
                    {
                        package_paths.append(Strings::format(" \"%s\"", vcpkg::u8string(package)));
                    }
                }

                return Strings::format(R"###("%s" "%s" "%s"%s)###",
                                       vcpkg::u8string(portpath),
                                       vcpkg::u8string(portfile),
                                       vcpkg::u8string(buildtrees_current_dir),
                                       package_paths);
            });
        }

        if (Util::Sets::contains(options.switches, OPTION_BUILDTREES))
        {
            return Util::fmap(ports, [&](const std::string& port_name) -> std::string {
                return Strings::format(R"###("%s")###", vcpkg::u8string(paths.build_dir(port_name)));
            });
        }

        return Util::fmap(ports, [&](const std::string& port_name) -> std::string {
            const auto portpath = paths.builtin_ports_directory() / port_name;
            const auto portfile = portpath / "portfile.cmake";
            return Strings::format(R"###("%s" "%s")###", vcpkg::u8string(portpath), vcpkg::u8string(portfile));
        });
    }

    void perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        auto& fs = paths.get_filesystem();

        const ParsedArguments options = args.parse_arguments(COMMAND_STRUCTURE);

        const std::vector<std::string>& ports = args.command_arguments;
        for (auto&& port_name : ports)
        {
            const path portpath = paths.builtin_ports_directory() / port_name;
            Checks::check_maybe_upgrade(
                VCPKG_LINE_INFO, fs.is_directory(portpath), R"(Could not find port named "%s")", port_name);
        }

        std::vector<path> candidate_paths;
        auto maybe_editor_path = get_environment_variable("EDITOR");
        if (const std::string* editor_path = maybe_editor_path.get())
        {
            candidate_paths.emplace_back(*editor_path);
        }

#ifdef _WIN32
        static const path VS_CODE_INSIDERS = path{"Microsoft VS Code Insiders"} / "Code - Insiders.exe";
        static const path VS_CODE = path{"Microsoft VS Code"} / "Code.exe";

        const auto& program_files = get_program_files_platform_bitness();
        if (const path* pf = program_files.get())
        {
            candidate_paths.push_back(*pf / VS_CODE_INSIDERS);
            candidate_paths.push_back(*pf / VS_CODE);
        }

        const auto& program_files_32_bit = get_program_files_32_bit();
        if (const path* pf = program_files_32_bit.get())
        {
            candidate_paths.push_back(*pf / VS_CODE_INSIDERS);
            candidate_paths.push_back(*pf / VS_CODE);
        }

        const auto& app_data = get_environment_variable("APPDATA");
        if (const auto* ad = app_data.get())
        {
            const path default_base = path{*ad}.parent_path() / "Local" / "Programs";
            candidate_paths.push_back(default_base / VS_CODE_INSIDERS);
            candidate_paths.push_back(default_base / VS_CODE);
        }

        const std::vector<path> from_registry = find_from_registry();
        candidate_paths.insert(candidate_paths.end(), from_registry.cbegin(), from_registry.cend());

        const auto txt_default = get_registry_string(HKEY_CLASSES_ROOT, R"(.txt\ShellNew)", "ItemName");
        if (const auto entry = txt_default.get())
        {
            auto full_path = expand_environment_strings(*entry);
            auto first = full_path.begin();
            const auto last = full_path.end();
            first = std::find_if_not(first, last, [](const char c) { return c == '@'; });
            const auto comma = std::find(first, last, ',');
            candidate_paths.push_back(vcpkg::u8path(first, comma));
        }
#elif defined(__APPLE__)
        candidate_paths.push_back(
            path{"/Applications/Visual Studio Code - Insiders.app/Contents/Resources/app/bin/code"});
        candidate_paths.push_back(path{"/Applications/Visual Studio Code.app/Contents/Resources/app/bin/code"});
#elif defined(__linux__)
        candidate_paths.push_back(path{"/usr/share/code/bin/code"});
        candidate_paths.push_back(path{"/usr/bin/code"});

        if (cmd_execute(Command("command").string_arg("-v").string_arg("xdg-mime")) == 0)
        {
            auto mime_qry = Command("xdg-mime").string_arg("query").string_arg("default").string_arg("text/plain");
            auto execute_result = cmd_execute_and_capture_output(mime_qry);
            if (execute_result.exit_code == 0 && !execute_result.output.empty())
            {
                mime_qry = Command("command").string_arg("-v").string_arg(
                    execute_result.output.substr(0, execute_result.output.find('.')));
                execute_result = cmd_execute_and_capture_output(mime_qry);
                if (execute_result.exit_code == 0 && !execute_result.output.empty())
                {
                    execute_result.output.erase(
                        std::remove(std::begin(execute_result.output), std::end(execute_result.output), '\n'),
                        std::end(execute_result.output));
                    candidate_paths.push_back(path{execute_result.output});
                }
            }
        }
#endif

        const auto it = Util::find_if(candidate_paths, [&](const path& p) { return fs.exists(p); });
        if (it == candidate_paths.cend())
        {
            print2(
                Color::error,
                "Error: Visual Studio Code was not found and the environment variable EDITOR is not set or invalid.\n");
            print2("The following paths were examined:\n");
            print_paths(candidate_paths);
            print2("You can also set the environmental variable EDITOR to your editor of choice.\n");
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        const path env_editor = *it;
        const std::vector<std::string> arguments = create_editor_arguments(paths, options, ports);
        const auto args_as_string = Strings::join(" ", arguments);
        auto cmd_line = Command(env_editor).raw_arg(args_as_string).string_arg("-n");

        auto editor_exe = vcpkg::u8string(env_editor.filename());

#ifdef _WIN32
        if (editor_exe == "Code.exe" || editor_exe == "Code - Insiders.exe")
        {
            // note that we are invoking cmd silently but Code.exe is relaunched from there
            cmd_execute_background(
                Command("cmd").string_arg("/c").raw_arg(Strings::concat('"', cmd_line.command_line(), R"( <NUL")")));
            Checks::exit_success(VCPKG_LINE_INFO);
        }
#endif
        Checks::exit_with_code(VCPKG_LINE_INFO, cmd_execute(cmd_line));
    }

    void EditCommand::perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths) const
    {
        Edit::perform_and_exit(args, paths);
    }
}
