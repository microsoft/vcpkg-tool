#pragma once

#include <vcpkg/base/fwd/json.h>

#include <vcpkg/fwd/installedpaths.h>
#include <vcpkg/fwd/statusparagraph.h>

#include <vcpkg/base/fmt.h>

#include <vcpkg/binaryparagraph.h>

#include <map>
#include <string>
#include <vector>

namespace vcpkg
{
    struct StatusLine
    {
        Want want = Want::ERROR_STATE;
        InstallState state = InstallState::ERROR_STATE;

        bool is_installed() const noexcept { return want == Want::INSTALL && state == InstallState::INSTALLED; }
        void to_string(std::string& out) const;
        std::string to_string() const;

        friend bool operator==(const StatusLine& lhs, const StatusLine& rhs)
        {
            return lhs.want == rhs.want && lhs.state == rhs.state;
        }

        friend bool operator!=(const StatusLine& lhs, const StatusLine& rhs) { return !(lhs == rhs); }
    };

    ExpectedL<StatusLine> parse_status_line(StringView text, Optional<StringView> origin, TextRowCol init_rowcol);

    // metadata for a package's representation in the 'installed' tree
    struct StatusParagraph
    {
        StatusParagraph() = default;
        StatusParagraph(StringView origin, Paragraph&& fields);

        bool is_installed() const noexcept { return status.is_installed(); }

        BinaryParagraph package;
        StatusLine status;
    };

    void serialize(const StatusParagraph& pgh, std::string& out_str);

    StringLiteral to_string_literal(InstallState f);
    StringLiteral to_string_literal(Want f);

    struct InstalledPackageView
    {
        InstalledPackageView() = default;
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

        const StatusParagraph* core = nullptr;
        std::vector<const StatusParagraph*> features;
    };

    Json::Value serialize_ipv(const InstalledPackageView& ipv,
                              const InstalledPaths& installed,
                              const ReadOnlyFilesystem& fs);
}

VCPKG_FORMAT_WITH_TO_STRING_LITERAL_NONMEMBER(vcpkg::InstallState);
VCPKG_FORMAT_WITH_TO_STRING_LITERAL_NONMEMBER(vcpkg::Want);
VCPKG_FORMAT_WITH_TO_STRING(vcpkg::StatusLine);
