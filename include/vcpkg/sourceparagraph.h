#pragma once

#include <vcpkg/base/fwd/expected.h>
#include <vcpkg/base/fwd/span.h>

#include <vcpkg/fwd/configuration.h>
#include <vcpkg/fwd/packagespec.h>
#include <vcpkg/fwd/vcpkgcmdarguments.h>

#include <vcpkg/base/json.h>
#include <vcpkg/base/path.h>
#include <vcpkg/base/stringview.h>

#include <vcpkg/paragraphparser.h>
#include <vcpkg/platform-expression.h>
#include <vcpkg/versions.h>

namespace vcpkg
{
    struct ManifestAndPath
    {
        Json::Object manifest;
        Path path;
    };

    struct DependencyConstraint
    {
        VersionConstraintKind type = VersionConstraintKind::None;
        Version version;

        friend bool operator==(const DependencyConstraint& lhs, const DependencyConstraint& rhs);
        friend bool operator!=(const DependencyConstraint& lhs, const DependencyConstraint& rhs)
        {
            return !(lhs == rhs);
        }

        Optional<Version> try_get_minimum_version() const;
    };

    struct DependencyRequestedFeature
    {
        std::string name;
        PlatformExpression::Expr platform;

        friend bool operator==(const DependencyRequestedFeature& lhs, const DependencyRequestedFeature& rhs);
        friend bool operator!=(const DependencyRequestedFeature& lhs, const DependencyRequestedFeature& rhs);
    };

    struct Dependency
    {
        std::string name;
        // a list of "real" features without "core" or "default". Use member default_features instead.
        std::vector<DependencyRequestedFeature> features;
        PlatformExpression::Expr platform;
        DependencyConstraint constraint;
        bool host = false;

        bool default_features = true;
        bool has_platform_expressions() const;

        Json::Object extra_info;

        /// @param id adds "default" if `default_features` is false.
        FullPackageSpec to_full_spec(View<std::string> features, Triplet target, Triplet host) const;

        friend bool operator==(const Dependency& lhs, const Dependency& rhs);
        friend bool operator!=(const Dependency& lhs, const Dependency& rhs) { return !(lhs == rhs); }
    };

    struct DependencyOverride
    {
        std::string name;
        Version version;

        Json::Object extra_info;

        friend bool operator==(const DependencyOverride& lhs, const DependencyOverride& rhs);
        friend bool operator!=(const DependencyOverride& lhs, const DependencyOverride& rhs) { return !(lhs == rhs); }
    };

    void serialize_dependency_override(Json::Array& arr, const DependencyOverride& dep);

    std::vector<FullPackageSpec> filter_dependencies(const std::vector<Dependency>& deps,
                                                     Triplet t,
                                                     Triplet host,
                                                     const std::unordered_map<std::string, std::string>& cmake_vars);

    struct NullTag
    {
    };

    enum class SpdxLicenseDeclarationKind
    {
        NotPresent,
        Null,
        String
    };

    struct SpdxApplicableLicenseExpression
    {
        std::string license_text;   // the expression text
        bool needs_and_parenthesis; // if true, when combined with AND, extra ()s need to be added

        std::string to_string() const;
        void to_string(std::string& target) const;

        friend bool operator==(const SpdxApplicableLicenseExpression& lhs,
                               const SpdxApplicableLicenseExpression& rhs) noexcept;
        friend bool operator!=(const SpdxApplicableLicenseExpression& lhs,
                               const SpdxApplicableLicenseExpression& rhs) noexcept;
        friend bool operator<(const SpdxApplicableLicenseExpression& lhs,
                              const SpdxApplicableLicenseExpression& rhs) noexcept;
        friend bool operator<=(const SpdxApplicableLicenseExpression& lhs,
                               const SpdxApplicableLicenseExpression& rhs) noexcept;
        friend bool operator>(const SpdxApplicableLicenseExpression& lhs,
                              const SpdxApplicableLicenseExpression& rhs) noexcept;
        friend bool operator>=(const SpdxApplicableLicenseExpression& lhs,
                               const SpdxApplicableLicenseExpression& rhs) noexcept;
    };

