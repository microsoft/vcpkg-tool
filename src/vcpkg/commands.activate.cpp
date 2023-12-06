#include <vcpkg/base/checks.h>

#include <vcpkg/commands.activate.h>
#include <vcpkg/configure-environment.h>
#include <vcpkg/vcpkgcmdarguments.h>

using namespace vcpkg;

namespace
{
    constexpr StringLiteral OPTION_MSBUILD_PROPS = "msbuild-props";
    constexpr StringLiteral OPTION_JSON = "json";

    constexpr CommandSetting ActivateOptions[] = {
        {OPTION_MSBUILD_PROPS, msgArtifactsOptionMSBuildProps},
        {OPTION_JSON, msgArtifactsOptionJson},
    };
} // unnamed namespace

namespace vcpkg
{
    constexpr CommandMetadata CommandActivateMetadata{
        "activate",
        msgCmdActivateSynopsis,
        {"vcpkg activate"},
        Undocumented,
        AutocompletePriority::Public,
        0,
        0,
        {CommonAcquireArtifactSwitches, ActivateOptions},
        nullptr,
    };

    void command_activate_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        auto parsed = args.parse_arguments(CommandActivateMetadata);
        std::vector<std::string> ecmascript_args;
        ecmascript_args.emplace_back("activate");
        forward_common_artifacts_arguments(ecmascript_args, parsed);
        Checks::exit_with_code(VCPKG_LINE_INFO, run_configure_environment_command(paths, ecmascript_args));
    }
} // namespace vcpkg
