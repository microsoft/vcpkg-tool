#include <vcpkg/fwd/vcpkgpaths.h>

#include <vcpkg/binaryparagraph.h>
#include <vcpkg/commands.interface.h>

namespace vcpkg::Commands::Usage
{
    struct CMakeUsageInfo
    {
        std::string name;
        Triplet triplet;
        Optional<std::string> usage_file;                                  // has a usage file
        Optional<std::string> header_to_find;                              // header only
        std::map<std::string, std::vector<std::string>> cmake_targets_map; // has cmake targets
    };

    CMakeUsageInfo get_cmake_usage(const BinaryParagraph& bpgh, const VcpkgPaths& paths);
    Json::Value to_json(const CMakeUsageInfo& cmui);
    std::string to_string(const CMakeUsageInfo& cmui);

    void perform_and_exit(const VcpkgCmdArguments& args,
                          const VcpkgPaths& paths,
                          Triplet default_triplet,
                          Triplet host_triplet);

    struct UsageCommand : TripletCommand
    {
        void perform_and_exit(const VcpkgCmdArguments& args,
                              const VcpkgPaths& paths,
                              Triplet default_triplet,
                              Triplet host_triplet) const override
        {
            Usage::perform_and_exit(args, paths, default_triplet, host_triplet);
        }
    };
}
