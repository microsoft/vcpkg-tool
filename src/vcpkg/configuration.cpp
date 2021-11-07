#include <vcpkg/base/jsonreader.h>
#include <vcpkg/base/system.print.h>

#include <vcpkg/configuration.h>
#include <vcpkg/metrics.h>
#include <vcpkg/vcpkgcmdarguments.h>

namespace
{
    using namespace vcpkg;

    struct ConfigurationDeserializer final : Json::IDeserializer<Configuration>
    {
        virtual StringView type_name() const override { return "a configuration object"; }

        constexpr static StringLiteral DEFAULT_REGISTRY = "default-registry";
        constexpr static StringLiteral REGISTRIES = "registries";
        virtual View<StringView> valid_fields() const override
        {
            constexpr static StringView t[] = {DEFAULT_REGISTRY, REGISTRIES};
            return t;
        }

        virtual Optional<Configuration> visit_object(Json::Reader& r, const Json::Object& obj) override;

        ConfigurationDeserializer(const Path& configuration_directory);

    private:
        Path configuration_directory;
    };

    constexpr StringLiteral ConfigurationDeserializer::DEFAULT_REGISTRY;
    constexpr StringLiteral ConfigurationDeserializer::REGISTRIES;

    Optional<Configuration> ConfigurationDeserializer::visit_object(Json::Reader& r, const Json::Object& obj)
    {
        Json::Object extra_info;
        for (const auto& el : obj)
        {
            if (Strings::starts_with(el.first, "$"))
            {
                extra_info.insert_or_replace(el.first.to_string(), el.second);
            }
        }

        RegistrySet registries;

        auto impl_des = get_registry_implementation_deserializer(configuration_directory);

        std::unique_ptr<RegistryImplementation> default_registry;
        if (r.optional_object_field(obj, DEFAULT_REGISTRY, default_registry, *impl_des))
        {
            registries.set_default_registry(std::move(default_registry));
        }

        auto reg_des = get_registry_array_deserializer(configuration_directory);
        std::vector<Registry> regs;
        r.optional_object_field(obj, REGISTRIES, regs, *reg_des);

        for (Registry& reg : regs)
        {
            registries.add_registry(std::move(reg));
        }

        return Configuration{std::move(registries), extra_info};
    }

    ConfigurationDeserializer::ConfigurationDeserializer(const Path& configuration_directory)
        : configuration_directory(configuration_directory)
    {
    }

    static Json::Object serialize_configuration_impl(const Configuration& config)
    {
        constexpr static StringLiteral REGISTRY_PACKAGES = "packages";

        Json::Object obj;

        for (const auto& el : config.extra_info)
        {
            obj.insert(el.first.to_string(), el.second);
        }

        if (config.registry_set.default_registry())
        {
            obj.insert(ConfigurationDeserializer::DEFAULT_REGISTRY,
                       config.registry_set.default_registry()->serialize());
        }

        auto reg_view = config.registry_set.registries();
        if (reg_view.size() > 0)
        {
            auto& reg_arr = obj.insert(ConfigurationDeserializer::REGISTRIES, Json::Array());
            for (const auto& reg : reg_view)
            {
                auto reg_obj = reg.implementation().serialize();
                auto& packages = reg_obj.insert(REGISTRY_PACKAGES, Json::Array{});
                for (const auto& pkg : reg.packages())
                    packages.push_back(Json::Value::string(pkg));
                reg_arr.push_back(std::move(reg_obj));
            }
        }

        return obj;
    }

}

std::unique_ptr<Json::IDeserializer<Configuration>> vcpkg::make_configuration_deserializer(const Path& config_directory)
{
    return std::make_unique<ConfigurationDeserializer>(config_directory);
}

Json::Object vcpkg::serialize_configuration(const Configuration& config)
{
    return serialize_configuration_impl(config);
}

namespace vcpkg
{
    void Configuration::validate_feature_flags(const FeatureFlagSettings& flags)
    {
        if (!flags.registries && registry_set.has_modifications())
        {
            LockGuardPtr<Metrics>(g_metrics)->track_property(
                "registries-error-registry-modification-without-feature-flag", "defined");
            vcpkg::printf(Color::warning,
                          "Warning: configuration specified the \"registries\" or \"default-registries\" field, but "
                          "the %s feature flag was not enabled.\n",
                          VcpkgCmdArguments::REGISTRIES_FEATURE);
            registry_set = RegistrySet();
        }
    }
}
