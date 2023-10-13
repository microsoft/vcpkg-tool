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
        virtual View<StringView> valid_fields() const override;

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
        virtual View<StringView> valid_fields() const override;

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

    View<StringView> RegistryConfigDeserializer::valid_fields() const
    {
        static constexpr StringView t[] = {
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
    static constexpr StringView valid_builtin_fields[] = {
        JsonIdKind,
        JsonIdBaseline,
        JsonIdPackages,
    };

    static constexpr StringView valid_filesystem_fields[] = {
        JsonIdKind,
        JsonIdBaseline,
        JsonIdPath,
        JsonIdPackages,
    };

    static constexpr StringView valid_git_fields[] = {
        JsonIdKind,
        JsonIdBaseline,
        JsonIdRepository,
        JsonIdReference,
        JsonIdPackages,
    };

    static constexpr StringView valid_artifact_fields[] = {
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
            std::string baseline;
            if (r.optional_object_field(obj, JsonIdBaseline, baseline, BaselineShaDeserializer::instance))
            {
                res.baseline = std::move(baseline);
            }

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

            if (!r.optional_object_field(
                    obj, JsonIdReference, res.reference.emplace(), GitReferenceDeserializer::instance))
            {
                res.reference = nullopt;
            }

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

    View<StringView> RegistryDeserializer::valid_fields() const
    {
        static constexpr StringView t[] = {
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

    struct DictionaryValidator final : Json::IDeserializer<Unit>
    {
        virtual LocalizedString type_name() const override { return msg::format(msgAStringStringDictionary); }

        virtual Optional<Unit> visit_object(Json::Reader& r, const Json::Object& obj) const override;

        static const DictionaryValidator instance;
    };

    const DictionaryValidator DictionaryValidator::instance;

    struct CeMetadataValidator final : Json::IDeserializer<Unit>
    {
        constexpr CeMetadataValidator(bool b) : allow_demands(b) { }

        virtual LocalizedString type_name() const override
        {
            return msg::format(msgAnObjectContainingVcpkgArtifactsMetadata);
        }

        virtual Optional<Unit> visit_object(Json::Reader& r, const Json::Object& obj) const override;

        const bool allow_demands = false;

        static const CeMetadataValidator instance;
        static const CeMetadataValidator nested_instance;
    };

    const CeMetadataValidator CeMetadataValidator::instance{true};
    const CeMetadataValidator CeMetadataValidator::nested_instance{false};

    struct DemandsValidator final : Json::IDeserializer<Unit>
    {
        virtual LocalizedString type_name() const override { return msg::format(msgADemandObject); }

        virtual Optional<Unit> visit_object(Json::Reader& r, const Json::Object& obj) const override;

        static DemandsValidator instance;
    };
    DemandsValidator DemandsValidator::instance;

    struct ConfigurationDeserializer final : Json::IDeserializer<Configuration>
    {
        virtual LocalizedString type_name() const override { return msg::format(msgAConfigurationObject); }

        virtual Optional<Configuration> visit_object(Json::Reader& r, const Json::Object& obj) const override;

        static ConfigurationDeserializer instance;
    };
    ConfigurationDeserializer ConfigurationDeserializer::instance;

    struct ArtifactsObjectValidator : Json::IDeserializer<Unit>
    {
        virtual LocalizedString type_name() const override
        {
            return msg::format(msgAnObjectContainingVcpkgArtifactsMetadata);
        }

        virtual Optional<Unit> visit_object(Json::Reader&, const Json::Object&) const override { return Unit{}; }

        static const ArtifactsObjectValidator instance;
    };
    const ArtifactsObjectValidator ArtifactsObjectValidator::instance;

    struct UntypedStringValidator : Json::IDeserializer<Unit>
    {
        virtual LocalizedString type_name() const override { return msg::format(msgAString); }

        virtual Optional<Unit> visit_string(Json::Reader&, StringView) const override { return Unit{}; }

        static const UntypedStringValidator instance;
    };

    const UntypedStringValidator UntypedStringValidator::instance;

    Optional<Unit> DictionaryValidator::visit_object(Json::Reader& r, const Json::Object& obj) const
    {
        Unit u;
        for (const auto& el : obj)
        {
            r.visit_in_key(el.value, el.key, u, UntypedStringValidator::instance);
        }
        return Unit{};
    }

    Optional<Unit> CeMetadataValidator::visit_object(Json::Reader& r, const Json::Object& obj) const
    {
        Unit u;
        r.optional_object_field(obj, JsonIdError, u, UntypedStringValidator::instance);
        r.optional_object_field(obj, JsonIdWarning, u, UntypedStringValidator::instance);
        r.optional_object_field(obj, JsonIdMessage, u, UntypedStringValidator::instance);
        r.optional_object_field(obj, JsonIdApply, u, ArtifactsObjectValidator::instance);
        r.optional_object_field(obj, JsonIdSettings, u, ArtifactsObjectValidator::instance);
        r.optional_object_field(obj, JsonIdRequires, u, DictionaryValidator::instance);
        if (!allow_demands && obj.contains(JsonIdDemands))
        {
            r.add_extra_field_error(type_name(), JsonIdDemands);
        }
        return u;
    }

    Optional<Unit> DemandsValidator::visit_object(Json::Reader& r, const Json::Object& obj) const
    {
        Unit u;
        for (const auto& el : obj)
        {
            if (!Strings::starts_with(el.key, "$"))
            {
                r.visit_in_key(el.value, el.key, u, CeMetadataValidator::nested_instance);
            }
        }
        return u;
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
        static constexpr StringLiteral handled_fields[] = {
            JsonIdDefaultRegistry,
            JsonIdRegistries,
            JsonIdOverlayPorts,
            JsonIdOverlayTriplets,
        };

        Configuration ret;
        for (auto&& kv : obj)
        {
            if (!kv.key.empty() && kv.key[0] == '$')
            {
                ret.extra_info.insert_or_replace(kv.key, kv.value);
            }
            else if (std::find(std::begin(handled_fields), std::end(handled_fields), kv.key) ==
                     std::end(handled_fields))
            {
                ret.ce_metadata.insert_or_replace(kv.key, kv.value);
            }
        }

        r.optional_object_field(obj, JsonIdOverlayPorts, ret.overlay_ports, OverlayPathArrayDeserializer::instance);
        r.optional_object_field(
            obj, JsonIdOverlayTriplets, ret.overlay_triplets, OverlayTripletsPathArrayDeserializer::instance);

        RegistryConfig default_registry;
        if (r.optional_object_field(obj, JsonIdDefaultRegistry, default_registry, RegistryConfigDeserializer::instance))
        {
            if (default_registry.kind.value_or("") == JsonIdArtifact)
            {
                r.add_generic_error(type_name(), msg::format(msgDefaultRegistryIsArtifact));
            }
            ret.default_reg = std::move(default_registry);
        }

        r.optional_object_field(obj, JsonIdRegistries, ret.registries, RegistriesArrayDeserializer::instance);

        for (auto&& warning : collect_package_pattern_warnings(ret.registries))
        {
            r.add_warning(type_name(), warning);
        }

        CeMetadataValidator::instance.visit_object(r, obj);
        Unit u;
        r.optional_object_field(obj, JsonIdDemands, u, DemandsValidator::instance);
        return std::move(ret);
    }

    static void find_unknown_fields_impl(const Json::Object& obj, std::vector<std::string>& out, StringView path)
    {
        std::vector<StringView> ret;
        for (const auto& el : obj)
        {
            auto key = el.key;
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

            if (el.key == JsonIdDemands)
            {
                if (!el.value.is_object())
                {
                    continue;
                }

                for (const auto& demand : el.value.object(VCPKG_LINE_INFO))
                {
                    if (Strings::starts_with(demand.key, "$"))
                    {
                        continue;
                    }

                    find_unknown_fields_impl(demand.value.object(VCPKG_LINE_INFO),
                                             out,
                                             Strings::concat(path, ".", JsonIdDemands, ".", demand.key));
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
        if (kind == JsonIdGit)
        {
            return get_baseline_from_git_repo(paths, repo.value_or_exit(VCPKG_LINE_INFO));
        }
        else if (kind == JsonIdBuiltin)
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

    const Json::IDeserializer<Unit>& artifacts_object_validator = ArtifactsObjectValidator::instance;

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
        if (!conf_value.is_object())
        {
            messageSink.println(msgFailedToParseNoTopLevelObj, msg::path = origin);
            return nullopt;
        }

        return parse_configuration(std::move(conf_value).object(VCPKG_LINE_INFO), origin, messageSink);
    }

    Optional<Configuration> parse_configuration(const Json::Object& obj, StringView origin, MessageSink& messageSink)
    {
        Json::Reader reader(origin);
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
            obj.insert(el.key, el.value);
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

        obj.insert_or_replace_all(ce_metadata);
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
