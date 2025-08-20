#pragma once

#include <vcpkg/base/fwd/fmt.h>
#include <vcpkg/base/fwd/parse.h>
#include <vcpkg/base/fwd/span.h>

#include <vcpkg/fwd/packagespec.h>

#include <vcpkg/base/expected.h>
#include <vcpkg/base/optional.h>
#include <vcpkg/base/unicode.h>

#include <vcpkg/platform-expression.h>
#include <vcpkg/triplet.h>

#include <string>
#include <vector>

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
        FeatureSpec(const PackageSpec& spec, StringView feature)
            : m_spec(spec), m_feature(feature.data(), feature.size())
        {
        }

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

    std::string format_name_only_feature_spec(StringView package_name, StringView feature_name);

    template<class T>
    struct Located
    {
        SourceLoc loc;
        T value;

        template<class... Args>
        explicit Located(const SourceLoc& loc, Args&&... args) : loc(loc), value(std::forward<Args>(args)...)
        {
        }

        friend bool operator==(const Located& lhs, const Located& rhs)
        {
            return lhs.loc.row == rhs.loc.row && lhs.loc.column == rhs.loc.column && lhs.value == rhs.value;
        }
        friend bool operator!=(const Located& lhs, const Located& rhs) { return !(lhs == rhs); }
    };

    struct LocatedStringLess
    {
        using is_transparent = void;

        bool operator()(const Located<std::string>& lhs, const Located<std::string>& rhs) const
        {
            return lhs.value < rhs.value;
        }

        template<class Left>
        bool operator()(const Left& lhs, const Located<std::string>& rhs) const
        {
            return lhs < rhs.value;
        }

        template<class Right>
        bool operator()(const Located<std::string>& lhs, const Right& rhs) const
        {
            return lhs.value < rhs;
        }
    };

    Located<std::vector<std::string>> hoist_locations(std::vector<Located<std::string>>&& values);

    /// In an internal feature set, "default" represents default features and missing "core" has no semantic
    struct InternalFeatureSet : std::vector<std::string>
    {
        using std::vector<std::string>::vector;

        bool empty_or_only_core() const;
    };

    InternalFeatureSet internalize_feature_list(View<Located<std::string>> fs, ImplicitDefault id);

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

        FullPackageSpec(const PackageSpec& spec, const InternalFeatureSet& features)
            : package_spec(spec), features(features)
        {
        }

        FullPackageSpec(const PackageSpec& spec, InternalFeatureSet&& features)
            : package_spec(spec), features(std::move(features))
        {
        }

        FullPackageSpec(PackageSpec&& spec, const InternalFeatureSet& features)
            : package_spec(std::move(spec)), features(features)
        {
        }

        FullPackageSpec(PackageSpec&& spec, InternalFeatureSet&& features)
            : package_spec(std::move(spec)), features(std::move(features))
        {
        }

        std::string to_string() const;
        void to_string(std::string& s) const;

        /// Splats into individual FeatureSpec's
        void expand_fspecs_to(std::vector<FeatureSpec>& oFut) const;

        friend bool operator==(const FullPackageSpec& l, const FullPackageSpec& r)
        {
            return l.package_spec == r.package_spec && l.features == r.features;
        }
        friend bool operator!=(const FullPackageSpec& l, const FullPackageSpec& r) { return !(l == r); }
    };

    struct ParsedQualifiedSpecifier
    {
        Located<std::string> name;
        Optional<std::vector<Located<std::string>>> features;
        Optional<Located<std::string>> triplet;
        Optional<Located<PlatformExpression::Expr>> platform;

        const PlatformExpression::Expr& platform_or_always_true() const;

        /// @param id add "default" if "core" is not present
        // Assumes AllowPlatformSpec::No
        FullPackageSpec to_full_spec(Triplet default_triplet, ImplicitDefault id) const;

        // Assumes AllowFeatures::No, AllowPlatformSpec::No
        PackageSpec to_package_spec(Triplet default_triplet) const;
    };

    Optional<std::string> parse_feature_name(ParserBase& parser);
    Optional<std::string> parse_package_name(ParserBase& parser);
    ExpectedL<ParsedQualifiedSpecifier> parse_qualified_specifier(StringView input,
                                                                  AllowFeatures allow_features,
                                                                  ParseExplicitTriplet parse_explicit_triplet,
                                                                  AllowPlatformSpec allow_platform_spec);
    Optional<ParsedQualifiedSpecifier> parse_qualified_specifier(ParserBase& parser,
                                                                 AllowFeatures allow_features,
                                                                 ParseExplicitTriplet parse_explicit_triplet,
                                                                 AllowPlatformSpec allow_platform_spec);
} // namespace vcpkg

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
VCPKG_FORMAT_WITH_TO_STRING(vcpkg::FullPackageSpec);
