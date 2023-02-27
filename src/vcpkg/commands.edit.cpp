#include <vcpkg/base/strings.h>
#include <vcpkg/base/system.process.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.edit.h>
#include <vcpkg/help.h>
#include <vcpkg/paragraphs.h>
#include <vcpkg/registries.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

#include <limits.h>

#if defined(_WIN32)
namespace
{
    using namespace vcpkg;
    std::vector<Path> find_from_registry()
    {
        std::vector<Path> output;

        struct RegKey
        {
            HKEY root;
            StringLiteral subkey;
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
            const Optional<std::string> code_installpath =
                get_registry_string(keypath.root, keypath.subkey, "InstallLocation");
            if (const auto c = code_installpath.get())
            {
                const Path install_path = *c;
                output.push_back(install_path / "Code - Insiders.exe");
                output.push_back(install_path / "Code.exe");
            }
        }
        return output;
    }

    std::string expand_environment_strings(const std::string& input)
    {
        const auto widened = Strings::to_utf16(input);
        std::wstring result;
        result.resize(result.capacity());
        bool done;
        do
        {
            if (result.size() == ULONG_MAX)
            {
                Checks::exit_fail(VCPKG_LINE_INFO); // integer overflow
            }

            const auto required_size = ExpandEnvironmentStringsW(
                widened.c_str(), result.data(), static_cast<unsigned long>(result.size() + 1));
            if (required_size == 0)
            {
                msg::println_error(
                    msg::format(msgEnvStrFailedToExtract).append_raw('\n').append(LocalizedString::from_raw(input)));
                Checks::exit_fail(VCPKG_LINE_INFO);
            }

            done = required_size <= result.size() + 1;
            result.resize(required_size - 1);
        } while (!done);
        return Strings::to_utf8(result);
    }
}
#endif

namespace vcpkg::Commands::Edit
{
    static constexpr StringLiteral OPTION_BUILDTREES = "buildtrees";

    static constexpr StringLiteral OPTION_ALL = "all";

    static std::vector<std::string> valid_arguments(const VcpkgPaths& paths)
    {
        auto registry_set = paths.make_registry_set();
        auto sources_and_errors = Paragraphs::try_load_all_registry_ports(paths.get_filesystem(), *registry_set);

        return Util::fmap(sources_and_errors.paragraphs, Paragraphs::get_name_of_control_file);
    }

    static constexpr std::array<CommandSwitch, 2> EDIT_SWITCHES = {
        {{OPTION_BUILDTREES, []() { return msg::format(msgCmdEditOptBuildTrees); }},
         {OPTION_ALL, []() { return msg::format(msgCmdEditOptAll); }}}};

    const CommandStructure COMMAND_STRUCTURE = {
        [] { return create_example_string("edit zlib"); },
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
            auto packages = fs.get_files_non_recursive(paths.packages(), VCPKG_LINE_INFO);

            // TODO: Support edit for --overlay-ports
            return Util::fmap(ports, [&](const std::string& port_name) -> std::string {
                const auto portpath = paths.builtin_ports_directory() / port_name;
                const auto portfile = portpath / "portfile.cmake";
                const auto buildtrees_current_dir = paths.build_dir(port_name);
                const auto pattern = port_name + "_";

                std::string package_paths;
                for (auto&& package : packages)
                {
                    if (Strings::case_insensitive_ascii_starts_with(package.filename(), pattern))
                    {
                        package_paths.append(Strings::format(" \"%s\"", package));
                    }
                }

                return Strings::format(
                    R"###("%s" "%s" "%s"%s)###", portpath, portfile, buildtrees_current_dir, package_paths);
            });
        }

        if (Util::Sets::contains(options.switches, OPTION_BUILDTREES))
        {
            return Util::fmap(ports, [&](const std::string& port_name) -> std::string {
                return Strings::format(R"###("%s")###", paths.build_dir(port_name));
            });
        }

        return Util::fmap(ports, [&](const std::string& port_name) -> std::string {
            const auto portpath = paths.builtin_ports_directory() / port_name;
            const auto portfile = portpath / "portfile.cmake";
            return Strings::format(R"###("%s" "%s")###", portpath, portfile);
        });
    }

    void perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        auto& fs = paths.get_filesystem();

        const ParsedArguments options = args.parse_arguments(COMMAND_STRUCTURE);

        const std::vector<std::string>& ports = args.command_arguments;
        for (auto&& port_name : ports)
        {
            const auto portpath = paths.builtin_ports_directory() / port_name;
            if (!fs.is_directory(portpath))
            {
                msg::println_error(msgPortDoesNotExist, msg::package_name = port_name);
                Checks::exit_maybe_upgrade(VCPKG_LINE_INFO);
            }
        }

        std::vector<Path> candidate_paths;
        auto maybe_editor_path = get_environment_variable("EDITOR");
        if (const std::string* editor_path = maybe_editor_path.get())
        {
            candidate_paths.emplace_back(*editor_path);
        }

