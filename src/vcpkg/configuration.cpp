#include <vcpkg/base/jsonreader.h>
#include <vcpkg/base/system.print.h>

#include <vcpkg/configuration.h>
#include <vcpkg/metrics.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

namespace
{
    using namespace vcpkg;

    struct RegistryConfigDeserializer : Json::IDeserializer<RegistryConfig>
    {
        constexpr static StringLiteral KIND = "kind";
        constexpr static StringLiteral BASELINE = "baseline";
        constexpr static StringLiteral PATH = "path";
        constexpr static StringLiteral REPO = "repository";
        constexpr static StringLiteral REFERENCE = "reference";
        constexpr static StringLiteral NAME = "name";
        constexpr static StringLiteral LOCATION = "location";

        constexpr static StringLiteral KIND_BUILTIN = "builtin";
        constexpr static StringLiteral KIND_FILESYSTEM = "filesystem";
        constexpr static StringLiteral KIND_GIT = "git";
        constexpr static StringLiteral KIND_ARTIFACT = "artifact";

        virtual StringView type_name() const override { return "a registry"; }
        virtual View<StringView> valid_fields() const override;

        virtual Optional<RegistryConfig> visit_null(Json::Reader&) override;
        virtual Optional<RegistryConfig> visit_object(Json::Reader&, const Json::Object&) override;

        static RegistryConfigDeserializer instance;
    };
    RegistryConfigDeserializer RegistryConfigDeserializer::instance;
    constexpr StringLiteral RegistryConfigDeserializer::KIND;
    constexpr StringLiteral RegistryConfigDeserializer::BASELINE;
    constexpr StringLiteral RegistryConfigDeserializer::PATH;
    constexpr StringLiteral RegistryConfigDeserializer::REPO;
    constexpr StringLiteral RegistryConfigDeserializer::REFERENCE;
    constexpr StringLiteral RegistryConfigDeserializer::NAME;
    constexpr StringLiteral RegistryConfigDeserializer::LOCATION;
    constexpr StringLiteral RegistryConfigDeserializer::KIND_BUILTIN;
    constexpr StringLiteral RegistryConfigDeserializer::KIND_FILESYSTEM;
    constexpr StringLiteral RegistryConfigDeserializer::KIND_GIT;
    constexpr StringLiteral RegistryConfigDeserializer::KIND_ARTIFACT;

    struct RegistryDeserializer final : Json::IDeserializer<RegistryConfig>
    {
        constexpr static StringLiteral PACKAGES = "packages";

        virtual StringView type_name() const override { return "a registry"; }
        virtual View<StringView> valid_fields() const override;

        virtual Optional<RegistryConfig> visit_object(Json::Reader&, const Json::Object&) override;

        static RegistryDeserializer instance;
    };
    RegistryDeserializer RegistryDeserializer::instance;
    constexpr StringLiteral RegistryDeserializer::PACKAGES;

    View<StringView> RegistryConfigDeserializer::valid_fields() const
    {
        static const StringView t[] = {KIND, BASELINE, PATH, REPO, REFERENCE, NAME, LOCATION};
        return t;
    }
    View<StringView> valid_builtin_fields()
    {
        static const StringView t[] = {
            RegistryConfigDeserializer::KIND,
            RegistryConfigDeserializer::BASELINE,
            RegistryDeserializer::PACKAGES,
        };
        return t;
    }
    View<StringView> valid_filesystem_fields()
    {
        static const StringView t[] = {
            RegistryConfigDeserializer::KIND,
            RegistryConfigDeserializer::BASELINE,
            RegistryConfigDeserializer::PATH,
            RegistryDeserializer::PACKAGES,
        };
        return t;
    }
    View<StringView> valid_git_fields()
    {
        static const StringView t[] = {
            RegistryConfigDeserializer::KIND,
            RegistryConfigDeserializer::BASELINE,
            RegistryConfigDeserializer::REPO,
            RegistryConfigDeserializer::REFERENCE,
            RegistryDeserializer::PACKAGES,
        };
        return t;
    }
    View<StringView> valid_artifact_fields()
    {
        static const StringView t[] = {
            RegistryConfigDeserializer::KIND,
            RegistryConfigDeserializer::NAME,
            RegistryConfigDeserializer::LOCATION,
        };
        return t;
    }

