#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/json.h>
#include <vcpkg/base/optional.h>

#include <vcpkg/commands.z-print-config.h>
#include <vcpkg/installedpaths.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

#include <string>

using namespace vcpkg;

namespace
{
    void opt_add(Json::Object& obj, StringLiteral key, const Optional<Path>& opt)
    {
        if (auto p = opt.get())
        {
            obj.insert(key, p->native());
        }
    }
} // unnamed namespace

namespace vcpkg
{
    constexpr CommandMetadata CommandZPrintConfigMetadata{
        "z-print-config",
        {/*intentionally undocumented*/},
        {},
        Undocumented,
        AutocompletePriority::Never,
        0,
        0,
        {},
        nullptr,
    };

    void command_z_print_config_and_exit(const VcpkgCmdArguments& args,
                                         const VcpkgPaths& paths,
                                         Triplet default_triplet,
                                         Triplet host_triplet)
    {
        (void)args.parse_arguments(CommandZPrintConfigMetadata);
        Json::Object obj;
        obj.insert(JsonIdDownloads, paths.downloads.native());
        obj.insert(JsonIdDefaultTriplet, default_triplet.canonical_name());
        obj.insert(JsonIdHostTriplet, host_triplet.canonical_name());
        obj.insert(JsonIdVcpkgRoot, paths.root.native());
        obj.insert(JsonIdTools, paths.tools.native());
        if (auto ci_env = args.detected_ci_environment_name().get())
        {
            obj.insert(JsonIdDetectedCIEnvironment, *ci_env);
        }
        if (auto i = paths.maybe_installed().get())
        {
            obj.insert(JsonIdInstalled, i->root().native());
        }
        opt_add(obj, JsonIdBuildtrees, paths.maybe_buildtrees());
        opt_add(obj, JsonIdPackages, paths.maybe_packages());
        if (paths.maybe_installed())
        {
            obj.insert(JsonIdVersionsOutput, paths.versions_output().native());
            obj.insert(JsonIdManifestModeEnabled, Json::Value::boolean(paths.manifest_mode_enabled()));
        }
        obj.sort_keys();
        msg::write_unlocalized_text(Color::none, Json::stringify(obj) + "\n");
        Checks::exit_success(VCPKG_LINE_INFO);
    }
} // namespace vcpkg
