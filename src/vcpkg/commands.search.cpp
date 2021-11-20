#include <vcpkg/base/system.print.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.find.h>
#include <vcpkg/commands.search.h>
#include <vcpkg/help.h>
#include <vcpkg/vcpkgcmdarguments.h>

namespace vcpkg::Commands
{
    static constexpr StringLiteral OPTION_FULLDESC = "x-full-desc"; // TODO: This should find a better home, eventually
    static constexpr StringLiteral OPTION_JSON = "x-json";

    static constexpr std::array<CommandSwitch, 2> SearchSwitches = {{
        {OPTION_FULLDESC, "Do not truncate long text"},
        {OPTION_JSON, "(experimental) List libraries in JSON format"},
    }};

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
        const bool enable_json = Util::Sets::contains(options.switches, OPTION_JSON);
        Optional<StringView> filter;
        if (!args.command_arguments.empty())
        {
            filter = StringView{args.command_arguments[0]};
        }

        perform_find_port_and_exit(paths, full_description, enable_json, filter, args.overlay_ports);
    }
}
