#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/jsonreader.h>
#include <vcpkg/base/message_sinks.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/util.h>

#include <vcpkg/configuration.h>
#include <vcpkg/documentation.h>
#include <vcpkg/vcpkgpaths.h>

namespace
{
    using namespace vcpkg;

    /// <summary>
    /// Deserializes a list of package names and patterns along with their respective in-file declaration locations.
    /// </summary>
    struct PackagePatternDeserializer final : Json::IDeserializer<PackagePatternDeclaration>
    {
        virtual LocalizedString type_name() const override;
        virtual Optional<PackagePatternDeclaration> visit_string(Json::Reader&, StringView sv) const override;

        static const PackagePatternDeserializer instance;
    };

    struct PackagePatternArrayDeserializer final : Json::ArrayDeserializer<PackagePatternDeserializer>
    {
        virtual LocalizedString type_name() const override;
        static const PackagePatternArrayDeserializer instance;
    };
    LocalizedString PackagePatternDeserializer::type_name() const { return msg::format(msgAPackagePattern); }

    Optional<PackagePatternDeclaration> PackagePatternDeserializer::visit_string(Json::Reader& r, StringView sv) const
    {
        if (!is_package_pattern(sv))
        {
            r.add_generic_error(
                type_name(),
                msg::format(msgParsePackagePatternError, msg::package_name = sv, msg::url = docs::registries_url));
        }

        return PackagePatternDeclaration{
            sv.to_string(),
            r.path(),
        };
    }

    const PackagePatternDeserializer PackagePatternDeserializer::instance;

    LocalizedString PackagePatternArrayDeserializer::type_name() const { return msg::format(msgAPackagePatternArray); }

    const PackagePatternArrayDeserializer PackagePatternArrayDeserializer::instance;
    struct RegistryImplementationKindDeserializer : Json::StringDeserializer
    {
        LocalizedString type_name() const override { return msg::format(msgARegistryImplementationKind); }

        static const RegistryImplementationKindDeserializer instance;
    };

    const RegistryImplementationKindDeserializer RegistryImplementationKindDeserializer::instance;

    struct BaselineShaDeserializer : Json::StringDeserializer
    {
        LocalizedString type_name() const override { return msg::format(msgABaseline); }

        static const BaselineShaDeserializer instance;
    };

    const BaselineShaDeserializer BaselineShaDeserializer::instance;

    struct GitUrlDeserializer : Json::StringDeserializer
    {
        LocalizedString type_name() const override { return msg::format(msgAGitRepositoryUrl); }

        static const GitUrlDeserializer instance;
    };

    const GitUrlDeserializer GitUrlDeserializer::instance;

    struct GitReferenceDeserializer : Json::StringDeserializer
    {
        LocalizedString type_name() const override { return msg::format(msgAGitReference); }

        static const GitReferenceDeserializer instance;
    };

    const GitReferenceDeserializer GitReferenceDeserializer::instance;

    struct ArtifactsGitRegistryUrlDeserializer : Json::StringDeserializer
    {
        LocalizedString type_name() const override { return msg::format(msgAnArtifactsGitRegistryUrl); }

        static const ArtifactsGitRegistryUrlDeserializer instance;
    };

    const ArtifactsGitRegistryUrlDeserializer ArtifactsGitRegistryUrlDeserializer::instance;

    struct RegistryConfigDeserializer final : Json::IDeserializer<RegistryConfig>
    {
        virtual LocalizedString type_name() const override { return msg::format(msgARegistry); }
        virtual View<StringLiteral> valid_fields() const noexcept override;

        virtual Optional<RegistryConfig> visit_null(Json::Reader&) const override;
        virtual Optional<RegistryConfig> visit_object(Json::Reader&, const Json::Object&) const override;

        static const RegistryConfigDeserializer instance;
    };
    const RegistryConfigDeserializer RegistryConfigDeserializer::instance;

    struct OverlayPathStringDeserializer final : Json::StringDeserializer
    {
        LocalizedString type_name() const override { return msg::format(msgAnOverlayPath); }

        static const OverlayPathStringDeserializer instance;
    };

    const OverlayPathStringDeserializer OverlayPathStringDeserializer::instance;

    struct OverlayPathArrayDeserializer final : Json::ArrayDeserializer<OverlayPathStringDeserializer>
    {
        LocalizedString type_name() const override { return msg::format(msgAnArrayOfOverlayPaths); }

