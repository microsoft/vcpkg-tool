#include <vcpkg/base/jsonreader.h>
#include <vcpkg/base/system.print.h>

#include <vcpkg/configuration.h>
#include <vcpkg/metrics.h>
#include <vcpkg/vcpkgcmdarguments.h>

namespace
{
    using namespace vcpkg;

    struct DictionaryDeserializer final : Json::IDeserializer<Json::Object>
    {
        virtual StringView type_name() const override { return "an `string: string` dictionary object"; }

        virtual Optional<Json::Object> visit_object(Json::Reader& r, const Json::Object& obj) override;

        static DictionaryDeserializer instance;
    };
    DictionaryDeserializer DictionaryDeserializer::instance;

    struct CeMetadataDeserializer final : Json::IDeserializer<Json::Object>
    {
        virtual StringView type_name() const override { return "an object containing ce metadata"; }

        constexpr static StringLiteral CE_ERROR = "error";
        constexpr static StringLiteral CE_WARNING = "warning";
        constexpr static StringLiteral CE_MESSAGE = "message";
        constexpr static StringLiteral CE_APPLY = "apply";
        constexpr static StringLiteral CE_SETTINGS = "settings";
        constexpr static StringLiteral CE_REQUIRES = "requires";
        constexpr static StringLiteral CE_SEE_ALSO = "see-also";
        constexpr static StringLiteral CE_DEMANDS = "demands";

        virtual Optional<Json::Object> visit_object(Json::Reader& r, const Json::Object& obj) override;

        static CeMetadataDeserializer instance;
    };
    CeMetadataDeserializer CeMetadataDeserializer::instance;
    constexpr StringLiteral CeMetadataDeserializer::CE_ERROR;
    constexpr StringLiteral CeMetadataDeserializer::CE_WARNING;
    constexpr StringLiteral CeMetadataDeserializer::CE_MESSAGE;
    constexpr StringLiteral CeMetadataDeserializer::CE_APPLY;
    constexpr StringLiteral CeMetadataDeserializer::CE_SETTINGS;
    constexpr StringLiteral CeMetadataDeserializer::CE_REQUIRES;
    constexpr StringLiteral CeMetadataDeserializer::CE_SEE_ALSO;
    constexpr StringLiteral CeMetadataDeserializer::CE_DEMANDS;

    struct DemandsDeserializer final : Json::IDeserializer<Json::Object>
    {
        virtual StringView type_name() const override { return "a ce.demands object"; }

        virtual Optional<Json::Object> visit_object(Json::Reader& r, const Json::Object& obj) override;

        static DemandsDeserializer instance;
    };
    DemandsDeserializer DemandsDeserializer::instance;

    struct ConfigurationDeserializer final : Json::IDeserializer<Configuration>
    {
        virtual StringView type_name() const override { return "a configuration object"; }

        constexpr static StringLiteral DEFAULT_REGISTRY = "default-registry";
        constexpr static StringLiteral REGISTRIES = "registries";

        virtual Optional<Configuration> visit_object(Json::Reader& r, const Json::Object& obj) override;

        ConfigurationDeserializer(const Path& configuration_directory);

    private:
        Path configuration_directory;
    };
    constexpr StringLiteral ConfigurationDeserializer::DEFAULT_REGISTRY;
    constexpr StringLiteral ConfigurationDeserializer::REGISTRIES;

    Optional<Json::Object> DictionaryDeserializer::visit_object(Json::Reader& r, const Json::Object& obj)
    {
        Json::Object ret;
        for (const auto& el : obj)
        {
            auto key = el.first;
            auto value = el.second;

            if (!value.is_string())
            {
                r.add_generic_error(type_name(), "All elements must be key/value pairs");
                continue;
            }

            ret.insert_or_replace(el.first.to_string(), el.second);
        }
        return ret;
    }

    Optional<Json::Object> CeMetadataDeserializer::visit_object(Json::Reader& r, const Json::Object& obj)
    {
        auto extract_string = [&](const Json::Object& obj, StringView key, Json::Object& put_into) {
            static Json::StringDeserializer string_deserializer{"a string"};

            std::string value;
            if (r.optional_object_field(obj, key, value, string_deserializer))
            {
                put_into.insert_or_replace(key.to_string(), Json::Value::string(value));
            }
        };
        auto extract_dictionary = [&](const Json::Object& obj, StringView key, Json::Object& put_into) {
            Json::Object value;
            if (r.optional_object_field(obj, key, value, DictionaryDeserializer::instance))
            {
                put_into.insert_or_replace(key.to_string(), value);
            }
        };

        Json::Object ret;
        extract_string(obj, CE_ERROR, ret);
        extract_string(obj, CE_WARNING, ret);
        extract_string(obj, CE_MESSAGE, ret);
        if (auto ce_apply = obj.get(CE_APPLY))
        {
            if (!ce_apply->is_object())
            {
                r.add_generic_error(type_name(), CE_APPLY, " must be an object");
            }
            else
            {
                ret.insert_or_replace(CE_APPLY, *ce_apply);
            }
        }
        extract_dictionary(obj, CE_SETTINGS, ret);
        extract_dictionary(obj, CE_REQUIRES, ret);
        extract_dictionary(obj, CE_SEE_ALSO, ret);

        Json::Object demands;
        if (r.optional_object_field(obj, CE_DEMANDS, demands, DemandsDeserializer::instance))
        {
            ret.insert_or_replace(CE_DEMANDS, demands);
        }
        return ret;
    }

    Optional<Json::Object> DemandsDeserializer::visit_object(Json::Reader& r, const Json::Object& obj)
    {
        Json::Object ret;
        for (const auto& el : obj)
        {
            auto filter = el.first.to_string();
            if (Strings::starts_with(filter, "$"))
            {
                // Put comments back without attempting to parse.
                ret.insert_or_replace(filter, el.second);
                continue;
            }

            if (!el.second.is_object())
            {
                r.add_generic_error(type_name(), filter, " must be an object");
                continue;
            }

            auto maybe_demand = r.visit(el.second, CeMetadataDeserializer::instance);
            if (maybe_demand.has_value())
            {
                ret.insert_or_replace(filter, maybe_demand.value_or_exit(VCPKG_LINE_INFO));
            }
        }
        return ret;
    }

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

        Json::Object ce_config_obj;
        auto maybe_ce_config = r.visit(obj, CeMetadataDeserializer::instance);
        if (auto ce_config = maybe_ce_config.get())
        {
            ce_config_obj = *ce_config;
        }

        return Configuration{std::move(registries), ce_config_obj, extra_info};
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

        for (const auto& el : config.ce_metadata)
        {
            obj.insert(el.first.to_string(), el.second);
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
