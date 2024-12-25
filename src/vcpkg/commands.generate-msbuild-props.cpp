#include <vcpkg/base/checks.h>
#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.generate-msbuild-props.h>
#include <vcpkg/configure-environment.h>
#include <vcpkg/vcpkgcmdarguments.h>

using namespace vcpkg;

namespace
{
    constexpr CommandSetting GenerateMSBuildPropsOptions[] = {
        {SwitchMSBuildProps, msgArtifactsOptionMSBuildProps},
    };
} // unnamed namespace

namespace vcpkg
{
    constexpr CommandMetadata CommandGenerateMsbuildPropsMetadata{
        "generate-msbuild-props",
        msgCmdGenerateMSBuildPropsSynopsis,
        {msgCmdGenerateMSBuildPropsExample1, msgCmdGenerateMSBuildPropsExample2},
        Undocumented,
        AutocompletePriority::Internal,
        0,
        0,
        {CommonAcquireArtifactSwitches, GenerateMSBuildPropsOptions},
        nullptr,
    };

    void command_generate_msbuild_props_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        auto parsed = args.parse_arguments(CommandGenerateMsbuildPropsMetadata);
        std::vector<std::string> ecmascript_args;
        ecmascript_args.emplace_back("generate-msbuild-props");

        forward_common_artifacts_arguments(ecmascript_args, parsed);

        if (!Util::Maps::contains(parsed.settings, SwitchMSBuildProps))
        {
            Checks::msg_exit_with_error(VCPKG_LINE_INFO, msgOptionRequiresAValue, msg::option = SwitchMSBuildProps);
        }

        Checks::exit_with_code(VCPKG_LINE_INFO, run_configure_environment_command(paths, ecmascript_args));
    }
} // namespace vcpkg