        static const OverlayPathArrayDeserializer instance;
    };

    const OverlayPathArrayDeserializer OverlayPathArrayDeserializer::instance;

    struct OverlayTripletsPathStringDeserializer final : Json::StringDeserializer
    {
        LocalizedString type_name() const override { return msg::format(msgAnOverlayTripletsPath); }

        static const OverlayTripletsPathStringDeserializer instance;
    };

    const OverlayTripletsPathStringDeserializer OverlayTripletsPathStringDeserializer::instance;

    struct OverlayTripletsPathArrayDeserializer final : Json::ArrayDeserializer<OverlayTripletsPathStringDeserializer>
    {
        LocalizedString type_name() const override { return msg::format(msgAnArrayOfOverlayTripletsPaths); }

        static const OverlayTripletsPathArrayDeserializer instance;
    };

    const OverlayTripletsPathArrayDeserializer OverlayTripletsPathArrayDeserializer::instance;

    struct RegistryDeserializer final : Json::IDeserializer<RegistryConfig>
    {
        virtual LocalizedString type_name() const override { return msg::format(msgARegistry); }
        virtual View<StringLiteral> valid_fields() const noexcept override;

        virtual Optional<RegistryConfig> visit_object(Json::Reader&, const Json::Object&) const override;

        static RegistryDeserializer instance;
    };
    RegistryDeserializer RegistryDeserializer::instance;

    struct RegistriesArrayDeserializer : Json::ArrayDeserializer<RegistryDeserializer>
    {
        LocalizedString type_name() const override { return msg::format(msgAnArrayOfRegistries); }

        static const RegistriesArrayDeserializer instance;
    };

    const RegistriesArrayDeserializer RegistriesArrayDeserializer::instance;

    View<StringLiteral> RegistryConfigDeserializer::valid_fields() const noexcept
    {
        static constexpr StringLiteral t[] = {
            JsonIdKind,
            JsonIdBaseline,
            JsonIdPath,
            JsonIdRepository,
            JsonIdReference,
            JsonIdName,
            JsonIdLocation,
        };
        return t;
    }
    static constexpr StringLiteral valid_builtin_fields[] = {
        JsonIdKind,
        JsonIdBaseline,
        JsonIdPackages,
    };

    static constexpr StringLiteral valid_filesystem_fields[] = {
        JsonIdKind,
        JsonIdBaseline,
        JsonIdPath,
        JsonIdPackages,
    };

    static constexpr StringLiteral valid_git_fields[] = {
        JsonIdKind,
        JsonIdBaseline,
        JsonIdRepository,
        JsonIdReference,
        JsonIdPackages,
    };

    static constexpr StringLiteral valid_artifact_fields[] = {
        JsonIdKind,
        JsonIdName,
        JsonIdLocation,
    };

    Optional<RegistryConfig> RegistryConfigDeserializer::visit_null(Json::Reader&) const { return RegistryConfig(); }

