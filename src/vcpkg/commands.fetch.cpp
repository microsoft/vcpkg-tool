#include <vcpkg/base/fwd/message_sinks.h>

#include <vcpkg/base/util.h>

#include <vcpkg/commands.fetch.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

using namespace vcpkg;

namespace
{
    static constexpr CommandSwitch STDERR_STATUS[] = {
        {"x-stderr-status", msgCmdFetchOptXStderrStatus},
    };
} // unnamed namespace

namespace vcpkg
{
    constexpr CommandMetadata CommandFetchMetadata{
        "fetch",
        msgCmdFetchSynopsis,
        {"vcpkg fetch python"},
        Undocumented,
        AutocompletePriority::Public,
        1,
        1,
        {STDERR_STATUS},
        nullptr,
    };

    void command_fetch_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        const auto parsed = args.parse_arguments(CommandFetchMetadata);
        const bool stderr_status = Util::Sets::contains(parsed.switches, STDERR_STATUS[0].name);
        const std::string tool = parsed.command_arguments[0];
        if (const auto* tool_path =
                paths.get_tool_path(stderr_status ? stderr_diagnostic_context : console_diagnostic_context, tool))
        {
            msg::write_unlocalized_text_to_stdout(Color::none, tool_path->native() + '\n');
            Checks::exit_success(VCPKG_LINE_INFO);
        }
        else
        {
            Checks::exit_fail(VCPKG_LINE_INFO);
        }
    }
} // namespace vcpkg
