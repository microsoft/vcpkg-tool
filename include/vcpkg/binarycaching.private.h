#pragma once

#include <vcpkg/fwd/packagespec.h>
#include <vcpkg/fwd/vcpkgpaths.h>

#include <vcpkg/base/strings.h>

#include <vcpkg/dependencies.h>

namespace vcpkg
{
    std::string reformat_version(const std::string& version, const std::string& abi_tag);

    struct NugetReference
    {
        NugetReference(std::string id, std::string version) : id(std::move(id)), version(std::move(version)) { }

        std::string id;
        std::string version;

        std::string nupkg_filename() const { return Strings::concat(id, '.', version, ".nupkg"); }
    };

    inline NugetReference make_nugetref(const PackageSpec& spec,
                                        const std::string& raw_version,
                                        const std::string& abi_tag,
                                        const std::string& prefix)
    {
        return {Strings::concat(prefix, spec.dir()), reformat_version(raw_version, abi_tag)};
    }
    inline NugetReference make_nugetref(const Dependencies::InstallPlanAction& action, const std::string& prefix)
    {
        return make_nugetref(action.spec,
                             action.source_control_file_location.value_or_exit(VCPKG_LINE_INFO)
                                 .source_control_file->core_paragraph->version,
                             action.abi_info.value_or_exit(VCPKG_LINE_INFO).package_abi,
                             prefix);
    }

    namespace details
    {
        struct NuGetRepoInfo
        {
            std::string repo;
            std::string branch;
            std::string commit;
        };

        NuGetRepoInfo get_nuget_repo_info_from_env();
    }

    std::string generate_nuspec(const VcpkgPaths& paths,
                                const Dependencies::InstallPlanAction& action,
                                const NugetReference& ref,
                                details::NuGetRepoInfo rinfo = details::get_nuget_repo_info_from_env());
}
