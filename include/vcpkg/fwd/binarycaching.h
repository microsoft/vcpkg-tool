#pragma once

namespace vcpkg
{
    enum class RestoreResult : unsigned char
    {
        unavailable,
        restored,
    };

    enum class CacheAvailability : unsigned char
    {
        unknown,
        unavailable,
        available,
    };

    enum class CacheStatusState : unsigned char
    {
        unknown,   // the cache status of the indicated package ABI is unknown
        available, // the cache is known to contain the package ABI, but it has not been restored
        restored,  // the cache contains the ABI and it has been restored to the packages tree
    };

    struct IReadBinaryProvider;
    struct IWriteBinaryProvider;
    struct BinaryCache;
    struct BinaryConfigParserState;
    struct BinaryPackageReadInfo;
    struct BinaryPackageWriteInfo;
}
