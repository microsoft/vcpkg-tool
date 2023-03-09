#pragma once

namespace vcpkg
{
    enum class DeploymentKind
    {
        Git,         // vcpkg was deployed with "git clone" or similar
        OneLiner,    // vcpkg was deployed with the "one liner" installer
        VisualStudio // vcpkg was deployed by the Visual Studio installer
    };

    struct BundleSettings;
}
