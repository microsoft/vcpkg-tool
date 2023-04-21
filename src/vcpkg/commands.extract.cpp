#include <vcpkg/base/fwd/message_sinks.h>

#include <vcpkg/archives.h>
#include <vcpkg/commands.h>
#include <vcpkg/commands.extract.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

namespace vcpkg::Commands
{
    const CommandStructure ExtractCommandStructure = {
        [] { return create_example_string("x-extract path/to/archive path/to/destination"); },
        2,
        2,
        {{}, {}, {}},
        nullptr,
    };

    void extract_command_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
         auto& fs = paths.get_filesystem();
         auto parse_args = args.parse_arguments(ExtractCommandStructure);

         auto archive_path = parse_args.command_arguments[0];
         auto destination_path = parse_args.command_arguments[1];

         extract_archive(fs, paths.get_tool_cache(), null_sink, Path{archive_path}, Path{destination_path});
    }
}