    struct ParsedSpdxLicenseDeclaration
    {
        ParsedSpdxLicenseDeclaration();
        ParsedSpdxLicenseDeclaration(NullTag);
        ParsedSpdxLicenseDeclaration(std::string&& license_text,
                                     std::vector<SpdxApplicableLicenseExpression>&& applicable_licenses);

        ParsedSpdxLicenseDeclaration(const ParsedSpdxLicenseDeclaration&) = default;
        ParsedSpdxLicenseDeclaration(ParsedSpdxLicenseDeclaration&&) = default;
        ParsedSpdxLicenseDeclaration& operator=(const ParsedSpdxLicenseDeclaration&) = default;
        ParsedSpdxLicenseDeclaration& operator=(ParsedSpdxLicenseDeclaration&&) = default;

        std::string to_string() const;
        void to_string(std::string& target) const;

        friend bool operator==(const ParsedSpdxLicenseDeclaration& lhs,
                               const ParsedSpdxLicenseDeclaration& rhs) noexcept;
        friend bool operator!=(const ParsedSpdxLicenseDeclaration& lhs,
                               const ParsedSpdxLicenseDeclaration& rhs) noexcept;

        SpdxLicenseDeclarationKind kind() const noexcept { return m_kind; }
        const std::string& license_text() const noexcept { return m_license_text; }
        const std::vector<SpdxApplicableLicenseExpression>& applicable_licenses() const noexcept
        {
            return m_applicable_licenses;
        }

    private:
        SpdxLicenseDeclarationKind m_kind;
        std::string m_license_text;
        std::vector<SpdxApplicableLicenseExpression> m_applicable_licenses;
    };

    /// <summary>
    /// Port metadata of additional feature in a package (part of CONTROL file)
    /// </summary>
    struct FeatureParagraph
    {
        std::string name;
        std::vector<std::string> description;
        std::vector<Dependency> dependencies;
        PlatformExpression::Expr supports_expression;
        ParsedSpdxLicenseDeclaration license;

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
        Version version;
        std::vector<std::string> description;
        std::vector<std::string> summary;
        std::vector<std::string> maintainers;
        std::string homepage;
        std::string documentation;
        std::vector<Dependency> dependencies;
        std::vector<DependencyOverride> overrides;
        std::vector<DependencyRequestedFeature> default_features;

        ParsedSpdxLicenseDeclaration license;

        Optional<std::string> builtin_baseline;

        Optional<Json::Object> configuration;
        ConfigurationSource configuration_source = ConfigurationSource::None;
        // Currently contacts is only a Json::Object but it will eventually be unified with maintainers
        Json::Object contacts;

        PlatformExpression::Expr supports_expression;

        Json::Object extra_info;

        friend bool operator==(const SourceParagraph& lhs, const SourceParagraph& rhs);
        friend bool operator!=(const SourceParagraph& lhs, const SourceParagraph& rhs) { return !(lhs == rhs); }
    };

    enum class PortSourceKind
    {
        Unknown,
        Builtin,
        Overlay,
        Git,
        Filesystem,
    };

    struct NoAssertionTag
    {
    };

    inline constexpr NoAssertionTag no_assertion;

    struct PortLocation
    {
        explicit PortLocation(const Path& port_directory, NoAssertionTag, PortSourceKind kind);
        explicit PortLocation(Path&& port_directory, NoAssertionTag, PortSourceKind kind);
        explicit PortLocation(const Path& port_directory, std::string&& spdx_location, PortSourceKind kind);
        explicit PortLocation(Path&& port_directory, std::string&& spdx_location, PortSourceKind kind);
        PortLocation(const PortLocation&) = default;
        PortLocation(PortLocation&&) = default;
        PortLocation& operator=(const PortLocation&) = default;
        PortLocation& operator=(PortLocation&&) = default;

        Path port_directory;

        /// Should model SPDX PackageDownloadLocation. Empty implies NOASSERTION.
        /// See https://spdx.github.io/spdx-spec/package-information/#77-package-download-location-field
        std::string spdx_location;

        PortSourceKind kind;
    };

