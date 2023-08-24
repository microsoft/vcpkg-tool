#include <vcpkg/base/checks.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.acquire.h>
#include <vcpkg/configure-environment.h>
#include <vcpkg/vcpkgcmdarguments.h>

using namespace vcpkg;

namespace
{
    constexpr StringLiteral SWITCH_WINDOWS = "windows";
    constexpr StringLiteral SWITCH_OSX = "osx";
    constexpr StringLiteral SWITCH_LINUX = "linux";
    constexpr StringLiteral SWITCH_FREEBSD = "freebsd";
    constexpr StringLiteral SWITCH_X86 = "x86";
    constexpr StringLiteral SWITCH_X64 = "x64";
    constexpr StringLiteral SWITCH_ARM = "arm";
    constexpr StringLiteral SWITCH_ARM64 = "arm64";
    constexpr StringLiteral SWITCH_TARGET_X86 = "target:x86";
    constexpr StringLiteral SWITCH_TARGET_X64 = "target:x64";
    constexpr StringLiteral SWITCH_TARGET_ARM = "target:arm";
    constexpr StringLiteral SWITCH_TARGET_ARM64 = "target:arm64";
    constexpr StringLiteral SWITCH_FORCE = "force";
    constexpr StringLiteral SWITCH_ALL_LANGUAGES = "all-languages";

    constexpr CommandSwitch AcquireSwitches[] = {
        {SWITCH_WINDOWS, [] { return msg::format(msgArtifactsSwitchWindows); }},
        {SWITCH_OSX, [] { return msg::format(msgArtifactsSwitchOsx); }},
        {SWITCH_LINUX, [] { return msg::format(msgArtifactsSwitchLinux); }},
        {SWITCH_FREEBSD, [] { return msg::format(msgArtifactsSwitchFreebsd); }},
        {SWITCH_X86, [] { return msg::format(msgArtifactsSwitchX86); }},
        {SWITCH_X64, [] { return msg::format(msgArtifactsSwitchX64); }},
        {SWITCH_ARM, [] { return msg::format(msgArtifactsSwitchARM); }},
        {SWITCH_ARM64, [] { return msg::format(msgArtifactsSwitchARM64); }},
        {SWITCH_TARGET_X86, [] { return msg::format(msgArtifactsSwitchTargetX86); }},
        {SWITCH_TARGET_X64, [] { return msg::format(msgArtifactsSwitchTargetX64); }},
        {SWITCH_TARGET_ARM, [] { return msg::format(msgArtifactsSwitchTargetARM); }},
        {SWITCH_TARGET_ARM64, [] { return msg::format(msgArtifactsSwitchTargetARM64); }},
        {SWITCH_FORCE, [] { return msg::format(msgArtifactsSwitchForce); }},
        {SWITCH_ALL_LANGUAGES, [] { return msg::format(msgArtifactsSwitchAllLanguages); }},
    };

    constexpr const StringLiteral* operating_systems[] = {&SWITCH_WINDOWS, &SWITCH_OSX, &SWITCH_LINUX, &SWITCH_FREEBSD};
    constexpr const StringLiteral* host_platforms[] = {&SWITCH_X86, &SWITCH_X64, &SWITCH_ARM, &SWITCH_ARM64};
    constexpr const StringLiteral* target_platforms[] = {
        &SWITCH_TARGET_X86, &SWITCH_TARGET_X64, &SWITCH_TARGET_ARM, &SWITCH_TARGET_ARM64};

    constexpr StringLiteral OPTION_VERSION = "version";

    constexpr CommandMultiSetting AcquireMultiOptions[] = {
        {OPTION_VERSION, [] { return msg::format(msgArtifactsOptionVersion); }},
    };
} // unnamed namespace

namespace vcpkg
{
    constexpr CommandMetadata CommandAcquireMetadata = {
        [] { return create_example_string("acquire cmake"); },
        1,
        SIZE_MAX,
        {{AcquireSwitches}, {}, {AcquireMultiOptions}},
        nullptr,
    };

    void command_acquire_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        auto parsed = args.parse_arguments(CommandAcquireMetadata);
        std::vector<std::string> ecmascript_args;
        ecmascript_args.emplace_back("acquire");

        auto&& switches = parsed.switches;
        for (auto&& parsed_switch : switches)
        {
            ecmascript_args.push_back(fmt::format("--{}", parsed_switch));
        }

        if (more_than_one_mapped(operating_systems, parsed.switches))
        {
            Checks::msg_exit_with_error(VCPKG_LINE_INFO, msgArtifactsSwitchOnlyOneOperatingSystem);
        }

        if (more_than_one_mapped(host_platforms, parsed.switches))
        {
            Checks::msg_exit_with_error(VCPKG_LINE_INFO, msgArtifactsSwitchOnlyOneHostPlatform);
        }

        if (more_than_one_mapped(target_platforms, parsed.switches))
        {
            Checks::msg_exit_with_error(VCPKG_LINE_INFO, msgArtifactsSwitchOnlyOneTargetPlatform);
        }

        auto maybe_versions = Util::lookup_value(parsed.multisettings, OPTION_VERSION);
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
