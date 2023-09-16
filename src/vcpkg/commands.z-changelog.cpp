#include <vcpkg/commands.z-changelog.h>
#include <vcpkg/vcpkgcmdarguments.h>

namespace vcpkg
{
    constexpr CommandMetadata CommandZChangelogMetadata{
        "z-changelog",
        "Generate github.com/microsoft/vcpkg changelog",
        {},
        Undocumented,
        AutocompletePriority::Never,
        1,
        2,
        {},
        nullptr,
    };

    void command_z_changelog_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths) { }
}
