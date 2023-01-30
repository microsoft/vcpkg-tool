#pragma once

#include <vcpkg/base/files.h>
#include <vcpkg/base/messages.h>

#include <vcpkg/commands.interface.h>
#include <vcpkg/vcpkgcmdarguments.h>

namespace vcpkg::Commands
{
    struct FormatArgMismatches
    {
        std::vector<StringView> arguments_without_comment;
        std::vector<StringView> comments_without_argument;
    };

    std::vector<StringView> get_all_format_args(StringView fstring, LocalizedString& error);
    FormatArgMismatches get_format_arg_mismatches(StringView value, StringView comment, LocalizedString& error);

    struct GenerateDefaultMessageMapCommand : BasicCommand
    {
        void perform_and_exit(const VcpkgCmdArguments&, Filesystem&) const override;
    };
}
