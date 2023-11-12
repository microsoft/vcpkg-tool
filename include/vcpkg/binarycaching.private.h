#pragma once

#include <vcpkg/base/fwd/files.h>

#include <vcpkg/fwd/dependencies.h>
#include <vcpkg/fwd/packagespec.h>
#include <vcpkg/fwd/vcpkgpaths.h>

#include <vcpkg/base/strings.h>

#include <vcpkg/binarycaching.h>

namespace vcpkg
{
    // Turns:
    // - <XXXX>-<YY>-<ZZ><whatever> -> <X>.<Y>.<Z>-vcpkg<abitag>
    // - v?<X> -> <X>.0.0-vcpkg<abitag>
    //   - this avoids turning 20-01-01 into 20.0.0-vcpkg<abitag>
    // - v?<X>.<Y><whatever> -> <X>.<Y>.0-vcpkg<abitag>
    // - v?<X>.<Y>.<Z><whatever> -> <X>.<Y>.<Z>-vcpkg<abitag>
    // - anything else -> 0.0.0-vcpkg<abitag>
    std::string format_version_for_nugetref(StringView version_text, StringView abi_tag);

    struct NugetReference
    {
        NugetReference(std::string id, std::string version) : id(std::move(id)), version(std::move(version)) { }

        std::string id;
        std::string version;

        std::string nupkg_filename() const { return Strings::concat(id, '.', version, ".nupkg"); }
    };

    NugetReference make_nugetref(const InstallPlanAction& action, StringView prefix);

    std::string generate_nuspec(const Path& package_dir,
                                const InstallPlanAction& action,
                                StringView id_prefix,
                                const NuGetRepoInfo& repo_info);
}
