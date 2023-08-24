#pragma once

#include <vcpkg/base/fwd/downloads.h>
#include <vcpkg/base/fwd/files.h>

#include <vcpkg/fwd/vcpkgpaths.h>

#include <vcpkg/base/expected.h>
#include <vcpkg/base/span.h>
#include <vcpkg/base/stringview.h>
#include <vcpkg/base/util.h>

#include <string>

namespace vcpkg
{
    ExpectedL<Path> download_vcpkg_standalone_bundle(const DownloadManager& download_manager,
                                                     const Filesystem& fs,
                                                     const Path& download_root);

    int run_configure_environment_command(const VcpkgPaths& paths, View<std::string> args);

    template<size_t N>
    bool more_than_one_mapped(const StringLiteral* const (&candidates)[N],
                              const std::set<std::string, std::less<>>& switches)
    {
        bool seen = false;
        for (auto&& candidate : candidates)
        {
            if (Util::Sets::contains(switches, *candidate))
            {
                if (seen)
                {
                    return true;
                }

                seen = true;
            }
        }

        return false;
    }
}
