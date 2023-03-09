#pragma once

#include <vcpkg/fwd/installedpaths.h>
#include <vcpkg/fwd/vcpkgpaths.h>

#include <vcpkg/base/chrono.h>
#include <vcpkg/base/optional.h>

#include <vcpkg/binaryparagraph.h>
#include <vcpkg/build.h>
#include <vcpkg/dependencies.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

#include <chrono>
#include <set>
#include <string>
#include <vector>

namespace vcpkg
{
    enum class KeepGoing
    {
        NO = 0,
        YES
    };

    struct SpecSummary
    {
        explicit SpecSummary(const InstallPlanAction& action);
        explicit SpecSummary(const RemovePlanAction& action);

        const BinaryParagraph* get_binary_paragraph() const;
        const PackageSpec& get_spec() const { return m_spec; }
        bool is_user_requested_install() const;
        Optional<ExtendedBuildResult> build_result;
        vcpkg::ElapsedTime timing;
        std::chrono::system_clock::time_point start_time;

    private:
        const InstallPlanAction* m_install_action;
        PackageSpec m_spec;
    };

    struct InstallSummary
    {
        std::vector<SpecSummary> results;

        void print() const;
        void print_failed() const;
        std::string xunit_results() const;
        bool failed() const;
    };

    struct InstallDir
    {
        static InstallDir from_destination_root(const InstalledPaths& ip, Triplet t, const BinaryParagraph& pgh);

    private:
        Path m_destination;
        Path m_listfile;

    public:
        const Path& destination() const;
        const Path& listfile() const;
    };

    ExtendedBuildResult perform_install_plan_action(const VcpkgCmdArguments& args,
                                                    const VcpkgPaths& paths,
                                                    InstallPlanAction& action,
                                                    StatusParagraphs& status_db,
                                                    const CMakeVars::CMakeVarProvider& var_provider);

    enum class InstallResult
    {
        FILE_CONFLICTS,
        SUCCESS,
    };

    void install_package_and_write_listfile(Filesystem& fs, const Path& source_dir, const InstallDir& destination_dir);

    void install_files_and_write_listfile(Filesystem& fs,
                                          const Path& source_dir,
                                          const std::vector<Path>& files,
                                          const InstallDir& destination_dir);

    InstallResult install_package(const VcpkgPaths& paths,
                                  const BinaryControlFile& binary_paragraph,
                                  StatusParagraphs* status_db);

    struct CMakeUsageInfo
    {
        std::string message;
        bool usage_file = false;
        bool header_only = false;
        std::map<std::string, std::vector<std::string>> cmake_targets_map;
    };

    std::vector<std::string> get_cmake_add_library_names(StringView cmake_file);
    std::string get_cmake_find_package_name(StringView dirname, StringView filename);
    CMakeUsageInfo get_cmake_usage(const Filesystem& fs, const InstalledPaths& installed, const BinaryParagraph& bpgh);

    namespace Install
    {
        extern const CommandStructure COMMAND_STRUCTURE;

        void print_usage_information(const BinaryParagraph& bpgh,
                                     std::set<std::string>& printed_usages,
                                     const Filesystem& fs,
                                     const InstalledPaths& installed);

        InstallSummary perform(const VcpkgCmdArguments& args,
                               ActionPlan& action_plan,
                               const KeepGoing keep_going,
                               const VcpkgPaths& paths,
                               StatusParagraphs& status_db,
                               BinaryCache& binary_cache,
                               const IBuildLogsRecorder& build_logs_recorder,
                               const CMakeVars::CMakeVarProvider& var_provider);

        void perform_and_exit(const VcpkgCmdArguments& args,
                              const VcpkgPaths& paths,
                              Triplet default_triplet,
                              Triplet host_triplet);
    } // namespace vcpkg::Install

    struct InstallCommand : Commands::TripletCommand
    {
        virtual void perform_and_exit(const VcpkgCmdArguments& args,
                                      const VcpkgPaths& paths,
                                      Triplet default_triplet,
                                      Triplet host_triplet) const override;
    };

    void track_install_plan(ActionPlan& plan);
}