    Optional<RegistryConfig> RegistryConfigDeserializer::visit_null(Json::Reader&) { return RegistryConfig(); }

    Optional<RegistryConfig> RegistryConfigDeserializer::visit_object(Json::Reader& r, const Json::Object& obj)
    {
        static Json::StringDeserializer kind_deserializer{"a registry implementation kind"};
        static Json::StringDeserializer baseline_deserializer{"a baseline"};

        RegistryConfig res;
        res.json_document_path = r.path();

        auto& kind = res.kind.emplace();
        r.required_object_field(type_name(), obj, KIND, kind, kind_deserializer);

        if (kind == KIND_BUILTIN)
        {
            auto& baseline = res.baseline.emplace();
            r.required_object_field("a builtin registry", obj, BASELINE, baseline, baseline_deserializer);
            r.check_for_unexpected_fields(obj, valid_builtin_fields(), "a builtin registry");
        }
        else if (kind == KIND_FILESYSTEM)
        {
            std::string baseline;
            if (r.optional_object_field(obj, BASELINE, baseline, baseline_deserializer))
            {
                res.baseline = std::move(baseline);
            }

            r.required_object_field(
                "a filesystem registry", obj, PATH, res.path.emplace(), Json::PathDeserializer::instance);

            r.check_for_unexpected_fields(obj, valid_filesystem_fields(), "a filesystem registry");
        }
        else if (kind == KIND_GIT)
        {
            static Json::StringDeserializer repo_des{"a git repository URL"};
            r.required_object_field("a git registry", obj, REPO, res.repo.emplace(), repo_des);

            static Json::StringDeserializer ref_des{"a git reference (for example, a branch)"};
            if (!r.optional_object_field(obj, REFERENCE, res.reference.emplace(), ref_des))
            {
                res.reference = nullopt;
            }

            r.required_object_field("a git registry", obj, BASELINE, res.baseline.emplace(), baseline_deserializer);

            r.check_for_unexpected_fields(obj, valid_git_fields(), "a git registry");
        }
        else if (kind == KIND_ARTIFACT)
        {
            r.required_object_field(
                "an artifacts registry", obj, NAME, res.name.emplace(), Json::IdentifierDeserializer::instance);

            static Json::StringDeserializer location_des{"an artifacts git repository URL"};
            r.required_object_field("an artifacts registry", obj, LOCATION, res.location.emplace(), location_des);

            r.check_for_unexpected_fields(obj, valid_artifact_fields(), "an artifacts registry");
        }
        else
        {
            StringLiteral valid_kinds[] = {KIND_BUILTIN, KIND_FILESYSTEM, KIND_GIT, KIND_ARTIFACT};
            r.add_generic_error(type_name(),
                                "Field \"kind\" did not have an expected value (expected one of: \"",
                                Strings::join("\", \"", valid_kinds),
                                "\"; found \"",
                                kind,
                                "\")");
            return nullopt;
        }

        return std::move(res); // gcc-7 bug workaround redundant move
    }

    View<StringView> RegistryDeserializer::valid_fields() const
    {
        static const StringView t[] = {
            RegistryConfigDeserializer::KIND,
            RegistryConfigDeserializer::BASELINE,
            RegistryConfigDeserializer::PATH,
            RegistryConfigDeserializer::REPO,
            RegistryConfigDeserializer::REFERENCE,
            RegistryConfigDeserializer::NAME,
            RegistryConfigDeserializer::LOCATION,
            PACKAGES,
        };
        return t;
    }

