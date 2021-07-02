#include <vcpkg/base/files.h>
#include <vcpkg/base/system.print.h>

#include <vcpkg/commands.init-registry.h>
#include <vcpkg/vcpkgcmdarguments.h>

namespace vcpkg::Commands::InitRegistry
{
    static const CommandStructure COMMAND_STRUCTURE = {
        create_example_string(R"(x-init-registry .)"),
        1,
        1,
        {{}, {}, {}},
        nullptr,
    };

    void perform_and_exit(const VcpkgCmdArguments& args, Filesystem& fs)
    {
        auto parsed_args = args.parse_arguments(COMMAND_STRUCTURE);

        const auto path_argument = vcpkg::u8path(args.command_arguments.front());
        const auto path = combine(fs.current_path(VCPKG_LINE_INFO), path_argument);
        if (!fs.exists(path / vcpkg::u8path(".git")))
        {
            vcpkg::printf(Color::error,
                          "Could not create registry at %s because this is not a git repository root.\n"
                          "Use `git init %s` to create a git repository in this folder.\n",
                          u8string(path),
                          u8string(path_argument));
            Checks::exit_fail(VCPKG_LINE_INFO);
        }
        const auto ports = path / vcpkg::u8path("ports");
        const auto baseline = path / vcpkg::u8path("versions") / vcpkg::u8path("baseline.json");
        if (!fs.exists(ports))
        {
            fs.create_directories(ports, VCPKG_LINE_INFO);
        }
        if (!fs.exists(baseline))
        {
            const auto content = R"({
  "default": {}
})";
            fs.write_contents_and_dirs(baseline, content, VCPKG_LINE_INFO);
        }
        print2("Sucessfully created registry at ", vcpkg::u8string(path), "\n");
        Checks::exit_success(VCPKG_LINE_INFO);
    }

    void InitRegistryCommand::perform_and_exit(const VcpkgCmdArguments& args, Filesystem& fs) const
    {
        InitRegistry::perform_and_exit(args, fs);
    }
}
