#pragma once

namespace vcpkg
{
    struct Configuration;
    struct ConfigurationAndSource;
    struct RegistryConfig;
    struct ManifestConfiguration;

    enum class ConfigurationSource
    {
        None,
        VcpkgConfigurationFile,
        ManifestFileVcpkgConfiguration,
        ManifestFileConfiguration
    };
}
