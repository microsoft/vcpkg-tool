#include <vcpkg/base/util.h>

#include <vcpkg/commands.find.h>
#include <vcpkg/commands.search.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

using namespace vcpkg;

namespace
{
    constexpr StringLiteral OPTION_FULLDESC = "x-full-desc"; // TODO: This should find a better home, eventually
    constexpr StringLiteral OPTION_JSON = "x-json";

    constexpr CommandSwitch SearchSwitches[] = {
        {OPTION_FULLDESC, msgHelpTextOptFullDesc},
        {OPTION_JSON, msgJsonSwitch},
    };
} // unnamed namespace

namespace vcpkg
{
    constexpr CommandMetadata CommandSearchMetadata = {
        "search",
        msgHelpSearchCommand,
        {msgCmdSearchExample1, "vcpkg search png"},
        "https://learn.microsoft.com/vcpkg/commands/search",
        AutocompletePriority::Public,
        0,
        1,
        {SearchSwitches, {}},
        nullptr,
    };

    void command_search_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        msg::default_output_stream = OutputStream::StdErr;
        const ParsedArguments options = args.parse_arguments(CommandSearchMetadata);
        const bool full_description = Util::Sets::contains(options.switches, OPTION_FULLDESC);
        Optional<StringView> filter;
        if (!options.command_arguments.empty())
        {
            filter = StringView{options.command_arguments[0]};
        }

        perform_find_port_and_exit(
            paths, full_description, Util::Sets::contains(options.switches, OPTION_JSON), filter, paths.overlay_ports);
    }
} // namespace vcpkg
