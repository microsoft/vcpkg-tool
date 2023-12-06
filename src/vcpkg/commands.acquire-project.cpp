#include <vcpkg/base/checks.h>

#include <vcpkg/commands.acquire-project.h>
#include <vcpkg/configure-environment.h>
#include <vcpkg/vcpkgcmdarguments.h>

namespace vcpkg
{
    constexpr CommandMetadata CommandAcquireProjectMetadata{
        "acquire_project",
        msgCmdAcquireProjectSynopsis,
        {"vcpkg acquire-project"},
        Undocumented,
        AutocompletePriority::Public,
        0,
        0,
        {CommonAcquireArtifactSwitches},
        nullptr,
    };

    void command_acquire_project_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        auto parsed = args.parse_arguments(CommandAcquireProjectMetadata);
        std::vector<std::string> ecmascript_args;
        ecmascript_args.emplace_back("acquire-project");
        forward_common_artifacts_arguments(ecmascript_args, parsed);
        Checks::exit_with_code(VCPKG_LINE_INFO, run_configure_environment_command(paths, ecmascript_args));
    }
} // namespace vcpkg
