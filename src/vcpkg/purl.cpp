#include <vcpkg/purl.h>

#include <vcpkg/base/fmt.h>
#include <vcpkg/base/strings.h>

#include <vcpkg/dependencies.h>
#include <vcpkg/sourceparagraph.h>

#include <vector>

using namespace vcpkg;

std::string vcpkg::make_vcpkg_purl(const InstallPlanAction& action)
{
    const auto& version = action.version;
    const auto& source = action.source_control_file_and_location();

    std::string purl = fmt::format("pkg:vcpkg/{}", Strings::percent_encode(action.spec.name()));
    if (!version.text.empty())
    {
        purl.push_back('@');
        purl += Strings::percent_encode(version.text);
    }

    // PURL qualifiers, emitted in canonical (alphabetical) key order.
    std::vector<std::string> qualifiers;
    if (!version.text.empty() && version.port_version != 0)
    {
        qualifiers.push_back(fmt::format("port_version={}", version.port_version));
    }

    if (!source.spdx_repository_url.empty())
    {
        qualifiers.push_back("repository_url=" + Strings::percent_encode(source.spdx_repository_url));
    }

    qualifiers.push_back("triplet=" + Strings::percent_encode(action.spec.triplet().canonical_name()));

    // spdx_location is the SPDX PackageDownloadLocation: a VCS URL that pins the port's
    // git-tree, which identifies the exact port recipe.
    if (!source.spdx_location.empty())
    {
        qualifiers.push_back("vcs_url=" + Strings::percent_encode(source.spdx_location));
    }

    purl.push_back('?');
    purl += Strings::join("&", qualifiers);
    return purl;
}
