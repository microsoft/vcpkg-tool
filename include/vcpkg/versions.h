#pragma once

#include <vcpkg/base/expected.h>

#include <vcpkg/versiont.h>

namespace vcpkg::Versions
{
    using Version = VersionT;

    enum class VerComp
    {
        unk,
        lt,
        eq,
        gt,
    };

    enum class Scheme
    {
        Relaxed,
        Semver,
        Date,
        String
    };

    void to_string(std::string& out, Scheme scheme);

    struct VersionSpec
    {
        std::string port_name;
        VersionT version;

        VersionSpec(const std::string& port_name, const VersionT& version);

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
    };

    struct DateVersion
    {
        std::string original_string;
        std::string version_string;
        std::vector<uint64_t> identifiers;

        static ExpectedS<DateVersion> from_string(const std::string& str);
    };

    ExpectedS<DotVersion> relaxed_from_string(const std::string& str);
    ExpectedS<DotVersion> semver_from_string(const std::string& str);

    VerComp compare(const std::string& a, const std::string& b, Scheme scheme);
    VerComp compare(const DotVersion& a, const DotVersion& b);
    VerComp compare(const DateVersion& a, const DateVersion& b);

    struct Constraint
    {
        enum class Type
        {
            None,
            Minimum
        };
    };
}
