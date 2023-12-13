#pragma once

#include <vcpkg/base/fwd/json.h>

#include <vcpkg/fwd/installedpaths.h>
#include <vcpkg/fwd/statusparagraph.h>

#include <vcpkg/binaryparagraph.h>

#include <map>
#include <string>
#include <vector>

namespace vcpkg
{
    /// <summary>
    /// Installed package metadata
    /// </summary>
    struct StatusParagraph
    {
        StatusParagraph() noexcept;
        StatusParagraph(StringView origin, Paragraph&& fields);

        bool is_installed() const { return want == Want::INSTALL && state == InstallState::INSTALLED; }

        BinaryParagraph package;
        Want want;
        InstallState state;
    };

    void serialize(const StatusParagraph& pgh, std::string& out_str);

    std::string to_string(InstallState f);

    std::string to_string(Want f);

    struct InstalledPackageView
    {
        InstalledPackageView() noexcept : core(nullptr) { }

        InstalledPackageView(const StatusParagraph* c, std::vector<const StatusParagraph*>&& fs)
            : core(c), features(std::move(fs))
        {
        }

        const PackageSpec& spec() const { return core->package.spec; }
        std::vector<PackageSpec> dependencies() const;
        std::map<std::string, std::vector<FeatureSpec>> feature_dependencies() const;
        InternalFeatureSet feature_list() const;
        const Version& version() const;

        std::vector<StatusParagraph> all_status_paragraphs() const;

        const StatusParagraph* core;
        std::vector<const StatusParagraph*> features;
    };

    Json::Value serialize_ipv(const InstalledPackageView& ipv,
                              const InstalledPaths& installed,
                              const ReadOnlyFilesystem& fs);
}
