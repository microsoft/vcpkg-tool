#pragma once

#include <vcpkg/base/fwd/files.h>

#include <vcpkg/fwd/dependencies.h>
#include <vcpkg/fwd/installedpaths.h>
#include <vcpkg/fwd/packagespec.h>
#include <vcpkg/fwd/triplet.h>
#include <vcpkg/fwd/vcpkgcmdarguments.h>
#include <vcpkg/fwd/vcpkgpaths.h>

namespace vcpkg
{
    enum class Purge : bool
    {
        NO = 0,
        YES
    };

    void remove_package(const Filesystem& fs,
                        const InstalledPaths& installed,
                        const PackageSpec& spec,
                        StatusParagraphs& status_db);

    extern const CommandMetadata CommandRemoveMetadata;
    void command_remove_and_exit(const VcpkgCmdArguments& args,
                                 const VcpkgPaths& paths,
                                 Triplet default_triplet,
                                 Triplet host_triplet);
}