    Optional<RegistryConfig> RegistryConfigDeserializer::visit_object(Json::Reader& r, const Json::Object& obj) const
    {
        RegistryConfig res;
        auto& kind = res.kind.emplace();
        r.required_object_field(type_name(), obj, JsonIdKind, kind, RegistryImplementationKindDeserializer::instance);

        if (kind == JsonIdBuiltin)
        {
            auto& baseline = res.baseline.emplace();
            r.required_object_field(
                msg::format(msgABuiltinRegistry), obj, JsonIdBaseline, baseline, BaselineShaDeserializer::instance);
            r.check_for_unexpected_fields(obj, valid_builtin_fields, msg::format(msgABuiltinRegistry));
        }
        else if (kind == JsonIdFilesystem)
        {
            r.optional_object_field_emplace(obj, JsonIdBaseline, res.baseline, BaselineShaDeserializer::instance);
            r.required_object_field(msg::format(msgAFilesystemRegistry),
                                    obj,
                                    JsonIdPath,
                                    res.path.emplace(),
                                    Json::PathDeserializer::instance);
            r.check_for_unexpected_fields(obj, valid_filesystem_fields, msg::format(msgAFilesystemRegistry));
        }
        else if (kind == JsonIdGit)
        {
            r.required_object_field(
                msg::format(msgAGitRegistry), obj, JsonIdRepository, res.repo.emplace(), GitUrlDeserializer::instance);
            r.optional_object_field_emplace(obj, JsonIdReference, res.reference, GitReferenceDeserializer::instance);
            r.required_object_field(msg::format(msgAGitRegistry),
                                    obj,
                                    JsonIdBaseline,
                                    res.baseline.emplace(),
                                    BaselineShaDeserializer::instance);
            r.check_for_unexpected_fields(obj, valid_git_fields, msg::format(msgAGitRegistry));
        }
        else if (kind == JsonIdArtifact)
        {
            r.required_object_field(msg::format(msgAnArtifactsRegistry),
                                    obj,
                                    JsonIdName,
                                    res.name.emplace(),
                                    Json::IdentifierDeserializer::instance);
            r.required_object_field(msg::format(msgAnArtifactsRegistry),
                                    obj,
                                    JsonIdLocation,
                                    res.location.emplace(),
                                    ArtifactsGitRegistryUrlDeserializer::instance);
            r.check_for_unexpected_fields(obj, valid_artifact_fields, msg::format(msgAnArtifactsRegistry));
        }
        else
        {
            StringLiteral valid_kinds[] = {JsonIdBuiltin, JsonIdFilesystem, JsonIdGit, JsonIdArtifact};
            r.add_generic_error(type_name(),
                                msg::format(msgFieldKindDidNotHaveExpectedValue,
                                            msg::expected = Strings::join(", ", valid_kinds),
                                            msg::actual = kind));
            return nullopt;
        }

        return std::move(res); // gcc-7 bug workaround redundant move
    }

    View<StringLiteral> RegistryDeserializer::valid_fields() const noexcept
    {
        static constexpr StringLiteral t[] = {
            JsonIdKind,
            JsonIdBaseline,
            JsonIdPath,
            JsonIdRepository,
            JsonIdReference,
            JsonIdName,
            JsonIdLocation,
            JsonIdPackages,
        };
        return t;
    }

    Optional<RegistryConfig> RegistryDeserializer::visit_object(Json::Reader& r, const Json::Object& obj) const
    {
        auto impl = RegistryConfigDeserializer::instance.visit_object(r, obj);

        if (auto config = impl.get())
        {
            if (config->kind && *config->kind.get() != JsonIdArtifact)
            {
                auto& declarations = config->package_declarations.emplace();
                r.required_object_field(
                    type_name(), obj, JsonIdPackages, declarations, PackagePatternArrayDeserializer::instance);
                config->packages.emplace(Util::fmap(declarations, [](auto&& decl) { return decl.pattern; }));
            }
        }
        return impl;
    }

    struct DictionaryDeserializer final : Json::IDeserializer<Json::Object>
    {
        virtual LocalizedString type_name() const override { return msg::format(msgAStringStringDictionary); }

        virtual Optional<Json::Object> visit_object(Json::Reader& r, const Json::Object& obj) const override;

        static const DictionaryDeserializer instance;
    };

    const DictionaryDeserializer DictionaryDeserializer::instance;

    struct CeMetadataDeserializer final : Json::IDeserializer<Json::Object>
    {
        virtual LocalizedString type_name() const override
        {
            return msg::format(msgAnObjectContainingVcpkgArtifactsMetadata);
        }

        virtual Optional<Json::Object> visit_object(Json::Reader& r, const Json::Object& obj) const override;

        static const CeMetadataDeserializer instance;
    };

    const CeMetadataDeserializer CeMetadataDeserializer::instance;

    struct DemandsDeserializer final : Json::IDeserializer<Json::Object>
    {
        virtual LocalizedString type_name() const override { return msg::format(msgADemandObject); }

        virtual Optional<Json::Object> visit_object(Json::Reader& r, const Json::Object& obj) const override;

        static DemandsDeserializer instance;
    };
    DemandsDeserializer DemandsDeserializer::instance;

    struct ConfigurationDeserializer final : Json::IDeserializer<Configuration>
    {
        virtual LocalizedString type_name() const override { return msg::format(msgAConfigurationObject); }

        virtual Optional<Configuration> visit_object(Json::Reader& r, const Json::Object& obj) const override;

        static ConfigurationDeserializer instance;
    };
    ConfigurationDeserializer ConfigurationDeserializer::instance;

