#include <vcpkg/commands.extract.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>
#include <vcpkg/archives.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/parse.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.h>
#include <vcpkg/base/system.process.h>
#include <vcpkg/base/util.h>

#include <vcpkg/archives.h>
#include <vcpkg/commands.h>
#include <vcpkg/tools.h>

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
        //auto& archive = args.parse_arguments(ExtractCommandStructure);
        MessageSink& null_sink = null_sink_instance;

        msg::write_unlocalized_text_to_stdout(Color::none, "extracting...\n");

        extract_archive(fs, paths.get_tool_cache(), null_sink, Path{"C:\\dev\\Testing\\BUG1797696.zip"}, Path{"C:\\dev\\Testing"});

		msg::write_unlocalized_text_to_stdout(Color::none, "Hello World!");
	}
}