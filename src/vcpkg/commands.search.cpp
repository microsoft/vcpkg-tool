#include <vcpkg/base/util.h>

#include <vcpkg/commands.find.h>
#include <vcpkg/commands.help.h>
#include <vcpkg/commands.search.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

namespace vcpkg::Commands
{
    static constexpr StringLiteral OPTION_FULLDESC = "x-full-desc"; // TODO: This should find a better home, eventually
    static constexpr StringLiteral OPTION_JSON = "x-json";

    static constexpr std::array<CommandSwitch, 2> SearchSwitches = {
        {{OPTION_FULLDESC, []() { return msg::format(msgHelpTextOptFullDesc); }},
         {OPTION_JSON, []() { return msg::format(msgJsonSwitch); }}}};

    const CommandStructure SearchCommandStructure = {
        [] { return msg::format(msgSearchHelp).append_raw('\n').append(create_example_string("search png")); },
        0,
        1,
        {SearchSwitches, {}},
        nullptr,
    };

    void command_search_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        const ParsedArguments options = args.parse_arguments(SearchCommandStructure);
        const bool full_description = Util::Sets::contains(options.switches, OPTION_FULLDESC);
        Optional<StringView> filter;
        if (!options.command_arguments.empty())
        {
            filter = StringView{options.command_arguments[0]};
        }

        perform_find_port_and_exit(
            paths, full_description, Util::Sets::contains(options.switches, OPTION_JSON), filter, paths.overlay_ports);
    }
}
