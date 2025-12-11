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
