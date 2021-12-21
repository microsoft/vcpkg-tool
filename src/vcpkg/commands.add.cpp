#include <vcpkg/base/basic_checks.h>
#include <vcpkg/base/strings.h>

#include <vcpkg/commands.add.h>
#include <vcpkg/configure-environment.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

using namespace vcpkg;

namespace
{
    const CommandStructure AddCommandStructure = {
        Strings::format(
            "Adds the indicated port or artifact to the manifest associated with the current directory.\n%s\n%s",
            create_example_string("add port png"),
            create_example_string("add artifact cmake")),
        2,
        2,
        {{}, {}},
        nullptr,
    };
}

namespace vcpkg::Commands
{
    void AddCommand::perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths) const
    {
        args.parse_arguments(AddCommandStructure);
        auto&& selector = args.command_arguments[0];
        if (selector == "artifact")
        {
            std::string ce_args[] = {"add", args.command_arguments[1]};
            Checks::exit_with_code(VCPKG_LINE_INFO, run_configure_environment_command(paths, ce_args));
        }

        if (selector == "port")
        {
            auto manifest = paths.get_manifest().get();
            if (!manifest)
            {
                Checks::exit_with_message(VCPKG_LINE_INFO, "add port requires an active manifest file.\n");
            }

            Checks::exit_with_message(VCPKG_LINE_INFO, "add port will be added in a future vcpkg release.\n");
        }

        Checks::exit_with_message(VCPKG_LINE_INFO, "The first parmaeter to add must be 'artifact' or 'port'.\n");
    }
}
