#pragma once

#include <vcpkg/fwd/binaryparagraph.h>
#include <vcpkg/fwd/commands.install.h>
#include <vcpkg/fwd/installedpaths.h>
#include <vcpkg/fwd/vcpkgcmdarguments.h>
#include <vcpkg/fwd/vcpkgpaths.h>

#include <vcpkg/base/chrono.h>
#include <vcpkg/base/optional.h>

#include <vcpkg/commands.build.h>
#include <vcpkg/dependencies.h>
#include <vcpkg/packagespec.h>

#include <chrono>
#include <set>
#include <string>
#include <vector>

namespace vcpkg
{
    struct SpecSummary
    {
        explicit SpecSummary(const InstallPlanAction& action);
        explicit SpecSummary(const RemovePlanAction& action);

        const BinaryParagraph* get_binary_paragraph() const;
        const PackageSpec& get_spec() const { return m_spec; }
        Optional<const std::string&> get_abi() const
        {
            return m_install_action ? m_install_action->package_abi() : nullopt;
        }
        bool is_user_requested_install() const;
        Optional<ExtendedBuildResult> build_result;
        vcpkg::ElapsedTime timing;
        std::chrono::system_clock::time_point start_time;
        Optional<const InstallPlanAction&> get_install_plan_action() const
        {
            return m_install_action ? Optional<const InstallPlanAction&>(*m_install_action) : nullopt;
        }

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

    void install_package_and_write_listfile(const Filesystem& fs,
                                            const Path& source_dir,
                                            const InstallDir& destination_dir);

    void install_files_and_write_listfile(const Filesystem& fs,
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
    CMakeUsageInfo get_cmake_usage(const ReadOnlyFilesystem& fs,
                                   const InstalledPaths& installed,
                                   const BinaryParagraph& bpgh);

    extern const CommandMetadata CommandInstallMetadata;

    void install_print_usage_information(const BinaryParagraph& bpgh,
                                         std::set<std::string>& printed_usages,
                                         const ReadOnlyFilesystem& fs,
                                         const InstalledPaths& installed);

    void install_preclear_packages(const VcpkgPaths& paths, const ActionPlan& action_plan);

    InstallSummary install_execute_plan(const VcpkgCmdArguments& args,
                                        const ActionPlan& action_plan,
                                        const KeepGoing keep_going,
                                        const VcpkgPaths& paths,
                                        StatusParagraphs& status_db,
                                        BinaryCache& binary_cache,
                                        const IBuildLogsRecorder& build_logs_recorder,
                                        bool include_manifest_in_github_issue = false);

    void command_install_and_exit(const VcpkgCmdArguments& args,
                                  const VcpkgPaths& paths,
                                  Triplet default_triplet,
                                  Triplet host_triplet);

    void track_install_plan(const ActionPlan& plan);
}