    Optional<RegistryConfig> RegistryDeserializer::visit_object(Json::Reader& r, const Json::Object& obj)
    {
        auto impl = RegistryConfigDeserializer::instance.visit_object(r, obj);

        if (auto config = impl.get())
        {
            static Json::ArrayDeserializer<Json::PackageNameDeserializer> package_names_deserializer{
                "an array of package names"};

            if (config->kind && *config->kind.get() != RegistryConfigDeserializer::KIND_ARTIFACT)
            {
                r.required_object_field(
                    type_name(), obj, PACKAGES, config->packages.emplace(), package_names_deserializer);
            }
        }
        return impl;
    }

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
        constexpr static StringLiteral OVERLAY_PORTS = "overlay-ports";
        constexpr static StringLiteral OVERLAY_TRIPLETS = "overlay-triplets";

        virtual Optional<Configuration> visit_object(Json::Reader& r, const Json::Object& obj) override;

        static ConfigurationDeserializer instance;
    };
    ConfigurationDeserializer ConfigurationDeserializer::instance;
    constexpr StringLiteral ConfigurationDeserializer::DEFAULT_REGISTRY;
    constexpr StringLiteral ConfigurationDeserializer::REGISTRIES;
    constexpr StringLiteral ConfigurationDeserializer::OVERLAY_PORTS;
    constexpr StringLiteral ConfigurationDeserializer::OVERLAY_TRIPLETS;

    Optional<Json::Object> DictionaryDeserializer::visit_object(Json::Reader& r, const Json::Object& obj)
    {
        Json::Object ret;
        for (const auto& el : obj)
        {
            if (!el.second.is_string())
            {
                r.add_generic_error(type_name(), "value of [\"", el.first, "\"] must be a string");
                continue;
            }

            ret.insert_or_replace(el.first, el.second);
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
                put_into.insert_or_replace(key, std::move(value));
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
                    put_into.insert_or_replace(key, *value);
                }
            }
        };
        auto extract_dictionary = [&](const Json::Object& obj, StringView key, Json::Object& put_into) {
            Json::Object value;
            const auto errors_count = r.errors();
            if (r.optional_object_field(obj, key, value, DictionaryDeserializer::instance))
            {
                if (errors_count != r.errors()) return;
                put_into.insert_or_replace(key, value);
            }
        };

        Json::Object ret;
        for (const auto& el : obj)
        {
            auto&& key = el.first;
            if (Util::find(Configuration::known_fields(), key) == std::end(Configuration::known_fields()))
            {
                ret.insert_or_replace(key, el.second);
            }
        }
        extract_string(obj, CE_ERROR, ret);
        extract_string(obj, CE_WARNING, ret);
        extract_string(obj, CE_MESSAGE, ret);
        extract_object(obj, CE_APPLY, ret);
        extract_object(obj, CE_SETTINGS, ret);
        extract_dictionary(obj, CE_REQUIRES, ret);
        return ret;
    }

    Optional<Json::Object> DemandsDeserializer::visit_object(Json::Reader& r, const Json::Object& obj)
    {
        Json::Object ret;
        for (const auto& el : obj)
        {
            const auto key = el.first;
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

            const auto& demand_obj = el.second.object(VCPKG_LINE_INFO);
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
        Configuration ret;
        Json::Object& extra_info = ret.extra_info;

        std::vector<std::string> comment_keys;
        for (const auto& el : obj)
        {
            if (Strings::starts_with(el.first, "$"))
            {
                extra_info.insert_or_replace(el.first, el.second);
                comment_keys.emplace_back(el.first);
            }
        }

        static Json::ArrayDeserializer<Json::StringDeserializer> op_des("an array of overlay ports paths",
                                                                        Json::StringDeserializer{"an overlay path"});
        r.optional_object_field(obj, OVERLAY_PORTS, ret.overlay_ports, op_des);

        static Json::ArrayDeserializer<Json::StringDeserializer> ot_des("an array of overlay triplets paths",
                                                                        Json::StringDeserializer{"a triplet path"});
        r.optional_object_field(obj, OVERLAY_TRIPLETS, ret.overlay_triplets, ot_des);

        RegistryConfig default_registry;
        if (r.optional_object_field(obj, DEFAULT_REGISTRY, default_registry, RegistryConfigDeserializer::instance))
        {
            if (default_registry.kind.value_or("") == RegistryConfigDeserializer::KIND_ARTIFACT)
            {
                r.add_generic_error(type_name(),
                                    DEFAULT_REGISTRY,
                                    " cannot be of kind \"",
                                    RegistryConfigDeserializer::KIND_ARTIFACT,
                                    "\"");
            }
            ret.default_reg = std::move(default_registry);
        }

        static Json::ArrayDeserializer<RegistryDeserializer> regs_des("an array of registries");
        r.optional_object_field(obj, REGISTRIES, ret.registries, regs_des);

        Json::Object& ce_metadata_obj = ret.ce_metadata;
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

        return std::move(ret);
    }

    static void serialize_ce_metadata(const Json::Object& ce_metadata, Json::Object& put_into)
    {
        auto extract_object = [](const Json::Object& obj, StringView key, Json::Object& put_into) {
            if (auto value = obj.get(key))
            {
                put_into.insert_or_replace(key, *value);
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
                for (const auto& el : demands->object(VCPKG_LINE_INFO))
                {
                    auto key = el.first;
                    if (Strings::starts_with(key, "$"))
                    {
                        serialized_demands.insert_or_replace(key, el.second);
                        continue;
                    }

                    if (el.second.is_object())
                    {
                        auto& inserted = serialized_demands.insert_or_replace(key, Json::Object{});
                        serialize_ce_metadata(el.second.object(VCPKG_LINE_INFO), inserted);
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
                put_into.insert_or_replace(el.first, el.second);
            }
        }

        extract_object(ce_metadata, CeMetadataDeserializer::CE_MESSAGE, put_into);
        extract_object(ce_metadata, CeMetadataDeserializer::CE_WARNING, put_into);
        extract_object(ce_metadata, CeMetadataDeserializer::CE_ERROR, put_into);
        extract_object(ce_metadata, CeMetadataDeserializer::CE_SETTINGS, put_into);
        extract_object(ce_metadata, CeMetadataDeserializer::CE_APPLY, put_into);
        extract_object(ce_metadata, CeMetadataDeserializer::CE_REQUIRES, put_into);
        serialize_demands(ce_metadata, put_into);
    }

    static void find_unknown_fields_impl(const Json::Object& obj, std::vector<std::string>& out, StringView path)
    {
        std::vector<StringView> ret;
        for (const auto& el : obj)
        {
            auto key = el.first;
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

                for (const auto& demand : el.second.object(VCPKG_LINE_INFO))
                {
                    if (Strings::starts_with(demand.first, "$"))
                    {
                        continue;
                    }

                    find_unknown_fields_impl(
                        demand.second.object(VCPKG_LINE_INFO),
                        out,
                        Strings::concat(path, ".", DemandsDeserializer::CE_DEMANDS, ".", demand.first));
                }
            }
        }
    }
}

namespace vcpkg
{
    static ExpectedL<Optional<std::string>> get_baseline_from_git_repo(const VcpkgPaths& paths, StringView url)
    {
        auto res = paths.git_fetch_from_remote_registry(url, "HEAD");
        if (auto p = res.get())
        {
            return Optional<std::string>(std::move(*p));
        }
        else
        {
            return msg::format(msgUpdateBaselineRemoteGitError, msg::url = url)
                .append_raw('\n')
                .append_raw(Strings::trim(res.error()));
        }
    }

    ExpectedL<Optional<std::string>> RegistryConfig::get_latest_baseline(const VcpkgPaths& paths) const
    {
        if (kind == RegistryConfigDeserializer::KIND_GIT)
        {
            return get_baseline_from_git_repo(paths, repo.value_or_exit(VCPKG_LINE_INFO));
        }
        else if (kind == RegistryConfigDeserializer::KIND_BUILTIN)
        {
            if (paths.use_git_default_registry())
            {
                return get_baseline_from_git_repo(paths, builtin_registry_git_url);
            }
            else
            {
                // use the vcpkg git repository sha from the user's machine
                auto res = paths.get_current_git_sha();
                if (auto p = res.get())
                {
                    return Optional<std::string>(std::move(*p));
                }
                else
                {
                    return msg::format(msgUpdateBaselineLocalGitError, msg::path = paths.root)
                        .append_raw('\n')
                        .append_raw(Strings::trim(res.error()));
                }
            }
        }
        else
        {
            return baseline;
        }
    }

    StringView RegistryConfig::pretty_location() const
    {
        if (kind == RegistryConfigDeserializer::KIND_BUILTIN)
        {
            return builtin_registry_git_url;
        }
        if (kind == RegistryConfigDeserializer::KIND_FILESYSTEM)
        {
            return path.value_or_exit(VCPKG_LINE_INFO);
        }
        if (kind == RegistryConfigDeserializer::KIND_GIT)
        {
            return repo.value_or_exit(VCPKG_LINE_INFO);
        }
        if (kind == RegistryConfigDeserializer::KIND_ARTIFACT)
        {
            return location.value_or_exit(VCPKG_LINE_INFO);
        }

        Checks::unreachable(VCPKG_LINE_INFO);
    }

    View<StringView> Configuration::known_fields()
    {
        static constexpr StringView known_fields[]{
            ConfigurationDeserializer::DEFAULT_REGISTRY,
            ConfigurationDeserializer::REGISTRIES,
            ConfigurationDeserializer::OVERLAY_PORTS,
            ConfigurationDeserializer::OVERLAY_TRIPLETS,
            CeMetadataDeserializer::CE_MESSAGE,
            CeMetadataDeserializer::CE_WARNING,
            CeMetadataDeserializer::CE_ERROR,
            CeMetadataDeserializer::CE_SETTINGS,
            CeMetadataDeserializer::CE_APPLY,
            CeMetadataDeserializer::CE_REQUIRES,
            DemandsDeserializer::CE_DEMANDS,
        };
        return known_fields;
    }

    void Configuration::validate_as_active() const
    {
        if (!ce_metadata.is_empty())
        {
            auto unknown_fields = find_unknown_fields(*this);
            if (!unknown_fields.empty())
            {
                msg::println_warning(msg::format(msgUnrecognizedConfigField)
                                         .append_raw("\n\n" + Strings::join("\n", unknown_fields))
                                         .append(msgDocumentedFieldsSuggestUpdate));
            }
        }
    }

    static bool registry_config_requests_ce(const RegistryConfig& target) noexcept
    {
        if (auto* kind = target.kind.get())
        {
            if (*kind == "artifact")
            {
                return true;
            }
        }

        return false;
    }

    bool Configuration::requests_ce() const
    {
        if (!ce_metadata.is_empty())
        {
            return true;
        }

        if (const auto* pdefault = default_reg.get())
        {
            if (registry_config_requests_ce(*pdefault))
            {
                return true;
            }
        }

        return std::any_of(registries.begin(), registries.end(), registry_config_requests_ce);
    }

    Json::IDeserializer<Configuration>& get_configuration_deserializer() { return ConfigurationDeserializer::instance; }

    static std::unique_ptr<RegistryImplementation> instantiate_rconfig(const VcpkgPaths& paths,
                                                                       const RegistryConfig& config,
                                                                       const Path& config_dir)
    {
        if (auto k = config.kind.get())
        {
            if (*k == RegistryConfigDeserializer::KIND_BUILTIN)
            {
                return make_builtin_registry(paths,
                                             config.baseline.value_or_exit(VCPKG_LINE_INFO),
                                             config.name.value_or(config.json_document_path));
            }
            else if (*k == RegistryConfigDeserializer::KIND_GIT)
            {
                return make_git_registry(paths,
                                         config.repo.value_or_exit(VCPKG_LINE_INFO),
                                         config.reference.value_or("HEAD"),
                                         config.baseline.value_or_exit(VCPKG_LINE_INFO),
                                         config.name.value_or(config.json_document_path));
            }
            else if (*k == RegistryConfigDeserializer::KIND_FILESYSTEM)
            {
                return make_filesystem_registry(paths.get_filesystem(),
                                                config_dir / config.path.value_or_exit(VCPKG_LINE_INFO),
                                                config.baseline.value_or(""),
                                                config.name.value_or(config.json_document_path));
            }
            else
            {
                Checks::unreachable(VCPKG_LINE_INFO);
            }
        }
        else
        {
            return nullptr;
        }
    }

    std::unique_ptr<RegistrySet> Configuration::instantiate_registry_set(const VcpkgPaths& paths,
                                                                         const Path& config_dir) const
    {
        std::vector<Registry> r_impls;
        for (auto&& reg : registries)
        {
            // packages will be null for artifact registries
            if (auto p = reg.packages.get())
            {
                r_impls.emplace_back(std::vector<std::string>(*p), instantiate_rconfig(paths, reg, config_dir));
            }
        }
        auto reg1 =
            default_reg ? instantiate_rconfig(paths, *default_reg.get(), config_dir) : make_builtin_registry(paths);
        return std::make_unique<RegistrySet>(std::move(reg1), std::move(r_impls));
    }

    Json::Object Configuration::serialize() const
    {
        Json::Object obj;

        for (const auto& el : extra_info)
        {
            obj.insert(el.first, el.second);
        }

        if (auto default_registry = default_reg.get())
        {
            obj.insert(ConfigurationDeserializer::DEFAULT_REGISTRY, default_registry->serialize());
        }

        if (!registries.empty())
        {
            auto& reg_arr = obj.insert(ConfigurationDeserializer::REGISTRIES, Json::Array());
            for (const auto& reg : registries)
            {
                reg_arr.push_back(reg.serialize());
            }
        }

        if (!overlay_ports.empty())
        {
            auto& op_arr = obj.insert(ConfigurationDeserializer::OVERLAY_PORTS, Json::Array());
            for (const auto& port : overlay_ports)
            {
                op_arr.push_back(port);
            }
        }

        if (!overlay_triplets.empty())
        {
            auto& ot_arr = obj.insert(ConfigurationDeserializer::OVERLAY_TRIPLETS, Json::Array());
            for (const auto& triplet : overlay_triplets)
            {
                ot_arr.push_back(triplet);
            }
        }

        if (!ce_metadata.is_empty())
        {
            serialize_ce_metadata(ce_metadata, obj);
        }

        return obj;
    }

    Json::Value RegistryConfig::serialize() const
    {
        if (!kind)
        {
            return Json::Value::null(nullptr);
        }
        Json::Object obj;
        obj.insert(RegistryConfigDeserializer::KIND, Json::Value::string(*kind.get()));
        if (auto p = baseline.get()) obj.insert(RegistryConfigDeserializer::BASELINE, Json::Value::string(*p));
        if (auto p = location.get()) obj.insert(RegistryConfigDeserializer::LOCATION, Json::Value::string(*p));
        if (auto p = name.get()) obj.insert(RegistryConfigDeserializer::NAME, Json::Value::string(*p));
        if (auto p = path.get()) obj.insert(RegistryConfigDeserializer::PATH, Json::Value::string(p->native()));
        if (auto p = reference.get()) obj.insert(RegistryConfigDeserializer::REFERENCE, Json::Value::string(*p));
        if (auto p = repo.get()) obj.insert(RegistryConfigDeserializer::REPO, Json::Value::string(*p));
        if (packages)
        {
            auto& arr = obj.insert(RegistryDeserializer::PACKAGES, Json::Array());
            for (auto&& p : *packages.get())
            {
                arr.push_back(Json::Value::string(p));
            }
        }
        return Json::Value::object(std::move(obj));
    }

    std::vector<std::string> find_unknown_fields(const Configuration& config)
    {
        std::vector<std::string> out;
        find_unknown_fields_impl(config.ce_metadata, out, "$");
        return out;
    }
}
