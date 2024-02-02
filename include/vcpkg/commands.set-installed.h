#pragma once

#include <vcpkg/base/fwd/json.h>
#include <vcpkg/base/fwd/optional.h>

#include <vcpkg/fwd/build.h>
#include <vcpkg/fwd/cmakevars.h>
#include <vcpkg/fwd/commands.install.h>
#include <vcpkg/fwd/dependencies.h>
#include <vcpkg/fwd/statusparagraphs.h>
#include <vcpkg/fwd/triplet.h>
#include <vcpkg/fwd/vcpkgcmdarguments.h>
#include <vcpkg/fwd/vcpkgpaths.h>

#include <set>

namespace vcpkg
{
    enum class DryRun : bool
    {
        No,
        Yes
    };

    extern const CommandMetadata CommandSetInstalledMetadata;

    /**
     * @brief adjust_action_plan_to_status_db creates an action plan that installs only the requested ports.
     * All ABIs must be computed first.
     * @param action_plan An action plan that was created with an empty status db
     * @param status_db The status db of the installed folder
     * @returns A set of PackageSpec's that are already installed
     */
    std::set<PackageSpec> adjust_action_plan_to_status_db(ActionPlan& action_plan, const StatusParagraphs& status_db);

    void command_set_installed_and_exit_ex(const VcpkgCmdArguments& args,
                                           const VcpkgPaths& paths,
                                           const BuildPackageOptions& build_options,
                                           const CMakeVars::CMakeVarProvider& cmake_vars,
                                           ActionPlan action_plan,
                                           DryRun dry_run,
                                           const Optional<Path>& pkgsconfig_path,
                                           Triplet host_triplet,
                                           const KeepGoing keep_going,
                                           const bool only_downloads,
                                           const PrintUsage print_cmake_usage,
                                           bool include_manifest_in_github_issue);
    void command_set_installed_and_exit(const VcpkgCmdArguments& args,
                                        const VcpkgPaths& paths,
                                        Triplet default_triplet,
                                        Triplet host_triplet);

    Optional<Json::Object> create_dependency_graph_snapshot(const VcpkgCmdArguments& args,
                                                            const ActionPlan& action_plan);
}
