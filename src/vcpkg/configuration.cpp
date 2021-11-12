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
        virtual StringView type_name() const override { return "a `string: string` dictionary"; }

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

    struct DemandsDeserializer final : Json::IDeserializer<Json::Object>
    {
        virtual StringView type_name() const override { return "a demand object"; }

        constexpr static StringLiteral CE_DEMANDS = "demands";

        virtual Optional<Json::Object> visit_object(Json::Reader& r, const Json::Object& obj) override;

        static DemandsDeserializer instance;
    };
    DemandsDeserializer DemandsDeserializer::instance;
    constexpr StringLiteral DemandsDeserializer::CE_DEMANDS;

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
            auto value = el.second;

            if (!value.is_string())
            {
                r.add_generic_error(type_name(), "value of [\"", el.first, "\"] must be a string");
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
            const auto errors_count = r.errors();
            if (r.optional_object_field(obj, key, value, string_deserializer))
            {
                if (errors_count != r.errors()) return;
                put_into.insert_or_replace(key.to_string(), Json::Value::string(value));
            }
        };
        auto extract_object = [&](const Json::Object& obj, StringView key, Json::Object& put_into) {
            if (auto value = obj.get(key))
            {
                if (!value->is_object())
                {
                    r.add_generic_error(key, "expected an object");
                }
                else
                {
                    put_into.insert_or_replace(key.to_string(), *value);
                }
            }
        };
        auto extract_dictionary = [&](const Json::Object& obj, StringView key, Json::Object& put_into) {
            Json::Object value;
            const auto errors_count = r.errors();
            if (r.optional_object_field(obj, key, value, DictionaryDeserializer::instance))
            {
                if (errors_count != r.errors()) return;
                put_into.insert_or_replace(key.to_string(), value);
            }
        };

        Json::Object ret;
        for (const auto& el : obj)
        {
            auto&& key = el.first;
            if (Util::find(Configuration::known_fields(), key) == std::end(Configuration::known_fields()))
            {
                ret.insert_or_replace(key.to_string(), el.second);
            }
        }
        extract_string(obj, CE_ERROR, ret);
        extract_string(obj, CE_WARNING, ret);
        extract_string(obj, CE_MESSAGE, ret);
        extract_object(obj, CE_APPLY, ret);
        extract_object(obj, CE_SETTINGS, ret);
        extract_dictionary(obj, CE_REQUIRES, ret);
        extract_dictionary(obj, CE_SEE_ALSO, ret);
        return ret;
    }

    Optional<Json::Object> DemandsDeserializer::visit_object(Json::Reader& r, const Json::Object& obj)
    {
        Json::Object ret;
        for (const auto& el : obj)
        {
            auto key = el.first.to_string();
            if (Strings::starts_with(key, "$"))
            {
                // Put comments back without attempting to parse.
                ret.insert_or_replace(key, el.second);
                continue;
            }

            if (!el.second.is_object())
            {
                r.add_generic_error(type_name(), "value of [\"", key, "\"] must be an object");
                continue;
            }

            const auto& demand_obj = el.second.object();
            if (demand_obj.contains(CE_DEMANDS))
            {
                r.add_generic_error(type_name(),
                                    "$.demands.[\"",
                                    key,
                                    "\"] contains a `demands` object (nested `demands` have no effect)");
            }

            auto maybe_demand = r.visit(demand_obj, CeMetadataDeserializer::instance);
            if (maybe_demand.has_value())
            {
                ret.insert_or_replace(key, maybe_demand.value_or_exit(VCPKG_LINE_INFO));
            }
        }
        return ret;
    }

    Optional<Configuration> ConfigurationDeserializer::visit_object(Json::Reader& r, const Json::Object& obj)
    {
        static const StringView ARTIFACT = "artifact";

        Json::Object extra_info;

        std::vector<std::string> comment_keys;
        for (const auto& el : obj)
        {
            if (Strings::starts_with(el.first, "$"))
            {
                auto key = el.first.to_string();
                extra_info.insert_or_replace(key, el.second);
                comment_keys.push_back(key);
            }
        }

        RegistrySet registries;

        auto impl_des = get_registry_implementation_deserializer(configuration_directory);

        std::unique_ptr<RegistryImplementation> default_registry;
        if (r.optional_object_field(obj, DEFAULT_REGISTRY, default_registry, *impl_des))
        {
            if (default_registry && default_registry->kind() == ARTIFACT)
            {
                r.add_generic_error(type_name(), DEFAULT_REGISTRY, " cannot be of \"", ARTIFACT, "\" kind");
            }
            registries.set_default_registry(std::move(default_registry));
        }

        auto reg_des = get_registry_array_deserializer(configuration_directory);
        std::vector<Registry> regs;
        r.optional_object_field(obj, REGISTRIES, regs, *reg_des);

        for (Registry& reg : regs)
        {
            registries.add_registry(std::move(reg));
        }

        Json::Object ce_metadata_obj;
        auto maybe_ce_metadata = r.visit(obj, CeMetadataDeserializer::instance);
        if (maybe_ce_metadata.has_value())
        {
            ce_metadata_obj = maybe_ce_metadata.value_or_exit(VCPKG_LINE_INFO);
        }

        Json::Object demands_obj;
        if (r.optional_object_field(obj, DemandsDeserializer::CE_DEMANDS, demands_obj, DemandsDeserializer::instance))
        {
            ce_metadata_obj.insert_or_replace(DemandsDeserializer::CE_DEMANDS, demands_obj);
        }

        // Remove comments duplicated in ce_metadata
        for (auto&& comment_key : comment_keys)
        {
            ce_metadata_obj.remove(comment_key);
        }

        return Configuration{std::move(registries), ce_metadata_obj, extra_info};
    }

    ConfigurationDeserializer::ConfigurationDeserializer(const Path& configuration_directory)
        : configuration_directory(configuration_directory)
    {
    }

    static void serialize_ce_metadata(const Json::Object& ce_metadata, Json::Object& put_into)
    {
        auto extract_object = [](const Json::Object& obj, StringView key, Json::Object& put_into) {
            if (auto value = obj.get(key))
            {
                put_into.insert_or_replace(key.to_string(), *value);
            }
        };

        auto serialize_demands = [](const Json::Object& obj, Json::Object& put_into) {
            if (auto demands = obj.get(DemandsDeserializer::CE_DEMANDS))
            {
                if (!demands->is_object())
                {
                    return;
                }

                Json::Object serialized_demands;
                for (const auto& el : demands->object())
                {
                    auto key = el.first.to_string();
                    if (Strings::starts_with(key, "$"))
                    {
                        serialized_demands.insert_or_replace(key, el.second);
                        continue;
                    }

                    if (el.second.is_object())
                    {
                        auto& inserted = serialized_demands.insert_or_replace(key, Json::Object{});
                        serialize_ce_metadata(el.second.object(), inserted);
                    }
                }
                put_into.insert_or_replace(DemandsDeserializer::CE_DEMANDS, serialized_demands);
            }
        };

        // Unknown fields are left as-is
        for (const auto& el : ce_metadata)
        {
            if (Util::find(Configuration::known_fields(), el.first) == std::end(Configuration::known_fields()))
            {
                put_into.insert_or_replace(el.first.to_string(), el.second);
            }
        }

        extract_object(ce_metadata, CeMetadataDeserializer::CE_MESSAGE, put_into);
        extract_object(ce_metadata, CeMetadataDeserializer::CE_WARNING, put_into);
        extract_object(ce_metadata, CeMetadataDeserializer::CE_ERROR, put_into);
        extract_object(ce_metadata, CeMetadataDeserializer::CE_SETTINGS, put_into);
        extract_object(ce_metadata, CeMetadataDeserializer::CE_APPLY, put_into);
        extract_object(ce_metadata, CeMetadataDeserializer::CE_REQUIRES, put_into);
        extract_object(ce_metadata, CeMetadataDeserializer::CE_SEE_ALSO, put_into);
        serialize_demands(ce_metadata, put_into);
    }

    static Json::Object serialize_configuration_impl(const Configuration& config)
    {
        constexpr static StringLiteral REGISTRY_PACKAGES = "packages";

        Json::Object obj;

        for (const auto& el : config.extra_info)
        {
            obj.insert(el.first.to_string(), el.second);
        }

        if (!config.registry_set.is_default_builtin_registry())
        {
            if (auto default_registry = config.registry_set.default_registry())
            {
                obj.insert(ConfigurationDeserializer::DEFAULT_REGISTRY, default_registry->serialize());
            }
            else
            {
                obj.insert(ConfigurationDeserializer::DEFAULT_REGISTRY, Json::Value::null(nullptr));
            }
        }

        auto reg_view = config.registry_set.registries();
        if (reg_view.size() > 0)
        {
            auto& reg_arr = obj.insert(ConfigurationDeserializer::REGISTRIES, Json::Array());
            for (const auto& reg : reg_view)
            {
                auto reg_obj = reg.implementation().serialize();
                if (reg.packages().size())
                {
                    auto& packages = reg_obj.insert(REGISTRY_PACKAGES, Json::Array{});
                    for (const auto& pkg : reg.packages())
                    {
                        packages.push_back(Json::Value::string(pkg));
                    }
                }
                reg_arr.push_back(std::move(reg_obj));
            }
        }

        if (!config.ce_metadata.is_empty())
        {
            serialize_ce_metadata(config.ce_metadata, obj);
        }

        return obj;
    }

    static void find_unknown_fields_impl(const Json::Object& obj, std::vector<std::string>& out, StringView path)
    {
        std::vector<StringView> ret;
        for (const auto& el : obj)
        {
            auto key = el.first.to_string();
            if (Strings::starts_with(key, "$"))
            {
                continue;
            }

            if (Util::find(Configuration::known_fields(), key) == std::end(Configuration::known_fields()))
            {
                if (Strings::contains(key, " "))
                {
                    key = Strings::concat("[\"", key, "\"]");
                }
                out.push_back(Strings::concat(path, ".", key));
            }

            if (el.first == DemandsDeserializer::CE_DEMANDS)
            {
                if (!el.second.is_object())
                {
                    continue;
                }

                for (const auto& demand : el.second.object())
                {
                    if (Strings::starts_with(demand.first, "$"))
                    {
                        continue;
                    }

                    find_unknown_fields_impl(
                        demand.second.object(),
                        out,
                        Strings::concat(path, ".", DemandsDeserializer::CE_DEMANDS, ".", demand.first));
                }
            }
        }
    }
}

