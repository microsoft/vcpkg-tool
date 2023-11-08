#include <vcpkg/binarycaching.h>
#include <vcpkg/cmakevars.h>
#include <vcpkg/commands.build-external.h>
#include <vcpkg/commands.build.h>
#include <vcpkg/input.h>
#include <vcpkg/portfileprovider.h>
#include <vcpkg/registries.h>
#include <vcpkg/vcpkgcmdarguments.h>

namespace vcpkg
{
    constexpr CommandMetadata CommandBuildExternalMetadata{
        "build-external",
        msgCmdBuildExternalSynopsis,
        {msgCmdBuildExternalExample1, msgCmdBuildExternalExample2},
        Undocumented,
        AutocompletePriority::Internal,
        2,
        2,
        {},
        nullptr,
    };

    void command_build_external_and_exit(const VcpkgCmdArguments& args,
                                         const VcpkgPaths& paths,
                                         Triplet default_triplet,
                                         Triplet host_triplet)
    {
        const ParsedArguments options = args.parse_arguments(CommandBuildExternalMetadata);

        bool default_triplet_used = false;
        const FullPackageSpec spec = check_and_get_full_package_spec(options.command_arguments[0],
                                                                     default_triplet,
                                                                     default_triplet_used,
                                                                     CommandBuildExternalMetadata.get_example_text(),
                                                                     paths.get_triplet_db());
        if (default_triplet_used)
        {
            print_default_triplet_warning(args, paths.get_triplet_db());
        }

        auto overlays = paths.overlay_ports;
        overlays.insert(overlays.begin(), options.command_arguments[1]);

        auto& fs = paths.get_filesystem();
        auto registry_set = paths.make_registry_set();
        PathsPortFileProvider provider(fs, *registry_set, make_overlay_provider(fs, paths.original_cwd, overlays));
        command_build_and_exit_ex(args, spec, host_triplet, provider, null_build_logs_recorder(), paths);
    }
}
