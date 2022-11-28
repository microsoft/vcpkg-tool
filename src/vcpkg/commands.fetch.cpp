#include <vcpkg/base/system.print.h>

#include <vcpkg/commands.fetch.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

namespace vcpkg::Commands::Fetch
{
    static constexpr CommandSwitch STDERR_STATUS = {
        "x-stderr-status",
        "Direct status/downloading messages to stderr rather than stdout. (Errors/failures still go to stdout)"};

    const CommandStructure COMMAND_STRUCTURE = {
        Strings::format("The argument should be a tool name\n%s", create_example_string("fetch cmake")),
        1,
        1,
        {{&STDERR_STATUS, 1}},
        nullptr,
    };

    void perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        const auto parsed = args.parse_arguments(COMMAND_STRUCTURE);
        const bool stderr_status = Util::Sets::contains(parsed.switches, STDERR_STATUS.name);
        const std::string tool = args.command_arguments[0];
        const Path& tool_path = paths.get_tool_exe(tool, stderr_status ? stderr_sink : stdout_sink);
        msg::write_unlocalized_text_to_stdout(Color::none, tool_path.native() + '\n');
        Checks::exit_success(VCPKG_LINE_INFO);
    }

    void FetchCommand::perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths) const
    {
        Fetch::perform_and_exit(args, paths);
    }
}
