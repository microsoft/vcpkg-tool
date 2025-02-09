#pragma once

#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

namespace vcpkg::Commands::Quick 
{
    void perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths);

    struct QuickCommandSpec 
    {
        static constexpr StringLiteral name = "quick";
        static constexpr StringLiteral description = "Manage and execute quick commands";
        static constexpr std::array<CommandSwitch, 3> SWITCHES = {
            CommandSwitch{"add", "Add a new quick command"},
            CommandSwitch{"remove", "Remove an existing quick command"},
            CommandSwitch{"list", "List all quick commands"}
        };

        static constexpr std::array<CommandSetting, 0> SETTINGS = {};
        static constexpr std::array<CommandMultiSetting, 0> MULTISETTINGS = {};

        static std::vector<std::string> get_example_arguments()
        {
            return {
                "quick --add update-all \"vcpkg update && vcpkg upgrade --no-dry-run\" \"Update all packages\"",
                "quick --remove update-all",
                "quick --list",
                "quick update-all"
            };
        }
    };
} 