#ifdef _WIN32
        static const auto VS_CODE_INSIDERS = Path{"Microsoft VS Code Insiders"} / "Code - Insiders.exe";
        static const auto VS_CODE = Path{"Microsoft VS Code"} / "Code.exe";

        const auto& program_files = get_program_files_platform_bitness();
        if (const Path* pf = program_files.get())
        {
            candidate_paths.push_back(*pf / VS_CODE_INSIDERS);
            candidate_paths.push_back(*pf / VS_CODE);
        }

        const auto& program_files_32_bit = get_program_files_32_bit();
        if (const Path* pf = program_files_32_bit.get())
        {
            candidate_paths.push_back(*pf / VS_CODE_INSIDERS);
            candidate_paths.push_back(*pf / VS_CODE);
        }

        const auto& app_data = get_environment_variable("APPDATA");
        if (const auto* ad = app_data.get())
        {
            Path default_base = *ad;
            default_base.replace_filename("Local\\Programs");
            candidate_paths.push_back(default_base / VS_CODE_INSIDERS);
            candidate_paths.push_back(default_base / VS_CODE);
        }

        const std::vector<Path> from_registry = find_from_registry();
        candidate_paths.insert(candidate_paths.end(), from_registry.cbegin(), from_registry.cend());

        const auto txt_default = get_registry_string(HKEY_CLASSES_ROOT, R"(.txt\ShellNew)", "ItemName");
        if (const auto entry = txt_default.get())
        {
            auto full_path = expand_environment_strings(*entry);
            auto first = full_path.begin();
            const auto last = full_path.end();
            first = std::find_if_not(first, last, [](const char c) { return c == '@'; });
            const auto comma = std::find(first, last, ',');
            candidate_paths.emplace_back(&*first, comma - first);
        }
#elif defined(__APPLE__)
        candidate_paths.emplace_back("/Applications/Visual Studio Code - Insiders.app/Contents/Resources/app/bin/code");
        candidate_paths.emplace_back("/Applications/Visual Studio Code.app/Contents/Resources/app/bin/code");
#elif defined(__linux__)
        candidate_paths.emplace_back("/usr/share/code/bin/code");
        candidate_paths.emplace_back("/usr/bin/code");

        if (succeeded(cmd_execute(Command("command").string_arg("-v").string_arg("xdg-mime"))))
        {
            auto mime_qry = Command("xdg-mime").string_arg("query").string_arg("default").string_arg("text/plain");
            auto maybe_output = flatten_out(cmd_execute_and_capture_output(mime_qry), "xdg-mime");
            const auto output = maybe_output.get();
            if (output && !output->empty())
            {
                mime_qry = Command("command").string_arg("-v").string_arg(output->substr(0, output->find('.')));
                auto maybe_output2 = flatten_out(cmd_execute_and_capture_output(mime_qry), "xdg-mime");
                const auto output2 = maybe_output2.get();
                if (output2 && !output2->empty())
                {
                    output2->erase(std::remove(output2->begin(), output2->end(), '\n'), output2->end());
                    candidate_paths.emplace_back(std::move(*output2));
                }
            }
        }
#endif

        const auto it = Util::find_if(candidate_paths, [&](const Path& p) { return fs.exists(p, IgnoreErrors{}); });
        if (it == candidate_paths.cend())
        {
            msg::println_error(msg::format(msgErrorVsCodeNotFound, msg::env_var = "EDITOR")
                                   .append_raw('\n')
                                   .append(msgErrorVsCodeNotFoundPathExamined));
            print_paths(candidate_paths);
            msg::println(msgInfoSetEnvVar, msg::env_var = "EDITOR");
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        const Path env_editor = *it;
        const std::vector<std::string> arguments = create_editor_arguments(paths, options, ports);
        const auto args_as_string = Strings::join(" ", arguments);
        auto cmd_line = Command(env_editor).raw_arg(args_as_string).string_arg("-n");
#if defined(_WIN32)
        auto editor_exe = env_editor.filename();
        if (editor_exe == "Code.exe" || editor_exe == "Code - Insiders.exe")
        {
            // note that we are invoking cmd silently but Code.exe is relaunched from there
            cmd_execute_background(
                Command("cmd").string_arg("/c").raw_arg(Strings::concat('"', cmd_line.command_line(), R"( <NUL")")));
            Checks::exit_success(VCPKG_LINE_INFO);
        }
#endif // ^^^ _WIN32

        Checks::exit_with_code(VCPKG_LINE_INFO, cmd_execute(cmd_line).value_or_exit(VCPKG_LINE_INFO));
    }

    void EditCommand::perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths) const
    {
        Edit::perform_and_exit(args, paths);
    }
}
