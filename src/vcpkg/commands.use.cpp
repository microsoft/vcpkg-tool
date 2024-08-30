#include <vcpkg/base/checks.h>
#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.use.h>
#include <vcpkg/configure-environment.h>
#include <vcpkg/vcpkgcmdarguments.h>

#include <limits.h>

using namespace vcpkg;

namespace
{
    constexpr CommandSetting UseOptions[] = {
        {SwitchMSBuildProps, msgArtifactsOptionMSBuildProps},
    };

    constexpr CommandMultiSetting UseMultiOptions[] = {
        {SwitchVersion, msgArtifactsOptionVersion},
    };
} // unnamed namespace

namespace vcpkg
{
    constexpr CommandMetadata CommandUseMetadata{
        "use",
        msgCmdUseSynopsis,
        {msgCmdUseExample1, "vcpkg-shell use cmake"},
        Undocumented,
        AutocompletePriority::Public,
        1,
        SIZE_MAX,
        {CommonAcquireArtifactSwitches, UseOptions, UseMultiOptions},
        nullptr,
    };

    void command_use_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        auto parsed = args.parse_arguments(CommandUseMetadata);
        std::vector<std::string> ecmascript_args;
        ecmascript_args.emplace_back("use");

        forward_common_artifacts_arguments(ecmascript_args, parsed);

        auto maybe_versions = Util::lookup_value(parsed.multisettings, SwitchVersion);
        if (auto versions = maybe_versions.get())
        {
            if (versions->size() != parsed.command_arguments.size())
            {
                Checks::msg_exit_with_error(VCPKG_LINE_INFO, msgArtifactsOptionVersionMismatch);
            }

            for (auto&& version : *versions)
            {
                ecmascript_args.push_back("--version");
                ecmascript_args.push_back(version);
            }
        }

        ecmascript_args.insert(ecmascript_args.end(),
                               std::make_move_iterator(parsed.command_arguments.begin()),
                               std::make_move_iterator(parsed.command_arguments.end()));

        Checks::exit_with_code(VCPKG_LINE_INFO, run_configure_environment_command(paths, ecmascript_args));
    }
} // namespace vcpkg
