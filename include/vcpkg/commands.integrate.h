#pragma once

#include <vcpkg/fwd/vcpkgcmdarguments.h>
#include <vcpkg/fwd/vcpkgpaths.h>

#include <vcpkg/base/stringview.h>

#include <string>
#include <vector>

namespace vcpkg::Commands::Integrate
{
    extern const CommandStructure COMMAND_STRUCTURE;

    void perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths);
    void append_helpstring(HelpTableFormatter& table);
    LocalizedString get_helpstring();

    Optional<int> find_targets_file_version(StringView contents);
    std::vector<std::string> get_bash_source_completion_lines(StringView contents);

    struct ZshAutocomplete
    {
        std::vector<std::string> source_completion_lines;
        bool has_bashcompinit;
        bool has_autoload_bashcompinit;
    };
    ZshAutocomplete get_zsh_autocomplete_data(StringView contents);
}
