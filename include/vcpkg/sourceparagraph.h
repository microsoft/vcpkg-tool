#pragma once

#include <vcpkg/base/fwd/json.h>

#include <vcpkg/fwd/configuration.h>
#include <vcpkg/fwd/vcpkgcmdarguments.h>

#include <vcpkg/base/expected.h>
#include <vcpkg/base/span.h>
#include <vcpkg/base/system.h>

#include <vcpkg/packagespec.h>
#include <vcpkg/paragraphparser.h>
#include <vcpkg/platform-expression.h>
#include <vcpkg/versiondeserializers.h>
#include <vcpkg/versions.h>

namespace vcpkg
{
    std::vector<FullPackageSpec> filter_dependencies(const std::vector<Dependency>& deps,
                                                     Triplet t,
                                                     Triplet host,
                                                     const std::unordered_map<std::string, std::string>& cmake_vars,
                                                     ImplicitDefault id);

    struct Type
    {
        enum
        {
            UNKNOWN,
            PORT,
            ALIAS,
        } type;

        static std::string to_string(const Type&);
        static Type from_string(const std::string&);
    };

    bool operator==(const Type&, const Type&);
    bool operator!=(const Type&, const Type&);

    /// <summary>
    /// Port metadata of additional feature in a package (part of CONTROL file)
    /// </summary>
    struct FeatureParagraph
    {
        std::string name;
        std::vector<std::string> description;
        std::vector<Dependency> dependencies;
        PlatformExpression::Expr supports_expression;

        Json::Object extra_info;

        friend bool operator==(const FeatureParagraph& lhs, const FeatureParagraph& rhs);
        friend bool operator!=(const FeatureParagraph& lhs, const FeatureParagraph& rhs) { return !(lhs == rhs); }
    };

    /// <summary>
    /// Port metadata of the core feature of a package (part of CONTROL file)
    /// </summary>
    struct SourceParagraph
    {
        std::string name;
        VersionScheme version_scheme = VersionScheme::String;
        std::string raw_version;
        int port_version = 0;
        std::vector<std::string> description;
        std::vector<std::string> summary;
        std::vector<std::string> maintainers;
        std::string homepage;
        std::string documentation;
        std::vector<Dependency> dependencies;
        std::vector<DependencyOverride> overrides;
        std::vector<std::string> default_features;

        // there are two distinct "empty" states here
        // "user did not provide a license" -> nullopt
        // "user provided license = null" -> {""}
        Optional<std::string> license; // SPDX license expression

        Optional<std::string> builtin_baseline;
        Optional<Json::Object> vcpkg_configuration;
        // Currently contacts is only a Json::Object but it will eventually be unified with maintainers
        Json::Object contacts;

        Type type = {Type::PORT};
        PlatformExpression::Expr supports_expression;

        Json::Object extra_info;

        Version to_version() const { return Version{raw_version, port_version}; }

        friend bool operator==(const SourceParagraph& lhs, const SourceParagraph& rhs);
        friend bool operator!=(const SourceParagraph& lhs, const SourceParagraph& rhs) { return !(lhs == rhs); }
    };

    /// <summary>
    /// Full metadata of a package: core and other features.
    /// </summary>
    struct SourceControlFile
    {
        SourceControlFile clone() const;

        static ParseExpected<SourceControlFile> parse_manifest_object(StringView origin,
                                                                      const Json::Object& object,
                                                                      MessageSink& warnings_sink);

        static ParseExpected<SourceControlFile> parse_control_file(StringView origin,
                                                                   std::vector<Paragraph>&& control_paragraphs);

        // Always non-null in non-error cases
        std::unique_ptr<SourceParagraph> core_paragraph;
        std::vector<std::unique_ptr<FeatureParagraph>> feature_paragraphs;
        Json::Object extra_features_info;

        Optional<const FeatureParagraph&> find_feature(StringView featurename) const;
        Optional<const std::vector<Dependency>&> find_dependencies_for_feature(const std::string& featurename) const;
        bool has_qualified_dependencies() const;

        Optional<std::string> check_against_feature_flags(const Path& origin,
                                                          const FeatureFlagSettings& flags,
                                                          bool is_default_builtin_registry = true) const;

        Version to_version() const { return core_paragraph->to_version(); }
        SchemedVersion to_schemed_version() const
        {
            return SchemedVersion{core_paragraph->version_scheme, core_paragraph->to_version()};
        }
        VersionSpec to_version_spec() const { return {core_paragraph->name, core_paragraph->to_version()}; }

        friend bool operator==(const SourceControlFile& lhs, const SourceControlFile& rhs);
        friend bool operator!=(const SourceControlFile& lhs, const SourceControlFile& rhs) { return !(lhs == rhs); }
    };

    Json::Object serialize_manifest(const SourceControlFile& scf);
    Json::Object serialize_debug_manifest(const SourceControlFile& scf);

    ExpectedS<ManifestConfiguration> parse_manifest_configuration(StringView origin, const Json::Object& manifest);

    /// <summary>
    /// Named pair of a SourceControlFile and the location of this file
    /// </summary>
    struct SourceControlFileAndLocation
    {
        Version to_version() const { return source_control_file->to_version(); }

        std::unique_ptr<SourceControlFile> source_control_file;
        Path source_location;
        /// Should model SPDX PackageDownloadLocation. Empty implies NOASSERTION.
        /// See https://spdx.github.io/spdx-spec/package-information/#77-package-download-location-field
        std::string registry_location;
    };

    void print_error_message(Span<const std::unique_ptr<ParseControlErrorInfo>> error_info_list);
    inline void print_error_message(const std::unique_ptr<ParseControlErrorInfo>& error_info_list)
    {
        return print_error_message({&error_info_list, 1});
    }

    std::string parse_spdx_license_expression(StringView sv, ParseMessages& messages);
}
