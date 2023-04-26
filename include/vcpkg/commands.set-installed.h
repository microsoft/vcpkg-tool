#pragma once

#include <vcpkg/base/fwd/optional.h>

#include <vcpkg/fwd/binarycaching.h>
#include <vcpkg/fwd/build.h>
#include <vcpkg/fwd/cmakevars.h>
#include <vcpkg/fwd/commands.install.h>
#include <vcpkg/fwd/dependencies.h>
#include <vcpkg/fwd/portfileprovider.h>
#include <vcpkg/fwd/triplet.h>
#include <vcpkg/fwd/vcpkgcmdarguments.h>
#include <vcpkg/fwd/vcpkgpaths.h>

namespace vcpkg::Commands::SetInstalled
{
    enum class DryRun : bool
    {
        No,
        Yes,
    };

    extern const CommandStructure COMMAND_STRUCTURE;

    /**
     * @brief adjust_action_plan_to_status_db creates an action plan that installs only the requested ports
     * @param action_plan An action plan that was created with an empty status db
     * @param status_db The status db of the installed folder
     */
    void adjust_action_plan_to_status_db(ActionPlan& action_plan, const StatusParagraphs& status_db);

    void perform_and_exit_ex(const VcpkgCmdArguments& args,
                             const VcpkgPaths& paths,
                             const PathsPortFileProvider& provider,
                             BinaryCache& binary_cache,
                             const CMakeVars::CMakeVarProvider& cmake_vars,
                             ActionPlan action_plan,
                             DryRun dry_run,
                             const Optional<Path>& pkgsconfig_path,
                             Triplet host_triplet,
                             const KeepGoing keep_going,
                             const bool only_downloads,
                             const PrintUsage print_cmake_usage);
    void perform_and_exit(const VcpkgCmdArguments& args,
                          const VcpkgPaths& paths,
                          Triplet default_triplet,
                          Triplet host_triplet);
}
