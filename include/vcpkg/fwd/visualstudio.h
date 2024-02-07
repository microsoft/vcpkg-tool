#pragma once

namespace vcpkg::VisualStudio
{
    enum class ReleaseType
    {
        // These must be sorted by the order that we want them given a choice
        STABLE,
        PRERELEASE,
        LEGACY,
        UNKNOWN,
    };

    struct VisualStudioInstance;
} // namespace vcpkg::VisualStudio
