#pragma once

#include <vcpkg/fwd/packagespec.h>
#include <vcpkg/fwd/vcpkgpaths.h>

#include <vcpkg/base/strings.h>

#include <vcpkg/dependencies.h>

namespace vcpkg
{
    // Turns:
    // - <XXXX>-<YY>-<ZZ><whatever> -> <X>.<Y>.<Z>-vcpkg<abitag>
    // - v?<X> -> <X>.0.0-vcpkg<abitag>
    //   - this avoids turning 20-01-01 into 20.0.0-vcpkg<abitag>
    // - v?<X>.<Y><whatever> -> <X>.<Y>.0-vcpkg<abitag>
    // - v?<X>.<Y>.<Z><whatever> -> <X>.<Y>.<Z>-vcpkg<abitag>
    // - anything else -> 0.0.0-vcpkg<abitag>
    std::string format_version_for_nugetref(StringView version, StringView abi_tag);

    struct NugetReference
    {
        NugetReference(std::string id, std::string version) : id(std::move(id)), version(std::move(version)) { }

        std::string id;
        std::string version;

        std::string nupkg_filename() const { return Strings::concat(id, '.', version, ".nupkg"); }
    };

    inline NugetReference make_nugetref(const PackageSpec& spec,
                                        StringView raw_version,
                                        StringView abi_tag,
                                        const std::string& prefix)
    {
        return {Strings::concat(prefix, spec.dir()), format_version_for_nugetref(raw_version, abi_tag)};
    }
    inline NugetReference make_nugetref(const InstallPlanAction& action, const std::string& prefix)
    {
        return make_nugetref(action.spec,
                             action.source_control_file_and_location.value_or_exit(VCPKG_LINE_INFO)
                                 .source_control_file->core_paragraph->raw_version,
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

    std::string generate_nuspec(const Path& package_dir,
                                const InstallPlanAction& action,
                                const NugetReference& ref,
                                const details::NuGetRepoInfo& rinfo = details::get_nuget_repo_info_from_env());
}