namespace vcpkg
{
    View<StringView> Configuration::known_fields()
    {
        static constexpr StringView known_fields[]{
            ConfigurationDeserializer::DEFAULT_REGISTRY,
            ConfigurationDeserializer::REGISTRIES,
            CeMetadataDeserializer::CE_MESSAGE,
            CeMetadataDeserializer::CE_WARNING,
            CeMetadataDeserializer::CE_ERROR,
            CeMetadataDeserializer::CE_SETTINGS,
            CeMetadataDeserializer::CE_APPLY,
            CeMetadataDeserializer::CE_REQUIRES,
            CeMetadataDeserializer::CE_SEE_ALSO,
            DemandsDeserializer::CE_DEMANDS,
        };
        return known_fields;
    }

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

        if (!ce_metadata.is_empty())
        {
            auto unknown_fields = find_unknown_fields(*this);
            if (!unknown_fields.empty())
            {
                vcpkg::print2(
                    Color::warning,
                    "Warning: configuration contains the following unrecognized fields:\n\n",
                    Strings::join("\n", unknown_fields),
                    "\n\nIf these are documented fields that should be recognized try updating the vcpkg tool.\n");
            }
        }
    }

    bool Configuration::requests_configure_environment() const
    {
        return registry_set.contains_artifact_registries() || !ce_metadata.is_empty();
    }

    std::unique_ptr<Json::IDeserializer<Configuration>> make_configuration_deserializer(const Path& config_directory)
    {
        return std::make_unique<ConfigurationDeserializer>(config_directory);
    }

    Json::Object serialize_configuration(const Configuration& config) { return serialize_configuration_impl(config); }

    std::vector<std::string> find_unknown_fields(const Configuration& config)
    {
        std::vector<std::string> out;
        find_unknown_fields_impl(config.ce_metadata, out, "$");
        return out;
    }
}
