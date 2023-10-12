#pragma once

#include <vcpkg/base/fwd/format.h>
#include <vcpkg/base/fwd/stringview.h>

#include <vcpkg/base/expected.h>

namespace vcpkg
{
    struct Version
    {
        Version() noexcept;
        Version(std::string&& value, int port_version);
        Version(const std::string& value, int port_version);

        std::string to_string() const;
        void to_string(std::string& out) const;

        friend bool operator==(const Version& left, const Version& right);
        friend bool operator!=(const Version& left, const Version& right);

        // Version has no operator< because without a scheme it is not necessarily semantically comparable;
        // VersionMapLess is provided as a less than comparison for use in std::map.
        friend struct VersionMapLess;

        const std::string& text() const { return m_text; }
        int port_version() const { return m_port_version; }

    private:
        std::string m_text;
        int m_port_version = 0;
    };

    struct VersionDiff
    {
        Version left;
        Version right;

        VersionDiff() noexcept;
        VersionDiff(const Version& left, const Version& right);

        std::string to_string() const;
    };

    struct VersionMapLess
    {
        bool operator()(const Version& left, const Version& right) const;
    };

    enum class VerComp
    {
        unk = -2,
        lt = -1, // these values are chosen to align with traditional -1/0/1 for less/equal/greater
        eq = 0,
        gt = 1,
    };

    // converts a strcmp <0/0/>0 style integer into a VerComp
    VerComp int_to_vercomp(int comparison_result);

    enum class VersionScheme
    {
        Missing,
        Relaxed,
        Semver,
        Date,
        String
    };

    struct SchemedVersion
    {
        VersionScheme scheme;
        Version version;

        friend bool operator==(const SchemedVersion& lhs, const SchemedVersion& rhs);
    };

    StringLiteral to_string_literal(VersionScheme scheme);

    struct VersionSpec
    {
        std::string port_name;
        Version version;

        VersionSpec(const std::string& port_name, const Version& version);

        VersionSpec(const std::string& port_name, const std::string& version_string, int port_version);

        std::string to_string() const;

        friend bool operator==(const VersionSpec& lhs, const VersionSpec& rhs);
        friend bool operator!=(const VersionSpec& lhs, const VersionSpec& rhs);
    };

    struct VersionSpecHasher
    {
        std::size_t operator()(const VersionSpec& key) const;
    };

    struct DotVersion
    {
        DotVersion() { } // intentionally disable making this type an aggregate

        std::string original_string;
        std::string version_string;
        std::string prerelease_string;

        std::vector<uint64_t> version;
        std::vector<std::string> identifiers;

        friend bool operator==(const DotVersion& lhs, const DotVersion& rhs);
        friend bool operator!=(const DotVersion& lhs, const DotVersion& rhs) { return !(lhs == rhs); }
        friend bool operator<(const DotVersion& lhs, const DotVersion& rhs);
        friend bool operator>(const DotVersion& lhs, const DotVersion& rhs) { return rhs < lhs; }
        friend bool operator>=(const DotVersion& lhs, const DotVersion& rhs) { return !(lhs < rhs); }
        friend bool operator<=(const DotVersion& lhs, const DotVersion& rhs) { return !(rhs < lhs); }

        static ExpectedL<DotVersion> try_parse(StringView str, VersionScheme target_scheme);
        static ExpectedL<DotVersion> try_parse_relaxed(StringView str);
        static ExpectedL<DotVersion> try_parse_semver(StringView str);
    };

    VerComp compare(const DotVersion& a, const DotVersion& b);

    struct DateVersion
    {
        DateVersion() { } // intentionally disable making this type an aggregate

        std::string original_string;
        std::string version_string;
        std::vector<uint64_t> identifiers;

        friend bool operator==(const DateVersion& lhs, const DateVersion& rhs);
        friend bool operator!=(const DateVersion& lhs, const DateVersion& rhs) { return !(lhs == rhs); }
        friend bool operator<(const DateVersion& lhs, const DateVersion& rhs);
        friend bool operator>(const DateVersion& lhs, const DateVersion& rhs) { return rhs < lhs; }
        friend bool operator>=(const DateVersion& lhs, const DateVersion& rhs) { return !(lhs < rhs); }
        friend bool operator<=(const DateVersion& lhs, const DateVersion& rhs) { return !(rhs < lhs); }

        static ExpectedL<DateVersion> try_parse(StringView version);
    };

    VerComp compare(const DateVersion& a, const DateVersion& b);

    // Try parsing with all version schemas and return 'unk' if none match
    VerComp compare_any(const Version& a, const Version& b);

    VerComp compare_versions(const SchemedVersion& a, const SchemedVersion& b);
    VerComp compare_versions(VersionScheme sa, const Version& a, VersionScheme sb, const Version& b);

    enum class VersionConstraintKind
    {
        None,
        Minimum
    };

    // this is for version parsing that isn't in vcpkg ports
    // stuff like tools, nuget, etc.
    struct ParsedExternalVersion
    {
        StringView major;
        StringView minor;
        StringView patch;

        void normalize();
    };

    StringView normalize_external_version_zeros(StringView sv);
    // /(\d\d\d\d)-(\d\d)-(\d\d).*/
    bool try_extract_external_date_version(ParsedExternalVersion& out, StringView version);
    // /(\d+)(\.\d+|$)(\.\d+)?.*/
    bool try_extract_external_dot_version(ParsedExternalVersion& out, StringView version);
}

VCPKG_FORMAT_WITH_TO_STRING(vcpkg::Version);
VCPKG_FORMAT_WITH_TO_STRING(vcpkg::VersionDiff);
VCPKG_FORMAT_WITH_TO_STRING_LITERAL_NONMEMBER(vcpkg::VersionScheme);
VCPKG_FORMAT_WITH_TO_STRING(vcpkg::VersionSpec);
