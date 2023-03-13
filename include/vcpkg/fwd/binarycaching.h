#pragma once

namespace vcpkg
{
    enum class RestoreResult
    {
        unavailable,
        restored,
    };

    enum class CacheAvailability
    {
        unavailable,
        available,
    };

    enum class CacheStatusState
    {
        unknown,   // the cache status of the indicated package ABI is unknown
        available, // the cache is known to contain the package ABI, but it has not been restored
        restored,  // the cache contains the ABI and it has been restored to the packages tree
    };

    struct CacheStatus;
    struct IBinaryProvider;
    struct BinaryCache;
    struct BinaryConfigParserState;
    struct BinaryProviderPushRequest;
}