    Optional<Json::Object> DictionaryDeserializer::visit_object(Json::Reader& r, const Json::Object& obj) const
    {
        Json::Object ret;
        for (const auto& el : obj)
        {
            if (!el.second.is_string())
            {
                r.add_generic_error(type_name(), msg::format(msgJsonFieldNotString, msg::json_field = el.first));
                continue;
            }

            ret.insert_or_replace(el.first, el.second);
        }
        return ret;
    }

    Optional<Json::Object> CeMetadataDeserializer::visit_object(Json::Reader& r, const Json::Object& obj) const
    {
        auto extract_string = [&](const Json::Object& obj, StringView key, Json::Object& put_into) {
            std::string value;
            const auto errors_count = r.messages().error_count();
            if (r.optional_object_field(obj, key, value, Json::UntypedStringDeserializer::instance))
            {
                if (errors_count != r.messages().error_count()) return;
                put_into.insert_or_replace(key, std::move(value));
            }
        };
        auto extract_object = [&](const Json::Object& obj, StringView key, Json::Object& put_into) {
            if (auto value = obj.get(key))
            {
                if (!value->is_object())
                {
                    r.add_generic_error(LocalizedString::from_raw(key), msg::format(msgExpectedAnObject));
                }
                else
                {
                    put_into.insert_or_replace(key, *value);
                }
            }
        };
        auto extract_dictionary = [&](const Json::Object& obj, StringView key, Json::Object& put_into) {
            Json::Object value;
            const auto errors_count = r.messages().error_count();
            if (r.optional_object_field(obj, key, value, DictionaryDeserializer::instance))
            {
                if (errors_count != r.messages().error_count()) return;
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
        extract_string(obj, JsonIdError, ret);
        extract_string(obj, JsonIdWarning, ret);
        extract_string(obj, JsonIdMessage, ret);
        extract_object(obj, JsonIdApply, ret);
        extract_object(obj, JsonIdSettings, ret);
        extract_dictionary(obj, JsonIdRequires, ret);
        return ret;
    }

    Optional<Json::Object> DemandsDeserializer::visit_object(Json::Reader& r, const Json::Object& obj) const
    {
        Json::Object ret;
        for (const auto& el : obj)
        {
            const auto key = el.first;
            if (key.starts_with("$"))
            {
                // Put comments back without attempting to parse.
                ret.insert_or_replace(key, el.second);
                continue;
            }

            auto maybe_demand_obj = el.second.maybe_object();
            if (!maybe_demand_obj)
            {
                r.add_generic_error(type_name(), msg::format(msgJsonFieldNotObject, msg::json_field = key));
                continue;
            }

            if (maybe_demand_obj->contains(JsonIdDemands))
            {
                r.add_generic_error(type_name(),
                                    msg::format(msgConfigurationNestedDemands, msg::json_field = el.first));
            }

            auto maybe_demand = CeMetadataDeserializer::instance.visit(r, *maybe_demand_obj);
            if (auto demand = maybe_demand.get())
            {
                ret.insert_or_replace(key, *demand);
            }
        }
        return ret;
    }

    LocalizedString& append_declaration_warning(LocalizedString& msg,
                                                StringView location,
                                                StringView registry,
                                                size_t indent_level)
    {
        return msg.append_indent(indent_level)
            .append(msgDuplicatePackagePatternLocation, msg::path = location)
            .append_raw('\n')
            .append_indent(indent_level)
            .append(msgDuplicatePackagePatternRegistry, msg::url = registry)
            .append_raw('\n');
    }

    std::vector<LocalizedString> collect_package_pattern_warnings(const std::vector<RegistryConfig>& registries)
    {
        struct LocationAndRegistry
        {
            StringView location;
            StringView registry;
        };

        // handle warnings from package pattern declarations
        std::map<std::string, std::vector<LocationAndRegistry>> patterns;
        for (auto&& reg : registries)
        {
            if (auto packages = reg.package_declarations.get())
            {
                for (auto&& pkg : *packages)
                {
                    patterns[pkg.pattern].emplace_back(LocationAndRegistry{
                        pkg.location,
                        reg.pretty_location(),
                    });
                }
            }
        }

        std::vector<LocalizedString> warnings;
        for (auto&& key_value_pair : patterns)
        {
            const auto& pattern = key_value_pair.first;
            const auto& locations = key_value_pair.second;
            if (locations.size() > 1)
            {
                auto first = locations.begin();
                const auto last = locations.end();
                auto warning = msg::format(msgDuplicatePackagePattern, msg::package_name = pattern)
                                   .append_raw('\n')
                                   .append_indent()
                                   .append(msgDuplicatePackagePatternFirstOcurrence)
                                   .append_raw('\n');
                append_declaration_warning(warning, first->location, first->registry, 2)
                    .append_raw('\n')
                    .append_indent()
                    .append(msgDuplicatePackagePatternIgnoredLocations)
                    .append_raw('\n');
                ++first;
                append_declaration_warning(warning, first->location, first->registry, 2);
                while (++first != last)
                {
                    warning.append_raw('\n');
                    append_declaration_warning(warning, first->location, first->registry, 2);
                }
                warnings.emplace_back(warning);
            }
        }
        return warnings;
    }

    Optional<Configuration> ConfigurationDeserializer::visit_object(Json::Reader& r, const Json::Object& obj) const
    {
        Configuration ret;
        Json::Object& extra_info = ret.extra_info;

        std::vector<std::string> comment_keys;
        for (const auto& el : obj)
        {
            if (el.first.starts_with("$"))
            {
                extra_info.insert_or_replace(el.first, el.second);
                comment_keys.emplace_back(el.first);
            }
        }

        r.optional_object_field(obj, JsonIdOverlayPorts, ret.overlay_ports, OverlayPathArrayDeserializer::instance);
        r.optional_object_field(
            obj, JsonIdOverlayTriplets, ret.overlay_triplets, OverlayTripletsPathArrayDeserializer::instance);

        RegistryConfig* default_registry = r.optional_object_field_emplace(
            obj, JsonIdDefaultRegistry, ret.default_reg, RegistryConfigDeserializer::instance);
        if (default_registry && default_registry->kind.value_or("") == JsonIdArtifact)
        {
            r.add_generic_error(type_name(), msg::format(msgDefaultRegistryIsArtifact));
        }

        r.optional_object_field(obj, JsonIdRegistries, ret.registries, RegistriesArrayDeserializer::instance);
        for (auto&& warning : collect_package_pattern_warnings(ret.registries))
        {
            r.add_warning(type_name(), warning);
        }

        Json::Object& ce_metadata_obj = ret.ce_metadata;
        auto maybe_ce_metadata = CeMetadataDeserializer::instance.visit(r, obj);
        if (auto ce_metadata = maybe_ce_metadata.get())
        {
            ce_metadata_obj = *ce_metadata;
        }

        Json::Object demands_obj;
        if (r.optional_object_field(obj, JsonIdDemands, demands_obj, DemandsDeserializer::instance))
        {
            ce_metadata_obj.insert_or_replace(JsonIdDemands, demands_obj);
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
            if (auto demands = obj.get(JsonIdDemands))
            {
                auto demands_obj = demands->maybe_object();
                if (!demands_obj)
                {
                    return;
                }

                Json::Object serialized_demands;
                for (const auto& el : *demands_obj)
                {
                    auto key = el.first;
                    if (key.starts_with("$"))
                    {
                        serialized_demands.insert_or_replace(key, el.second);
                        continue;
                    }

                    if (auto demand_obj = el.second.maybe_object())
                    {
                        auto& inserted = serialized_demands.insert_or_replace(key, Json::Object{});
                        serialize_ce_metadata(*demand_obj, inserted);
                    }
                }
                put_into.insert_or_replace(JsonIdDemands, serialized_demands);
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

        extract_object(ce_metadata, JsonIdMessage, put_into);
        extract_object(ce_metadata, JsonIdWarning, put_into);
        extract_object(ce_metadata, JsonIdError, put_into);
        extract_object(ce_metadata, JsonIdSettings, put_into);
        extract_object(ce_metadata, JsonIdApply, put_into);
        extract_object(ce_metadata, JsonIdRequires, put_into);
        serialize_demands(ce_metadata, put_into);
    }

    static void find_unknown_fields_impl(const Json::Object& obj, std::vector<std::string>& out, StringView path)
    {
        std::vector<StringView> ret;
        for (const auto& el : obj)
        {
            auto key = el.first;
            if (key.starts_with("$"))
            {
                continue;
            }

            if (Util::find(Configuration::known_fields(), key) == std::end(Configuration::known_fields()))
            {
                if (key.contains(' '))
                {
                    key = Strings::concat("[\"", key, "\"]");
                }
                out.push_back(Strings::concat(path, ".", key));
            }

            if (el.first == JsonIdDemands)
            {
                auto maybe_demands_object = el.second.maybe_object();
                if (!maybe_demands_object)
                {
                    continue;
                }

                for (const auto& demand : *maybe_demands_object)
                {
                    if (demand.first.starts_with("$"))
                    {
                        continue;
                    }

                    find_unknown_fields_impl(demand.second.object(VCPKG_LINE_INFO),
                                             out,
                                             Strings::concat(path, ".", JsonIdDemands, ".", demand.first));
                }
            }
        }
    }
}

namespace vcpkg
{
    static ExpectedL<Optional<std::string>> get_baseline_from_git_repo(const VcpkgPaths& paths,
                                                                       StringView url,
                                                                       std::string reference)
    {
        auto res = paths.git_fetch_from_remote_registry(url, reference);
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
        if (kind == JsonIdGit)
        {
            return get_baseline_from_git_repo(paths, repo.value_or_exit(VCPKG_LINE_INFO), reference.value_or("HEAD"));
        }
        else if (kind == JsonIdBuiltin)
        {
            if (paths.use_git_default_registry())
            {
                return get_baseline_from_git_repo(paths, builtin_registry_git_url, reference.value_or("HEAD"));
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
        if (kind == JsonIdBuiltin)
        {
            return builtin_registry_git_url;
        }
        if (kind == JsonIdFilesystem)
        {
            return path.value_or_exit(VCPKG_LINE_INFO);
        }
        if (kind == JsonIdGit)
        {
            return repo.value_or_exit(VCPKG_LINE_INFO);
        }
        if (kind == JsonIdArtifact)
        {
            return location.value_or_exit(VCPKG_LINE_INFO);
        }

        Checks::unreachable(VCPKG_LINE_INFO);
    }

    View<StringView> Configuration::known_fields()
    {
        static constexpr StringView known_fields[]{
            JsonIdDefaultRegistry,
            JsonIdRegistries,
            JsonIdOverlayPorts,
            JsonIdOverlayTriplets,
            JsonIdMessage,
            JsonIdWarning,
            JsonIdError,
            JsonIdSettings,
            JsonIdApply,
            JsonIdRequires,
            JsonIdDemands,
        };
        return known_fields;
    }

    StringLiteral configuration_source_file_name(ConfigurationSource source)
    {
        switch (source)
        {
            case ConfigurationSource::ManifestFileVcpkgConfiguration:
            case ConfigurationSource::ManifestFileConfiguration: return FileVcpkgDotJson;
            case ConfigurationSource::None: // we always make the configuration as a separate file by default, so use
                                            // that name if we don't already have one
            case ConfigurationSource::VcpkgConfigurationFile: return FileVcpkgConfigurationDotJson;
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }
    }

    StringLiteral configuration_source_field(ConfigurationSource source)
    {
        switch (source)
        {
            case ConfigurationSource::None:
            case ConfigurationSource::VcpkgConfigurationFile: return "";
            case ConfigurationSource::ManifestFileVcpkgConfiguration: return JsonIdVcpkgConfiguration;
            case ConfigurationSource::ManifestFileConfiguration: return JsonIdConfiguration;
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }
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

    constexpr const Json::IDeserializer<Configuration>& configuration_deserializer =
        ConfigurationDeserializer::instance;

    Optional<Configuration> parse_configuration(StringView contents, StringView origin, MessageSink& messageSink)
    {
        if (contents.empty()) return nullopt;

        auto conf = Json::parse(contents, origin);
        if (!conf)
        {
            messageSink.println(Color::error, conf.error());
            return nullopt;
        }

        auto conf_value = std::move(conf).value(VCPKG_LINE_INFO).value;
        if (auto conf_value_object = conf_value.maybe_object())
        {
            return parse_configuration(std::move(*conf_value_object), origin, messageSink);
        }

        messageSink.println(msgFailedToParseNoTopLevelObj, msg::path = origin);
        return nullopt;
    }

    Optional<Configuration> parse_configuration(const Json::Object& obj, StringView origin, MessageSink& messageSink)
    {
        Json::Reader reader(origin);
        auto maybe_configuration = ConfigurationDeserializer::instance.visit(reader, obj);
        if (!reader.messages().good())
        {
            if (reader.messages().any_errors())
            {
                DiagnosticLine{DiagKind::Error, origin, msg::format(msgFailedToParseConfig)}.print_to(messageSink);
            }

            for (auto&& line : reader.messages().lines())
            {
                line.print_to(messageSink);
            }

            DiagnosticLine{DiagKind::Note, msg::format(msgExtendedDocumentationAtUrl, msg::url = docs::registries_url)}
                .print_to(messageSink);

            if (reader.messages().any_errors()) return nullopt;
        }
        return maybe_configuration;
    }

    static std::unique_ptr<RegistryImplementation> instantiate_rconfig(const VcpkgPaths& paths,
                                                                       const RegistryConfig& config,
                                                                       const Path& config_dir)
    {
        if (auto k = config.kind.get())
        {
            if (*k == JsonIdBuiltin)
            {
                return make_builtin_registry(paths, config.baseline.value_or_exit(VCPKG_LINE_INFO));
            }
            else if (*k == JsonIdGit)
            {
                return make_git_registry(paths,
                                         config.repo.value_or_exit(VCPKG_LINE_INFO),
                                         config.reference.value_or("HEAD"),
                                         config.baseline.value_or_exit(VCPKG_LINE_INFO));
            }
            else if (*k == JsonIdFilesystem)
            {
                return make_filesystem_registry(paths.get_filesystem(),
                                                config_dir / config.path.value_or_exit(VCPKG_LINE_INFO),
                                                config.baseline.value_or(""));
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
            obj.insert(JsonIdDefaultRegistry, default_registry->serialize());
        }

        if (!registries.empty())
        {
            auto& reg_arr = obj.insert(JsonIdRegistries, Json::Array());
            for (const auto& reg : registries)
            {
                reg_arr.push_back(reg.serialize());
            }
        }

        if (!overlay_ports.empty())
        {
            auto& op_arr = obj.insert(JsonIdOverlayPorts, Json::Array());
            for (const auto& port : overlay_ports)
            {
                op_arr.push_back(port);
            }
        }

        if (!overlay_triplets.empty())
        {
            auto& ot_arr = obj.insert(JsonIdOverlayTriplets, Json::Array());
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
        obj.insert(JsonIdKind, Json::Value::string(*kind.get()));
        if (auto p = baseline.get()) obj.insert(JsonIdBaseline, Json::Value::string(*p));
        if (auto p = location.get()) obj.insert(JsonIdLocation, Json::Value::string(*p));
        if (auto p = name.get()) obj.insert(JsonIdName, Json::Value::string(*p));
        if (auto p = path.get()) obj.insert(JsonIdPath, Json::Value::string(p->native()));
        if (auto p = reference.get()) obj.insert(JsonIdReference, Json::Value::string(*p));
        if (auto p = repo.get()) obj.insert(JsonIdRepository, Json::Value::string(*p));
        if (packages)
        {
            auto& arr = obj.insert(JsonIdPackages, Json::Array());
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

    bool is_package_pattern(StringView sv)
    {
        if (Json::IdentifierDeserializer::is_ident(sv))
        {
            return true;
        }

        // ([a-z0-9]+-)*([a-z0-9]+[*]?|[*])
        auto cur = sv.begin();
        const auto last = sv.end();
        // each iteration of this loop matches either
        // ([a-z0-9]+-)
        // or the last
        // ([a-z0-9]+[*]?|[*])
        for (;;)
        {
            if (cur == last)
            {
                return false;
            }

            // this if checks for the first matched character of [a-z0-9]+
            if (!ParserBase::is_lower_digit(*cur))
            {
                // [a-z0-9]+ didn't match anything, so we must be matching
                // the last [*]
                if (*cur == '*')
                {
                    return ++cur == last;
                }

                return false;
            }

            // match the rest of the [a-z0-9]+
            do
            {
                ++cur;
                if (cur == last)
                {
                    return true;
                }
            } while (ParserBase::is_lower_digit(*cur));

            switch (*cur)
            {
                case '-':
                    // this loop iteration matched [a-z0-9]+- once
                    ++cur;
                    continue;
                case '*':
                    // this loop matched [a-z0-9]+[*]? (and [*] was present)
                    ++cur;
                    return cur == last;
                default: return false;
            }
        }
    }
}
