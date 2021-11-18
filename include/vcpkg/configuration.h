#pragma once

#include <vcpkg/fwd/configuration.h>
#include <vcpkg/fwd/vcpkgcmdarguments.h>

#include <vcpkg/base/files.h>
#include <vcpkg/base/json.h>

#include <vcpkg/registries.h>

namespace vcpkg
{
    struct RegistryConfig
    {
        // Missing kind means "null"
        Optional<std::string> kind;
        Optional<std::string> baseline;
        Optional<std::string> location;
        Optional<std::string> name;
        Optional<Path> path;
        Optional<std::string> reference;
        Optional<std::string> repo;
        std::vector<std::string> packages;

        Json::Value serialize() const;
    };

    struct Configuration
    {
        Optional<RegistryConfig> default_reg;
        std::vector<RegistryConfig> registries;
        Json::Object ce_metadata;
        Json::Object extra_info;

        Json::Object serialize() const;
        void validate_feature_flags();
        std::unique_ptr<RegistrySet> instantiate_registry_set() const;
        static View<StringView> known_fields();
    };

    struct ManifestConfiguration
    {
        Optional<std::string> builtin_baseline;
        Optional<Configuration> config;
    };

    Json::IDeserializer<Configuration>& get_configuration_deserializer();
    Json::IDeserializer<ManifestConfiguration>& get_manifest_configuration_deserializer();
    std::vector<std::string> find_unknown_fields(const Configuration& config);
}
