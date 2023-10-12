#include <vcpkg/base/files.h>
#include <vcpkg/base/jsonreader.h>
#include <vcpkg/base/message_sinks.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/util.h>

#include <vcpkg/configuration.h>
#include <vcpkg/documentation.h>
#include <vcpkg/metrics.h>
#include <vcpkg/vcpkgcmdarguments.h>
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

        virtual LocalizedString type_name() const override { return msg::format(msgARegistry); }
        virtual View<StringView> valid_fields() const override;

        virtual Optional<RegistryConfig> visit_null(Json::Reader&) const override;
        virtual Optional<RegistryConfig> visit_object(Json::Reader&, const Json::Object&) const override;

        static const RegistryConfigDeserializer instance;
    };
    const RegistryConfigDeserializer RegistryConfigDeserializer::instance;
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
        constexpr static StringLiteral PACKAGES = "packages";

        virtual LocalizedString type_name() const override { return msg::format(msgARegistry); }
        virtual View<StringView> valid_fields() const override;

        virtual Optional<RegistryConfig> visit_object(Json::Reader&, const Json::Object&) const override;

        static RegistryDeserializer instance;
    };
    RegistryDeserializer RegistryDeserializer::instance;
    constexpr StringLiteral RegistryDeserializer::PACKAGES;

    struct RegistriesArrayDeserializer : Json::ArrayDeserializer<RegistryDeserializer>
    {
        LocalizedString type_name() const override { return msg::format(msgAnArrayOfRegistries); }

        static const RegistriesArrayDeserializer instance;
    };

    const RegistriesArrayDeserializer RegistriesArrayDeserializer::instance;

    View<StringView> RegistryConfigDeserializer::valid_fields() const
    {
        static constexpr StringView t[] = {KIND, BASELINE, PATH, REPO, REFERENCE, NAME, LOCATION};
        return t;
    }
    View<StringView> valid_builtin_fields()
    {
        static constexpr StringView t[] = {
            RegistryConfigDeserializer::KIND,
            RegistryConfigDeserializer::BASELINE,
            RegistryDeserializer::PACKAGES,
        };
        return t;
    }
    View<StringView> valid_filesystem_fields()
    {
        static constexpr StringView t[] = {
            RegistryConfigDeserializer::KIND,
            RegistryConfigDeserializer::BASELINE,
            RegistryConfigDeserializer::PATH,
            RegistryDeserializer::PACKAGES,
        };
        return t;
    }
    View<StringView> valid_git_fields()
    {
        static constexpr StringView t[] = {
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
        static constexpr StringView t[] = {
            RegistryConfigDeserializer::KIND,
            RegistryConfigDeserializer::NAME,
            RegistryConfigDeserializer::LOCATION,
        };
        return t;
    }

    Optional<RegistryConfig> RegistryConfigDeserializer::visit_null(Json::Reader&) const { return RegistryConfig(); }

    Optional<RegistryConfig> RegistryConfigDeserializer::visit_object(Json::Reader& r, const Json::Object& obj) const
    {
        RegistryConfig res;
        auto& kind = res.kind.emplace();
        r.required_object_field(type_name(), obj, KIND, kind, RegistryImplementationKindDeserializer::instance);

        if (kind == KIND_BUILTIN)
        {
            auto& baseline = res.baseline.emplace();
            r.required_object_field(
                msg::format(msgABuiltinRegistry), obj, BASELINE, baseline, BaselineShaDeserializer::instance);
            r.check_for_unexpected_fields(obj, valid_builtin_fields(), msg::format(msgABuiltinRegistry));
        }
        else if (kind == KIND_FILESYSTEM)
        {
            std::string baseline;
            if (r.optional_object_field(obj, BASELINE, baseline, BaselineShaDeserializer::instance))
            {
                res.baseline = std::move(baseline);
            }

            r.required_object_field(
                msg::format(msgAFilesystemRegistry), obj, PATH, res.path.emplace(), Json::PathDeserializer::instance);

            r.check_for_unexpected_fields(obj, valid_filesystem_fields(), msg::format(msgAFilesystemRegistry));
        }
        else if (kind == KIND_GIT)
        {
            r.required_object_field(
                msg::format(msgAGitRegistry), obj, REPO, res.repo.emplace(), GitUrlDeserializer::instance);

            if (!r.optional_object_field(obj, REFERENCE, res.reference.emplace(), GitReferenceDeserializer::instance))
            {
                res.reference = nullopt;
            }

            r.required_object_field(
                msg::format(msgAGitRegistry), obj, BASELINE, res.baseline.emplace(), BaselineShaDeserializer::instance);

            r.check_for_unexpected_fields(obj, valid_git_fields(), msg::format(msgAGitRegistry));
        }
        else if (kind == KIND_ARTIFACT)
        {
            r.required_object_field(msg::format(msgAnArtifactsRegistry),
                                    obj,
                                    NAME,
                                    res.name.emplace(),
                                    Json::IdentifierDeserializer::instance);

            r.required_object_field(msg::format(msgAnArtifactsRegistry),
                                    obj,
                                    LOCATION,
                                    res.location.emplace(),
                                    ArtifactsGitRegistryUrlDeserializer::instance);

            r.check_for_unexpected_fields(obj, valid_artifact_fields(), msg::format(msgAnArtifactsRegistry));
        }
        else
        {
            StringLiteral valid_kinds[] = {KIND_BUILTIN, KIND_FILESYSTEM, KIND_GIT, KIND_ARTIFACT};
            r.add_generic_error(type_name(),
                                msg::format(msgFieldKindDidNotHaveExpectedValue,
                                            msg::expected = Strings::join(", ", valid_kinds),
                                            msg::actual = kind));
            return nullopt;
        }

        return std::move(res); // gcc-7 bug workaround redundant move
    }

    View<StringView> RegistryDeserializer::valid_fields() const
    {
        static constexpr StringView t[] = {
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

    Optional<RegistryConfig> RegistryDeserializer::visit_object(Json::Reader& r, const Json::Object& obj) const
    {
        auto impl = RegistryConfigDeserializer::instance.visit_object(r, obj);

        if (auto config = impl.get())
        {
            if (config->kind && *config->kind.get() != RegistryConfigDeserializer::KIND_ARTIFACT)
            {
                auto& declarations = config->package_declarations.emplace();
                r.required_object_field(
                    type_name(), obj, PACKAGES, declarations, PackagePatternArrayDeserializer::instance);
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

        constexpr static StringLiteral CE_ERROR = "error";
        constexpr static StringLiteral CE_WARNING = "warning";
        constexpr static StringLiteral CE_MESSAGE = "message";
        constexpr static StringLiteral CE_APPLY = "apply";
        constexpr static StringLiteral CE_SETTINGS = "settings";
        constexpr static StringLiteral CE_REQUIRES = "requires";

        virtual Optional<Json::Object> visit_object(Json::Reader& r, const Json::Object& obj) const override;

        static const CeMetadataDeserializer instance;
    };

    const CeMetadataDeserializer CeMetadataDeserializer::instance;
    constexpr StringLiteral CeMetadataDeserializer::CE_ERROR;
    constexpr StringLiteral CeMetadataDeserializer::CE_WARNING;
    constexpr StringLiteral CeMetadataDeserializer::CE_MESSAGE;
    constexpr StringLiteral CeMetadataDeserializer::CE_APPLY;
    constexpr StringLiteral CeMetadataDeserializer::CE_SETTINGS;
    constexpr StringLiteral CeMetadataDeserializer::CE_REQUIRES;

    struct DemandsDeserializer final : Json::IDeserializer<Json::Object>
    {
        virtual LocalizedString type_name() const override { return msg::format(msgADemandObject); }

        constexpr static StringLiteral CE_DEMANDS = "demands";

        virtual Optional<Json::Object> visit_object(Json::Reader& r, const Json::Object& obj) const override;

        static DemandsDeserializer instance;
    };
    DemandsDeserializer DemandsDeserializer::instance;
    constexpr StringLiteral DemandsDeserializer::CE_DEMANDS;

    struct ConfigurationDeserializer final : Json::IDeserializer<Configuration>
    {
        virtual LocalizedString type_name() const override { return msg::format(msgAConfigurationObject); }

        constexpr static StringLiteral DEFAULT_REGISTRY = "default-registry";
        constexpr static StringLiteral REGISTRIES = "registries";
        constexpr static StringLiteral OVERLAY_PORTS = "overlay-ports";
        constexpr static StringLiteral OVERLAY_TRIPLETS = "overlay-triplets";

        virtual Optional<Configuration> visit_object(Json::Reader& r, const Json::Object& obj) const override;

        static ConfigurationDeserializer instance;
    };
    ConfigurationDeserializer ConfigurationDeserializer::instance;
    constexpr StringLiteral ConfigurationDeserializer::DEFAULT_REGISTRY;
    constexpr StringLiteral ConfigurationDeserializer::REGISTRIES;
    constexpr StringLiteral ConfigurationDeserializer::OVERLAY_PORTS;
    constexpr StringLiteral ConfigurationDeserializer::OVERLAY_TRIPLETS;

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
            const auto errors_count = r.errors();
            if (r.optional_object_field(obj, key, value, Json::UntypedStringDeserializer::instance))
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

    Optional<Json::Object> DemandsDeserializer::visit_object(Json::Reader& r, const Json::Object& obj) const
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
                r.add_generic_error(type_name(), msg::format(msgJsonFieldNotObject, msg::json_field = key));
                continue;
            }

            const auto& demand_obj = el.second.object(VCPKG_LINE_INFO);
            if (demand_obj.contains(CE_DEMANDS))
            {
                r.add_generic_error(type_name(),
                                    msg::format(msgConfigurationNestedDemands, msg::json_field = el.first));
            }

            auto maybe_demand = r.visit(demand_obj, CeMetadataDeserializer::instance);
            if (maybe_demand.has_value())
            {
                ret.insert_or_replace(key, maybe_demand.value_or_exit(VCPKG_LINE_INFO));
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
                auto warning = msg::format_warning(msgDuplicatePackagePattern, msg::package_name = pattern)
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
            if (Strings::starts_with(el.first, "$"))
            {
                extra_info.insert_or_replace(el.first, el.second);
                comment_keys.emplace_back(el.first);
            }
        }

        r.optional_object_field(obj, OVERLAY_PORTS, ret.overlay_ports, OverlayPathArrayDeserializer::instance);
        r.optional_object_field(
            obj, OVERLAY_TRIPLETS, ret.overlay_triplets, OverlayTripletsPathArrayDeserializer::instance);

        RegistryConfig default_registry;
        if (r.optional_object_field(obj, DEFAULT_REGISTRY, default_registry, RegistryConfigDeserializer::instance))
        {
            if (default_registry.kind.value_or("") == RegistryConfigDeserializer::KIND_ARTIFACT)
            {
                r.add_generic_error(type_name(), msg::format(msgDefaultRegistryIsArtifact));
            }
            ret.default_reg = std::move(default_registry);
        }

        r.optional_object_field(obj, REGISTRIES, ret.registries, RegistriesArrayDeserializer::instance);

        for (auto&& warning : collect_package_pattern_warnings(ret.registries))
        {
            r.add_warning(type_name(), warning);
        }

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

    Optional<Configuration> parse_configuration(StringView contents, StringView origin, MessageSink& messageSink)
    {
        if (contents.empty()) return nullopt;

        auto conf = Json::parse(contents, origin);
        if (!conf)
        {
            messageSink.println(Color::error, LocalizedString::from_raw(conf.error()->to_string()));
            return nullopt;
        }

        auto conf_value = std::move(conf).value(VCPKG_LINE_INFO).value;
        if (!conf_value.is_object())
        {
            messageSink.println(msgFailedToParseNoTopLevelObj, msg::path = origin);
            return nullopt;
        }

        return parse_configuration(std::move(conf_value).object(VCPKG_LINE_INFO), origin, messageSink);
    }

    Optional<Configuration> parse_configuration(const Json::Object& obj, StringView origin, MessageSink& messageSink)
    {
        Json::Reader reader;
        auto maybe_configuration = reader.visit(obj, get_configuration_deserializer());
        bool has_warnings = !reader.warnings().empty();
        bool has_errors = !reader.errors().empty();
        if (has_warnings || has_errors)
        {
            if (has_errors)
            {
                messageSink.println(Color::error, msgFailedToParseConfig, msg::path = origin);
            }
            else
            {
                messageSink.println(Color::warning, msgWarnOnParseConfig, msg::path = origin);
            }

            for (auto&& msg : reader.errors())
            {
                messageSink.println(Color::error, LocalizedString().append_indent().append_raw(msg));
            }

            for (auto&& msg : reader.warnings())
            {
                messageSink.println(Color::warning, LocalizedString().append_indent().append(msg));
            }

            msg::println(msgExtendedDocumentationAtUrl, msg::url = docs::registries_url);

            if (has_errors) return nullopt;
        }
        return maybe_configuration;
    }

    static std::unique_ptr<RegistryImplementation> instantiate_rconfig(const VcpkgPaths& paths,
                                                                       const RegistryConfig& config,
                                                                       const Path& config_dir)
    {
        if (auto k = config.kind.get())
        {
            if (*k == RegistryConfigDeserializer::KIND_BUILTIN)
            {
                return make_builtin_registry(paths, config.baseline.value_or_exit(VCPKG_LINE_INFO));
            }
            else if (*k == RegistryConfigDeserializer::KIND_GIT)
            {
                return make_git_registry(paths,
                                         config.repo.value_or_exit(VCPKG_LINE_INFO),
                                         config.reference.value_or("HEAD"),
                                         config.baseline.value_or_exit(VCPKG_LINE_INFO));
            }
            else if (*k == RegistryConfigDeserializer::KIND_FILESYSTEM)
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

    bool is_package_pattern(StringView sv)
    {
        if (Json::IdentifierDeserializer::is_ident(sv))
        {
            return true;
        }

        /*if (sv == "*")
        {
            return true;
        }*/

        // ([a-z0-9]+(-[a-z0-9]+)*)(\*?)
        auto cur = sv.begin();
        const auto last = sv.end();
        for (;;)
        {
            // [a-z0-9]+
            if (cur == last)
            {
                return false;
            }

            if (!ParserBase::is_lower_digit(*cur))
            {
                if (*cur != '*')
                {
                    return false;
                }

                return ++cur == last;
            }

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
                    // repeat outer [a-z0-9]+ again to match -[a-z0-9]+
                    ++cur;
                    continue;
                case '*':
                    // match last optional *
                    ++cur;
                    return cur == last;
                default: return false;
            }
        }
    }
}
