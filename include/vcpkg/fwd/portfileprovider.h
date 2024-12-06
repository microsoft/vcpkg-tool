#pragma once

namespace vcpkg
{
    struct OverlayPortPaths;
    struct OverlayPortIndexEntry;
    struct PortFileProvider;
    struct PathsPortFileProvider;
    struct IVersionedPortfileProvider;
    struct IBaselineProvider;
    struct IOverlayProvider;

    enum class OverlayPortKind
    {
        Unknown,   // We have not tested yet
        Port,      // The overlay directory is itself a port
        Directory, // The overlay directory itself contains port directories
    };
}
