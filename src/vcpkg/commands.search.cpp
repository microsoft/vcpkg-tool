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

    void display_results(const std::vector<PackageMetrics>& results) {
        System::print2("\nSearch Results:\n");
        
        Table output;
        output.format()
            .line_format("| {0,-20} | {1,10} | {2,-12} | {3,10} |")
            .header_format("| {0,-20} | {1,-10} | {2,-12} | {3,-10} |")
            .format_cells()
            .column("Package")
            .column("Size")
            .column("Last Update")
            .column("Popularity");

        for (const auto& pkg : results) {
            std::string size_str = format_bytes(pkg.size);
            std::string date_str = format_time(pkg.last_update);
            
            output.format()
                .line_format("| {0,-20} | {1,10} | {2,-12} | {3,10} |")
                .add_row({
                    pkg.name,
                    size_str,
                    date_str,
                    std::to_string(pkg.popularity)
                });
        }

        System::print2(output.to_string());
    }
} // namespace vcpkg
