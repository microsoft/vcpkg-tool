#include <vcpkg/base/system.print.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.find.h>
#include <vcpkg/commands.search.h>
#include <vcpkg/help.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

namespace vcpkg::Commands
{
    static constexpr StringLiteral OPTION_FULLDESC = "x-full-desc"; // TODO: This should find a better home, eventually

    static constexpr std::array<CommandSwitch, 1> SearchSwitches = {{{OPTION_FULLDESC, "Do not truncate long text"}}};

    const CommandStructure SearchCommandStructure = {
        Strings::format(
            "The argument should be a substring to search for, or no argument to display all libraries.\n%s",
            create_example_string("search png")),
        0,
        1,
        {SearchSwitches, {}},
        nullptr,
    };

    void SearchCommand::perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths) const
    {
        const ParsedArguments options = args.parse_arguments(SearchCommandStructure);
        const bool full_description = Util::Sets::contains(options.switches, OPTION_FULLDESC);
        Optional<StringView> filter;
        if (!args.command_arguments.empty())
        {
            filter = StringView{args.command_arguments[0]};
        }

        perform_find_port_and_exit(paths, full_description, args.json.value_or(false), filter, paths.overlay_ports);
    }
}
