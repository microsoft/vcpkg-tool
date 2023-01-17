#include <vcpkg/binarycaching.h>
#include <vcpkg/build.h>
#include <vcpkg/cmakevars.h>
#include <vcpkg/commands.buildexternal.h>
#include <vcpkg/help.h>
#include <vcpkg/input.h>
#include <vcpkg/portfileprovider.h>
#include <vcpkg/vcpkgcmdarguments.h>

namespace vcpkg::Commands::BuildExternal
{
    const CommandStructure COMMAND_STRUCTURE = {
        create_example_string(R"(build-external zlib2 C:\path\to\dir\with\controlfile\)"),
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

        BinaryCache binary_cache{args, paths};

        const FullPackageSpec spec = check_and_get_full_package_spec(
            std::string(options.command_arguments[0]), default_triplet, COMMAND_STRUCTURE.example_text, paths);

        auto overlays = paths.overlay_ports;
        overlays.insert(overlays.begin(), options.command_arguments[1]);

        auto& fs = paths.get_filesystem();
        auto registry_set = paths.make_registry_set();
        PathsPortFileProvider provider(fs, *registry_set, make_overlay_provider(fs, paths.original_cwd, overlays));
        Build::perform_and_exit_ex(args, spec, host_triplet, provider, binary_cache, null_build_logs_recorder(), paths);
    }

    void BuildExternalCommand::perform_and_exit(const VcpkgCmdArguments& args,
                                                const VcpkgPaths& paths,
                                                Triplet default_triplet,
                                                Triplet host_triplet) const
    {
        BuildExternal::perform_and_exit(args, paths, default_triplet, host_triplet);
    }
}