    /// <summary>
    /// Full metadata of a package: core and other features.
    /// </summary>
    struct SourceControlFile
    {
        SourceControlFile clone() const;
        static ExpectedL<std::unique_ptr<SourceControlFile>> parse_project_manifest_object(StringView origin,
                                                                                           const Json::Object& object,
                                                                                           MessageSink& warnings_sink);

        static ExpectedL<std::unique_ptr<SourceControlFile>> parse_port_manifest_object(StringView origin,
                                                                                        const Json::Object& object,
                                                                                        MessageSink& warnings_sink);

        static ExpectedL<std::unique_ptr<SourceControlFile>> parse_control_file(
            StringView origin, std::vector<Paragraph>&& control_paragraphs);

        // Always non-null in non-error cases
        std::unique_ptr<SourceParagraph> core_paragraph;
        std::vector<std::unique_ptr<FeatureParagraph>> feature_paragraphs;
        Json::Object extra_features_info;

        Optional<const FeatureParagraph&> find_feature(StringView featurename) const;
        Optional<const std::vector<Dependency>&> find_dependencies_for_feature(const std::string& featurename) const;
        bool has_qualified_dependencies() const;

        ExpectedL<Unit> check_against_feature_flags(const Path& origin,
                                                    const FeatureFlagSettings& flags,
                                                    bool is_default_builtin_registry = true) const;

        const std::string& to_name() const noexcept { return core_paragraph->name; }
        VersionScheme to_version_scheme() const noexcept { return core_paragraph->version_scheme; }
        const Version& to_version() const noexcept { return core_paragraph->version; }
        SchemedVersion to_schemed_version() const
        {
            return SchemedVersion{core_paragraph->version_scheme, core_paragraph->version};
        }
        VersionSpec to_version_spec() const { return {core_paragraph->name, core_paragraph->version}; }

        friend bool operator==(const SourceControlFile& lhs, const SourceControlFile& rhs);
        friend bool operator!=(const SourceControlFile& lhs, const SourceControlFile& rhs) { return !(lhs == rhs); }
    };

    Json::Object serialize_manifest(const SourceControlFile& scf);

    ExpectedL<ManifestConfiguration> parse_manifest_configuration(const Json::Object& manifest,
                                                                  StringView origin,
                                                                  MessageSink& warningsSink);

    /// <summary>
    /// Named pair of a SourceControlFile and the location of this file
    /// </summary>
    struct SourceControlFileAndLocation
    {
        const std::string& to_name() const noexcept { return source_control_file->to_name(); }
        const Version& to_version() const { return source_control_file->to_version(); }
        VersionScheme scheme() const { return source_control_file->core_paragraph->version_scheme; }
        SchemedVersion schemed_version() const { return {scheme(), to_version()}; }
        VersionSpec to_version_spec() const { return source_control_file->to_version_spec(); }
        Path port_directory() const { return control_path.parent_path(); }

        SourceControlFileAndLocation clone() const
        {
            std::unique_ptr<SourceControlFile> scf;
            if (source_control_file)
            {
                scf = std::make_unique<SourceControlFile>(source_control_file->clone());
            }

            return SourceControlFileAndLocation{std::move(scf), control_path, spdx_location, kind};
        }

        std::unique_ptr<SourceControlFile> source_control_file;
        Path control_path;

        /// Should model SPDX PackageDownloadLocation. Empty implies NOASSERTION.
        /// See https://spdx.github.io/spdx-spec/package-information/#77-package-download-location-field
        std::string spdx_location;

        PortSourceKind kind = PortSourceKind::Unknown;
    };

    void print_error_message(const LocalizedString& message);

    ParsedSpdxLicenseDeclaration parse_spdx_license_expression(StringView sv, ParseMessages& messages);
    ParsedSpdxLicenseDeclaration parse_spdx_license_expression_required(StringView sv);

    // Exposed for testing
    ExpectedL<std::vector<Dependency>> parse_dependencies_list(const std::string& str,
                                                               StringView origin,
                                                               TextRowCol textrowcol = {});

    constexpr StringLiteral OVERRIDES = "overrides";
}
