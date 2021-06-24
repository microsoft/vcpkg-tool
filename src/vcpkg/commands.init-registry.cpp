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

        auto path = vcpkg::Files::u8path(args.command_arguments.front());
        path = combine(fs.current_path(VCPKG_LINE_INFO), path);
        const auto ports = path / vcpkg::Files::u8path("ports");
        const auto baseline = path / vcpkg::Files::u8path("versions") / vcpkg::Files::u8path("baseline.json");
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
        print2("Sucessfully created registry at ", vcpkg::Files::u8string(path), "\n");
        Checks::exit_success(VCPKG_LINE_INFO);
    }

    void InitRegistryCommand::perform_and_exit(const VcpkgCmdArguments& args, Filesystem& fs) const
    {
        InitRegistry::perform_and_exit(args, fs);
    }
}
