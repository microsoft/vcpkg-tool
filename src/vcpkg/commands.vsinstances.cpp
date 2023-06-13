#include <vcpkg/base/files.h>

#include <vcpkg/commands.help.h>
#include <vcpkg/commands.vsinstances.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>
#include <vcpkg/visualstudio.h>

namespace vcpkg::Commands::VSInstances
{
    const CommandStructure COMMAND_STRUCTURE = {
        [] { return create_example_string("x-vsinstances"); },
        0,
        0,
        {{}, {}},
        nullptr,
    };

    void perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
#if defined(_WIN32)
        const ParsedArguments parsed_args = args.parse_arguments(COMMAND_STRUCTURE);

        const auto instances = vcpkg::VisualStudio::get_visual_studio_instances(paths.get_filesystem());
        for (const std::string& instance : instances)
        {
            msg::write_unlocalized_text_to_stdout(Color::none, instance + "\n");
        }

        Checks::exit_success(VCPKG_LINE_INFO);
#else
        (void)args;
        (void)paths;
        Checks::msg_exit_with_message(VCPKG_LINE_INFO, msgWindowsOnlyCommand);
#endif
    }
}
