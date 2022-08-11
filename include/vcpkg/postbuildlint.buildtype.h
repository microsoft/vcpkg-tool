#pragma once

#include <vcpkg/base/stringview.h>

#include <vcpkg/build.h>

#include <array>

namespace vcpkg::PostBuildLint
{
    struct BuildType
    {
        BuildType() = delete;

        constexpr BuildType(ConfigurationType c, LinkageType l) : config(c), linkage(l) { }

        bool has_crt_linker_option(StringView sv) const;
        StringLiteral to_string() const;

        ConfigurationType config;
        LinkageType linkage;

        friend bool operator==(const BuildType& lhs, const BuildType& rhs)
        {
            return lhs.config == rhs.config && lhs.linkage == rhs.linkage;
        }
        friend bool operator!=(const BuildType& lhs, const BuildType& rhs) { return !(lhs == rhs); }
    };
}
