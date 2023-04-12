#pragma once

#include <vcpkg/packagespec.h>
#include <vcpkg/paragraphparser.h>
#include <vcpkg/sourceparagraph.h>

namespace vcpkg
{
    /// <summary>
    /// Built package metadata
    /// </summary>
    struct BinaryParagraph
    {
        BinaryParagraph();
        explicit BinaryParagraph(Paragraph fields);
        BinaryParagraph(const SourceParagraph& spgh,
                        Triplet triplet,
                        const std::string& abi_tag,
                        std::vector<PackageSpec> deps);
        BinaryParagraph(const PackageSpec& spec, const FeatureParagraph& fpgh, std::vector<PackageSpec> deps);

        void canonicalize();

        std::string displayname() const;

        std::string fullstem() const;

        std::string dir() const;

        bool is_feature() const { return !feature.empty(); }

        Version get_version() const { return {version, port_version}; }

        PackageSpec spec;
        std::string version;
        int port_version = 0;
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
    std::string format_binary_paragraph(BinaryParagraph paragraph);
}
