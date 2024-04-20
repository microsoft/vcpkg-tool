#include <vcpkg/base/checks.h>
#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.acquire.h>
#include <vcpkg/configure-environment.h>
#include <vcpkg/vcpkgcmdarguments.h>

using namespace vcpkg;

namespace
{
    constexpr CommandMultiSetting AcquireMultiOptions[] = {
        {SwitchVersion, msgArtifactsOptionVersion},
    };
} // unnamed namespace

namespace vcpkg
{
    constexpr CommandMetadata CommandAcquireMetadata{
        "acquire",
        msgCmdAcquireSynopsis,
        {msgCmdAcquireExample1, "vcpkg acquire cmake"},
        Undocumented,
        AutocompletePriority::Public,
        1,
        SIZE_MAX,
        {CommonAcquireArtifactSwitches, {}, AcquireMultiOptions},
        nullptr,
    };

    void command_acquire_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        auto parsed = args.parse_arguments(CommandAcquireMetadata);
        std::vector<std::string> ecmascript_args;
        ecmascript_args.emplace_back("acquire");
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
}
