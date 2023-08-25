#include <vcpkg/base/files.h>

#include <vcpkg/commands.vsinstances.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>
#include <vcpkg/visualstudio.h>

namespace vcpkg
{
    constexpr CommandMetadata CommandVsInstancesMetadata{
        "x-vsinstances",
        msgCmdVSInstancesSynopsis,
        {"vcpkg x-vsinstances"},
        AutocompletePriority::Public,
        0,
        0,
        {},
        nullptr,
    };

    void command_vs_instances_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
#if defined(_WIN32)
        const ParsedArguments parsed_args = args.parse_arguments(CommandVsInstancesMetadata);

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
