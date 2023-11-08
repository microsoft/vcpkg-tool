#pragma once

namespace vcpkg
{
    enum class InstallState
    {
        ERROR_STATE,
        NOT_INSTALLED,
        HALF_INSTALLED,
        INSTALLED,
    };

    enum class Want
    {
        ERROR_STATE,
        UNKNOWN,
        INSTALL,
        HOLD,
        DEINSTALL,
        PURGE
    };

    struct StatusParagraph;
    struct InstalledPackageView;
}
