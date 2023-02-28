
#include <vcpkg/base/checks.h>
#include <vcpkg/base/expected.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/git.h>
#include <vcpkg/base/json.h>

#include <vcpkg/commands.lint-port.h>
#include <vcpkg/configuration.h>
#include <vcpkg/paragraphs.h>
#include <vcpkg/portfileprovider.h>
#include <vcpkg/portlint.h>
#include <vcpkg/registries.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>
#include <vcpkg/versions.h>

using namespace vcpkg;

namespace
{
    constexpr StringLiteral OPTION_ALL = "all";
    constexpr StringLiteral OPTION_FIX = "fix";
    constexpr StringLiteral OPTION_INCREASE_VERSION = "increase-version";
}

namespace vcpkg::Commands::LintPort
{
    const CommandSwitch COMMAND_SWITCHES[] = {
        {OPTION_ALL, []() { return msg::format(msgCmdLintPortOptAllPorts); }},
        {OPTION_FIX, []() { return msg::format(msgCmdLintPortOptFix); }},
        {OPTION_INCREASE_VERSION, []() { return msg::format(msgCmdLintPortOptIncreaseVersion); }}};

    const CommandStructure COMMAND_STRUCTURE{
        []() { return create_example_string(R"###(x-lint-port <port name>)###"); },
        0,
        INT_MAX,
        {{COMMAND_SWITCHES}, {}, {}},
        nullptr,
    };

    void perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        auto parsed_args = args.parse_arguments(COMMAND_STRUCTURE);
        const bool add_all = Util::Sets::contains(parsed_args.switches, OPTION_ALL);
        const auto fix = Util::Enum::to_enum<Lint::Fix>(Util::Sets::contains(parsed_args.switches, OPTION_FIX));
        const bool increase_version = Util::Sets::contains(parsed_args.switches, OPTION_INCREASE_VERSION);

        auto& fs = paths.get_filesystem();

        std::vector<std::string> port_names;
        if (!args.command_arguments.empty())
        {
            if (add_all)
            {
                msg::println_warning(msgAddVersionIgnoringOptionAll, msg::option = OPTION_ALL);
            }
            port_names = args.command_arguments;
        }
        else
        {
            Checks::msg_check_exit(VCPKG_LINE_INFO,
                                   add_all,
                                   msgAddVersionUseOptionAll,
                                   msg::command_name = "x-lint-port",
                                   msg::option = OPTION_ALL);

            for (auto&& port_dir : fs.get_directories_non_recursive(paths.builtin_ports_directory(), VCPKG_LINE_INFO))
            {
                port_names.emplace_back(port_dir.stem().to_string());
            }
        }

        for (auto&& port_name : port_names)
        {
            auto maybe_scf = Paragraphs::try_load_port(fs, paths.builtin_ports_directory() / port_name);
            if (!maybe_scf)
            {
                msg::println_error(msgAddVersionLoadPortFailed, msg::package_name = port_name);
                print_error_message(maybe_scf.error());
                Checks::check_exit(VCPKG_LINE_INFO, !add_all);
                continue;
            }
            SourceControlFileAndLocation scf{std::move(maybe_scf.value_or_exit(VCPKG_LINE_INFO)),
                                             paths.builtin_ports_directory() / port_name};
            Lint::Status s = Lint::check_license_expression(*scf.source_control_file, fix);
            s |= Lint::check_used_version_scheme(*scf.source_control_file, fix);
            s |= Lint::check_portfile_deprecated_functions(fs, scf, fix);

            if (s == Lint::Status::Fixed || s == Lint::Status::PartiallyFixed)
            {
                if (s == Lint::Status::Fixed)
                {
                    msg::print(msg::format(msgLintPortErrorsFixed, msg::package_name = port_name).append_raw("\n\n"));
                }
                else
                {
                    msg::print(Color::error,
                               msg::format(msgLintPortErrors, msg::package_name = port_name).append_raw("\n\n"));
                }
                if (increase_version) scf.source_control_file->core_paragraph->port_version += 1;
                scf.source_control_file->canonicalize();
                std::error_code ec;
                fs.write_contents(scf.source_location / "vcpkg.json",
                                  Json::stringify(serialize_manifest(*scf.source_control_file)),
                                  ec);
                if (ec)
                {
                    Checks::msg_exit_with_error(
                        VCPKG_LINE_INFO,
                        msg::format(msgFailedToWriteManifest, msg::path = scf.source_location / "vcpkg.json")
                            .append_raw(": ")
                            .append_raw(ec.message()));
                }
            }
            else if (s == Lint::Status::Problem)
            {
                msg::print(Color::error,
                           msg::format(msgLintPortErrors, msg::package_name = port_name).append_raw("\n\n"));
            }
        }
        Checks::exit_success(VCPKG_LINE_INFO);
    }

    void LintPortCommand::perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths) const
    {
        LintPort::perform_and_exit(args, paths);
    }
}
