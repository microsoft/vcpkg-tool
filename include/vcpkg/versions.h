#pragma once

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

        const std::string text() const { return m_text; }
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
        Relaxed,
        Semver,
        Date,
        String
    };

    void to_string(std::string& out, VersionScheme scheme);

    struct VersionSpec
    {
        std::string port_name;
        Version version;

        VersionSpec(const std::string& port_name, const Version& version);

        VersionSpec(const std::string& port_name, const std::string& version_string, int port_version);

        friend bool operator==(const VersionSpec& lhs, const VersionSpec& rhs);
        friend bool operator!=(const VersionSpec& lhs, const VersionSpec& rhs);
    };

    struct VersionSpecHasher
    {
        std::size_t operator()(const VersionSpec& key) const;
    };

    struct DotVersion
    {
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

        static ExpectedS<DotVersion> try_parse(const std::string& str, VersionScheme target_scheme);
        static ExpectedS<DotVersion> try_parse_relaxed(const std::string& str);
        static ExpectedS<DotVersion> try_parse_semver(const std::string& str);
    };

    VerComp compare(const DotVersion& a, const DotVersion& b);

    struct DateVersion
    {
        std::string original_string;
        std::string version_string;
        std::vector<uint64_t> identifiers;

        friend bool operator==(const DateVersion& lhs, const DateVersion& rhs);
        friend bool operator!=(const DateVersion& lhs, const DateVersion& rhs) { return !(lhs == rhs); }
        friend bool operator<(const DateVersion& lhs, const DateVersion& rhs);
        friend bool operator>(const DateVersion& lhs, const DateVersion& rhs) { return rhs < lhs; }
        friend bool operator>=(const DateVersion& lhs, const DateVersion& rhs) { return !(lhs < rhs); }
        friend bool operator<=(const DateVersion& lhs, const DateVersion& rhs) { return !(rhs < lhs); }

        static ExpectedS<DateVersion> try_parse(const std::string& str);
    };

    VerComp compare(const DateVersion& a, const DateVersion& b);

    enum class VersionConstraintKind
    {
        None,
        Minimum
    };
}
