#include <vcpkg/base/files.h>
#include <vcpkg/base/system.print.h>

#include <vcpkg/commands.init-registry.h>
#include <vcpkg/vcpkgcmdarguments.h>

namespace vcpkg::Commands::InitRegistry
{
    void perform_and_exit(const VcpkgCmdArguments& args, Files::Filesystem& fs)
    {
        if (args.command_arguments.empty())
        {
            Checks::exit_with_message(
                VCPKG_LINE_INFO,
                "You have to provide a path to a directory where the registry should be created as argument. "
                "Pass '.' to create the registry in the current directory.");
        }
        if (args.command_arguments.size() > 1)
        {
            Checks::exit_with_message(VCPKG_LINE_INFO, "Only pass one directory as argument to this command.");
        }
        auto path = fs::u8path(args.command_arguments.front());
        if (path.is_relative())
        {
            path = fs.current_path(VCPKG_LINE_INFO) / path;
        }
        const auto ports = path / fs::u8path("ports");
        const auto baseline = path / fs::u8path("versions") / fs::u8path("baseline.json");
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
        System::print2("Sucessfully created registry at ", fs::u8string(path), "\n");
        Checks::exit_success(VCPKG_LINE_INFO);
    }

    void InitRegistryCommand::perform_and_exit(const VcpkgCmdArguments& args, Files::Filesystem& fs) const
    {
        InitRegistry::perform_and_exit(args, fs);
    }
}
