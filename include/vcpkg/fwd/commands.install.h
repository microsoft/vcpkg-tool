#pragma once

namespace vcpkg
{
    enum class KeepGoing
    {
        NO = 0,
        YES
    };

    enum class InstallResult
    {
        FILE_CONFLICTS,
        SUCCESS,
    };
}
