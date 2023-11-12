#pragma once

namespace vcpkg
{
    enum class UnsupportedPortAction : bool
    {
        Warn,
        Error,
    };

    enum class RequestType
    {
        UNKNOWN,
        USER_REQUESTED,
        AUTO_SELECTED
    };

    enum class InstallPlanType
    {
        UNKNOWN,
        BUILD_AND_INSTALL,
        ALREADY_INSTALLED,
        EXCLUDED
    };

    enum class ExportPlanType
    {
        UNKNOWN,
        NOT_BUILT,
        ALREADY_BUILT
    };

    struct BasicAction;
    struct PackageAction;
    struct InstallPlanAction;
    struct NotInstalledAction;
    struct RemovePlanAction;
    struct ActionPlan;
    struct ExportPlanAction;
    struct CreateInstallPlanOptions;
    struct RemovePlan;
    struct FormattedPlan;
    struct StatusParagraphs;
}
