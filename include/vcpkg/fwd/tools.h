#pragma once

namespace vcpkg
{
    enum class RequireExactVersions
    {
        YES,
        NO,
    };

    struct ToolVersion;
    struct ToolCache;
    struct PathAndVersion;
}
