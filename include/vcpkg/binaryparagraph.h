#pragma once

#include <vcpkg/base/fwd/stringview.h>

#include <vcpkg/packagespec.h>
#include <vcpkg/paragraphparser.h>
#include <vcpkg/sourceparagraph.h>

namespace vcpkg
{
    // metadata for a package in the 'packages' tree
    struct BinaryParagraph
    {
        BinaryParagraph() = default;
        BinaryParagraph(StringView origin, Paragraph&& fields);
        BinaryParagraph(const SourceParagraph& spgh,
                        const std::vector<std::string>& default_features,
                        Triplet triplet,
                        const std::string& abi_tag,
                        std::vector<PackageSpec> deps);
        BinaryParagraph(const PackageSpec& spec, const FeatureParagraph& fpgh, std::vector<PackageSpec> deps);

        void canonicalize();

        std::string display_name() const;

        std::string fullstem() const;

        bool is_feature() const { return !feature.empty(); }

        PackageSpec spec;
        Version version;
        std::vector<std::string> description;
        std::vector<std::string> maintainers;
        std::string feature;
        std::vector<std::string> default_features;
        std::vector<PackageSpec> dependencies;
        std::string abi;
    };

    bool operator==(const BinaryParagraph&, const BinaryParagraph&);
    bool operator!=(const BinaryParagraph&, const BinaryParagraph&);

    struct BinaryControlFile
    {
        BinaryParagraph core_paragraph;
        std::vector<BinaryParagraph> features;
    };

    void serialize(const BinaryParagraph& pgh, std::string& out_str);
    std::string format_binary_paragraph(const BinaryParagraph& paragraph);
}
