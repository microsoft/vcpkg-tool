#pragma once

#include <vcpkg/base/fwd/format.h>
#include <vcpkg/base/fwd/parse.h>

#include <vcpkg/base/expected.h>
#include <vcpkg/base/format.h>
#include <vcpkg/base/json.h>
#include <vcpkg/base/optional.h>
#include <vcpkg/base/view.h>

#include <vcpkg/platform-expression.h>
#include <vcpkg/triplet.h>
#include <vcpkg/versions.h>

namespace vcpkg
{
    ///
    /// <summary>
    /// Full specification of a package. Contains all information to reference
    /// a specific package.
    /// </summary>
    ///
    struct PackageSpec
    {
        PackageSpec() = default;
        PackageSpec(const std::string& name, Triplet triplet) : m_name(name), m_triplet(triplet) { }
        PackageSpec(std::string&& name, Triplet triplet) : m_name(std::move(name)), m_triplet(triplet) { }

        const std::string& name() const;

        Triplet triplet() const;

        std::string dir() const;

        std::string to_string() const;
        void to_string(std::string& s) const;

        bool operator<(const PackageSpec& other) const
        {
            if (name() < other.name()) return true;
            if (name() > other.name()) return false;
            return triplet() < other.triplet();
        }

    private:
        std::string m_name;
        Triplet m_triplet;
    };

    bool operator==(const PackageSpec& left, const PackageSpec& right);
    inline bool operator!=(const PackageSpec& left, const PackageSpec& right) { return !(left == right); }

    ///
    /// <summary>
    /// Full specification of a feature. Contains all information to reference
    /// a single feature in a specific package.
    /// </summary>
    ///
    struct FeatureSpec
    {
        FeatureSpec(const PackageSpec& spec, const std::string& feature) : m_spec(spec), m_feature(feature) { }

        const std::string& port() const { return m_spec.name(); }
        const std::string& feature() const { return m_feature; }
        Triplet triplet() const { return m_spec.triplet(); }

        const PackageSpec& spec() const { return m_spec; }

        std::string to_string() const;
        void to_string(std::string& out) const;

        bool operator<(const FeatureSpec& other) const
        {
            if (port() < other.port()) return true;
            if (port() > other.port()) return false;
            if (feature() < other.feature()) return true;
            if (feature() > other.feature()) return false;
            return triplet() < other.triplet();
        }

        bool operator==(const FeatureSpec& other) const
        {
            return triplet() == other.triplet() && port() == other.port() && feature() == other.feature();
        }

        bool operator!=(const FeatureSpec& other) const { return !(*this == other); }

    private:
        PackageSpec m_spec;
        std::string m_feature;
    };

    /// In an internal feature set, "default" represents default features and missing "core" has no semantic
    struct InternalFeatureSet : std::vector<std::string>
    {
        using std::vector<std::string>::vector;
    };

    ///
    /// <summary>
    /// Full specification of a package. Contains all information to reference
    /// a collection of features in a single package.
    /// </summary>
    ///
    struct FullPackageSpec
    {
        PackageSpec package_spec;
        InternalFeatureSet features;

        FullPackageSpec() = default;
        explicit FullPackageSpec(PackageSpec spec, InternalFeatureSet features)
            : package_spec(std::move(spec)), features(std::move(features))
        {
        }

        /// Splats into individual FeatureSpec's
        void expand_fspecs_to(std::vector<FeatureSpec>& oFut) const;

        friend bool operator==(const FullPackageSpec& l, const FullPackageSpec& r)
        {
            return l.package_spec == r.package_spec && l.features == r.features;
        }
        friend bool operator!=(const FullPackageSpec& l, const FullPackageSpec& r) { return !(l == r); }
    };

    struct DependencyConstraint
    {
        VersionConstraintKind type = VersionConstraintKind::None;
        std::string value;
        int port_version = 0;

        friend bool operator==(const DependencyConstraint& lhs, const DependencyConstraint& rhs);
        friend bool operator!=(const DependencyConstraint& lhs, const DependencyConstraint& rhs)
        {
            return !(lhs == rhs);
        }

        Optional<Version> try_get_minimum_version() const;
    };

    enum class ImplicitDefault : bool
    {
        NO,
        YES,
    };

    struct Dependency
    {
        struct Feature
        {
            std::string name;
            PlatformExpression::Expr platform;
            Feature(std::string name) : Feature(std::move(name), PlatformExpression::Expr::Empty()) { }
            Feature(std::string name, PlatformExpression::Expr platform)
                : name(std::move(name)), platform(std::move(platform))
            {
                Checks::check_exit(VCPKG_LINE_INFO,
                                   !this->name.empty() && this->name != "core" && this->name != "default");
            }
            friend bool operator==(const Feature& lhs, const Feature& rhs)
            {
                if (lhs.name != rhs.name) return false;
                if (!structurally_equal(lhs.platform, rhs.platform)) return false;

                return true;
            }
            friend bool operator!=(const Feature& lhs, const Feature& rhs) { return !(lhs == rhs); }
        };
        std::string name;
        // a list of "real" features without "core" or "default". Use member default_features instead.
        std::vector<Feature> features;
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
        std::string version;
        int port_version = 0;
        VersionScheme version_scheme = VersionScheme::String;

        Json::Object extra_info;

        friend bool operator==(const DependencyOverride& lhs, const DependencyOverride& rhs);
        friend bool operator!=(const DependencyOverride& lhs, const DependencyOverride& rhs) { return !(lhs == rhs); }
    };

    struct ParsedQualifiedSpecifier
    {
        std::string name;
        Optional<std::vector<std::string>> features;
        Optional<std::string> triplet;
        Optional<PlatformExpression::Expr> platform;

        /// @param id add "default" if "core" is not present
        /// @return nullopt on success. On failure, caller should supplement returned string with more context.
        ExpectedS<FullPackageSpec> to_full_spec(Triplet default_triplet, ImplicitDefault id) const;

        ExpectedS<PackageSpec> to_package_spec(Triplet default_triplet) const;
    };

    Optional<std::string> parse_feature_name(ParserBase& parser);
    Optional<std::string> parse_package_name(ParserBase& parser);
    ExpectedS<ParsedQualifiedSpecifier> parse_qualified_specifier(StringView input);
    Optional<ParsedQualifiedSpecifier> parse_qualified_specifier(ParserBase& parser);
}

template<>
struct std::hash<vcpkg::PackageSpec>
{
    size_t operator()(const vcpkg::PackageSpec& value) const
    {
        size_t hash = 17;
        hash = hash * 31 + std::hash<std::string>()(value.name());
        hash = hash * 31 + std::hash<vcpkg::Triplet>()(value.triplet());
        return hash;
    }
};

template<>
struct std::hash<vcpkg::FeatureSpec>
{
    size_t operator()(const vcpkg::FeatureSpec& value) const
    {
        size_t hash = std::hash<vcpkg::PackageSpec>()(value.spec());
        hash = hash * 31 + std::hash<std::string>()(value.feature());
        return hash;
    }
};

VCPKG_FORMAT_WITH_TO_STRING(vcpkg::PackageSpec);
VCPKG_FORMAT_WITH_TO_STRING(vcpkg::FeatureSpec);
