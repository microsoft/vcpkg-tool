#pragma once

#include <vcpkg/fwd/dependencies.h>

#include <vcpkg/commands.interface.h>

namespace vcpkg::Remove
{
    enum class Purge : bool
    {
        NO = 0,
        YES
    };

    void perform_remove_plan_action(const VcpkgPaths& paths,
                                    const RemovePlanAction& action,
                                    const Purge purge,
                                    StatusParagraphs* status_db);

    extern const CommandStructure COMMAND_STRUCTURE;

    struct RemoveCommand : Commands::TripletCommand
    {
        virtual void perform_and_exit(const VcpkgCmdArguments& args,
                                      const VcpkgPaths& paths,
                                      Triplet default_triplet,
                                      Triplet host_triplet) const override;
    };
}
