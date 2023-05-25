#include <vcpkg/base/fwd/message_sinks.h>

#include <vcpkg/archives.h>
#include <vcpkg/commands.extract.h>
#include <vcpkg/commands.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

namespace vcpkg::Commands
{
    const CommandStructure ExtractCommandStructure = {
        [] { return msg::format(msgExtractHelp); },
        2,
        2,
        {{}, {}, {}},
        nullptr,
    };

    void extract_command_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        auto& fs = paths.get_filesystem();
        auto parse_args = args.parse_arguments(ExtractCommandStructure);

        auto archive_path = Path{parse_args.command_arguments[0]};
        auto destination_path = Path{parse_args.command_arguments[1]} / archive_path.stem();

        extract_archive(fs, paths.get_tool_cache(), null_sink, Path{archive_path}, Path{destination_path});

        auto mapping = strip_mapping(fs, destination_path, 1);
        for (auto&& file : mapping)
        {
            msg::write_unlocalized_text_to_stdout(Color::none,
                                                  fmt::format("Old Path: {}\nNew Path{}\n\n",
                                                              file.first.generic_u8string(),
                                                              file.second.generic_u8string()));
		}

        Checks::exit_success(VCPKG_LINE_INFO);
    }
}