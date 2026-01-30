#pragma once

namespace vcpkg
{
    struct Configuration;
    struct ConfigurationAndSource;
    struct RegistryConfig;
    struct ManifestConfiguration;
    struct EditablePortsConfig;

    enum class ConfigurationSource
    {
        None,
        VcpkgConfigurationFile,
        ManifestFileVcpkgConfiguration,
        ManifestFileConfiguration
    };
}
