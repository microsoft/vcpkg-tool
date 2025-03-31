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
        Builtin,   // Same as directory but we remember that it came from builtin_ports_directory
    };
}
