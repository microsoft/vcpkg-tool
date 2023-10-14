#pragma once

#include <vcpkg/fwd/abi.h>
#include <vcpkg/fwd/dependencies.h>

#include <vcpkg/base/json.h>
#include <vcpkg/base/optional.h>
#include <vcpkg/base/path.h>
#include <vcpkg/base/stringview.h>

#include <vcpkg/cmakevars.h>
#include <vcpkg/commands.build.h>
#include <vcpkg/statusparagraphs.h>
#include <vcpkg/vcpkgpaths.h>


#include <memory>
#include <string>
#include <vector>

namespace vcpkg
{
    struct AbiEntry
    {
        std::string key;
        std::string value;

        AbiEntry() = default;
        AbiEntry(std::string key, std::string value) : key(std::move(key)), value(std::move(value)) { }

        bool operator<(const AbiEntry& other) const noexcept
        {
            return key < other.key || (key == other.key && value < other.value);
        }
    };

    struct AbiInfo
    {
        // These should always be known if an AbiInfo exists
        std::unique_ptr<PreBuildInfo> pre_build_info;
        Optional<const Toolset&> toolset;
        // These might not be known if compiler tracking is turned off or the port is --editable
        Optional<const CompilerInfo&> compiler_info;
        Optional<const std::string&> triplet_abi;
        std::string package_abi;
        Optional<Path> abi_tag_file;
    };

    void compute_all_abis(const VcpkgPaths& paths,
                          ActionPlan& action_plan,
                          const CMakeVars::CMakeVarProvider& var_provider,
                          const StatusParagraphs& status_db);
} // namespace vcpkg
