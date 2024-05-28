#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.find.h>
#include <vcpkg/commands.search.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

using namespace vcpkg;

namespace
{
    constexpr CommandSwitch SearchSwitches[] = {
        {SwitchXFullDesc, msgHelpTextOptFullDesc},
        {SwitchXJson, msgJsonSwitch},
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
        const bool full_description = Util::Sets::contains(options.switches, SwitchXFullDesc);
        Optional<StringView> filter;
        if (!options.command_arguments.empty())
        {
            filter = StringView{options.command_arguments[0]};
        }

        perform_find_port_and_exit(
            paths, full_description, Util::Sets::contains(options.switches, SwitchXJson), filter, paths.overlay_ports);
    }
} // namespace vcpkg
