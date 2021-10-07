#pragma once

#include <vcpkg/base/chrono.h>

#include <vcpkg/binaryparagraph.h>
#include <vcpkg/build.h>
#include <vcpkg/dependencies.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

#include <set>
#include <string>
#include <vector>

namespace vcpkg::Install
{
    enum class KeepGoing
    {
        NO = 0,
        YES
    };

    inline KeepGoing to_keep_going(const bool value) { return value ? KeepGoing::YES : KeepGoing::NO; }

    struct SpecSummary
    {
        SpecSummary(const PackageSpec& spec, const Dependencies::InstallPlanAction* action);

        const BinaryParagraph* get_binary_paragraph() const;

        PackageSpec spec;
        Build::ExtendedBuildResult build_result;
        vcpkg::ElapsedTime timing;

        const Dependencies::InstallPlanAction* action;
    };

    struct InstallSummary
    {
        std::vector<SpecSummary> results;

        void print() const;
        std::string xunit_results() const;
    };

    struct InstallDir
    {
        static InstallDir from_destination_root(const Path& destination_root,
                                                const std::string& destination_subdirectory,
                                                const Path& listfile);

    private:
        Path m_destination;
        std::string m_destination_subdirectory;
        Path m_listfile;

    public:
        const Path& destination() const;
        const std::string& destination_subdirectory() const;
        const Path& listfile() const;
    };

    Build::ExtendedBuildResult perform_install_plan_action(const VcpkgCmdArguments& args,
                                                           const VcpkgPaths& paths,
                                                           Dependencies::InstallPlanAction& action,
                                                           StatusParagraphs& status_db,
                                                           const CMakeVars::CMakeVarProvider& var_provider);

    enum class InstallResult
    {
        FILE_CONFLICTS,
        SUCCESS,
    };

    std::vector<std::string> get_all_port_names(const VcpkgPaths& paths);

    void install_package_and_write_listfile(const VcpkgPaths& paths, const PackageSpec& spec, const InstallDir& dirs);

    void install_files_and_write_listfile(Filesystem& fs,
                                          const Path& source_dir,
                                          const std::vector<Path>& files,
                                          const InstallDir& destination_dir);

    InstallResult install_package(const VcpkgPaths& paths,
                                  const BinaryControlFile& binary_paragraph,
                                  StatusParagraphs* status_db);

    InstallSummary perform(const VcpkgCmdArguments& args,
                           Dependencies::ActionPlan& action_plan,
                           const KeepGoing keep_going,
                           const VcpkgPaths& paths,
                           StatusParagraphs& status_db,
                           BinaryCache& binary_cache,
                           const Build::IBuildLogsRecorder& build_logs_recorder,
                           const CMakeVars::CMakeVarProvider& var_provider);

    struct CMakeUsageInfo
    {
        std::string message;
        bool usage_file = false;
        Optional<bool> header_only;
        std::map<std::string, std::vector<std::string>> cmake_targets_map;
    };

    CMakeUsageInfo get_cmake_usage(const BinaryParagraph& bpgh, const VcpkgPaths& paths);
    void print_usage_information(const BinaryParagraph& bpgh,
                                 std::set<std::string>& printed_usages,
                                 const VcpkgPaths& paths);

    extern const CommandStructure COMMAND_STRUCTURE;

    void perform_and_exit(const VcpkgCmdArguments& args,
                          const VcpkgPaths& paths,
                          Triplet default_triplet,
                          Triplet host_triplet);

    struct InstallCommand : Commands::TripletCommand
    {
        virtual void perform_and_exit(const VcpkgCmdArguments& args,
                                      const VcpkgPaths& paths,
                                      Triplet default_triplet,
                                      Triplet host_triplet) const override;
    };

    void track_install_plan(Dependencies::ActionPlan& plan);
}
