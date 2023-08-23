#include <vcpkg/binarycaching.h>
#include <vcpkg/cmakevars.h>
#include <vcpkg/commands.build-external.h>
#include <vcpkg/commands.build.h>
#include <vcpkg/commands.help.h>
#include <vcpkg/input.h>
#include <vcpkg/portfileprovider.h>
#include <vcpkg/registries.h>
#include <vcpkg/vcpkgcmdarguments.h>

namespace vcpkg::Commands::BuildExternal
{
    const CommandStructure COMMAND_STRUCTURE = {
        [] { return create_example_string(R"(build-external zlib2 C:\path\to\dir\with\vcpkg.json)"); },
        2,
        2,
        {},
        nullptr,
    };

    void perform_and_exit(const VcpkgCmdArguments& args,
                          const VcpkgPaths& paths,
                          Triplet default_triplet,
                          Triplet host_triplet)
    {
        const ParsedArguments options = args.parse_arguments(COMMAND_STRUCTURE);

        bool default_triplet_used = false;
        const FullPackageSpec spec = check_and_get_full_package_spec(options.command_arguments[0],
                                                                     default_triplet,
                                                                     default_triplet_used,
                                                                     COMMAND_STRUCTURE.get_example_text(),
                                                                     paths.get_triplet_db());

        auto overlays = paths.overlay_ports;
        overlays.insert(overlays.begin(), options.command_arguments[1]);

        auto& fs = paths.get_filesystem();
        auto registry_set = paths.make_registry_set();
        PathsPortFileProvider provider(fs, *registry_set, make_overlay_provider(fs, paths.original_cwd, overlays));
        Build::perform_and_exit_ex(args, spec, host_triplet, provider, null_build_logs_recorder(), paths);
    }
}
