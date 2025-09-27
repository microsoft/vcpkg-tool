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

    struct LicenseReport
    {
        bool any_unknown_licenses = false;
        std::set<std::string> named_licenses;
        void print_license_report(const msg::MessageT<>& named_license_heading) const;
    };

    struct InstallSummary
    {
        std::vector<SpecSummary> results;
        ElapsedTime elapsed;
        LicenseReport license_report;
        bool failed = false;

        LocalizedString format_results() const;
        void print_failed() const;
        void print_complete_message() const;
    };

    // First, writes triplet_canonical_name / (including the trailing slash) to listfile. Then:
    // For each directory in source_dir / proximate_files
    //  * create directory destination_installed / triplet_canonical_name / proximate_file
    //  * write a line in listfile triplet_canonical_name / proximate_file /  (note the trailing slash)
    // For each regular file in source_dir / proximate_files
    //  * copy source_dir / proximate_file -> destination_installed / triplet_canonical_name / proximate_file
    //  * write a line in listfile triplet_canonical_name / proximate_file
    // For each symlink or junction in source_dir / proximate_files:
    //  * if hydrate == SymlinkHydrate::yes, resolve symlinks and follow the rules above, otherwise,
    //    * copy the symlink or junction source_dir / proximate_file
    //       -> destination_installed / triplet_canonical_name / proximate_file
    //    * write a line in listfile triplet_canonical_name / proximate_file
    //      (note *no* trailing slash, even for directory symlinks)
    void install_files_and_write_listfile(const Filesystem& fs,
                                          const Path& source_dir,
                                          const std::vector<std::string>& proximate_files,
                                          const Path& destination_installed,
                                          StringView triplet_canonical_name,
                                          const Path& listfile,
                                          const SymlinkHydrate hydrate);

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

    void install_preclear_plan_packages(const VcpkgPaths& paths, const ActionPlan& action_plan);
    void install_clear_installed_packages(const VcpkgPaths& paths, View<InstallPlanAction> install_actions);

    InstallSummary install_execute_plan(const VcpkgCmdArguments& args,
                                        const VcpkgPaths& paths,
                                        Triplet host_triplet,
                                        const BuildPackageOptions& build_options,
                                        const ActionPlan& action_plan,
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
