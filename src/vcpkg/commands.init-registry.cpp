#include <vcpkg/base/files.h>
#include <vcpkg/base/system.print.h>

#include <vcpkg/commands.init-registry.h>

namespace vcpkg::Commands::InitRegistry
{
    void perform_and_exit(const VcpkgCmdArguments&, Files::Filesystem& fs)
    {
        const auto ports = fs::u8path("ports");
        const auto baseline = fs::u8path("versions") / fs::u8path("baseline.json");
        if (!fs.exists(ports))
        {
            fs.create_directory(ports, VCPKG_LINE_INFO);
        }
        if (!fs.exists(baseline))
        {
            const auto content = R"({
  "default": {}
})";
            fs.write_contents_and_dirs(baseline, content, VCPKG_LINE_INFO);
        }
        System::print2("Sucessfully created registry at ", fs::u8string(fs.current_path(VCPKG_LINE_INFO)), "\n");
        Checks::exit_success(VCPKG_LINE_INFO);
    }

    void InitRegistryCommand::perform_and_exit(const VcpkgCmdArguments& args, Files::Filesystem& fs) const
    {
        InitRegistry::perform_and_exit(args, fs);
    }
}
