#include <vcpkg/base/api-stable-format.h>
#include <vcpkg/base/checks.h>
#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/expected.h>
#include <vcpkg/base/jsonreader.h>
#include <vcpkg/base/message_sinks.h>
#include <vcpkg/base/span.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/stringview.h>
#include <vcpkg/base/util.h>

#include <vcpkg/configuration.h>
#include <vcpkg/documentation.h>
#include <vcpkg/metrics.h>
#include <vcpkg/packagespec.h>
#include <vcpkg/platform-expression.h>
#include <vcpkg/sourceparagraph.h>
#include <vcpkg/triplet.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/versiondeserializers.h>

namespace vcpkg
{

    bool operator==(const DependencyConstraint& lhs, const DependencyConstraint& rhs)
    {
        if (lhs.type != rhs.type) return false;
        return lhs.version == rhs.version;
    }

    Optional<Version> DependencyConstraint::try_get_minimum_version() const
    {
        if (type == VersionConstraintKind::None)
        {
            return nullopt;
        }

        return version;
    }

    bool Dependency::has_platform_expressions() const
    {
        return !platform.is_empty() || Util::any_of(features, [](const auto f) { return !f.platform.is_empty(); });
    }

    FullPackageSpec Dependency::to_full_spec(View<std::string> feature_list, Triplet target, Triplet host_triplet) const
    {
        InternalFeatureSet internal_feature_list(feature_list.begin(), feature_list.end());
        internal_feature_list.emplace_back(FeatureNameCore.data(), FeatureNameCore.size());
        if (default_features)
        {
            internal_feature_list.emplace_back(FeatureNameDefault.data(), FeatureNameDefault.size());
        }
        return FullPackageSpec{{name, host ? host_triplet : target}, std::move(internal_feature_list)};
    }

    bool operator==(const Dependency& lhs, const Dependency& rhs)
    {
        if (lhs.name != rhs.name) return false;
        if (lhs.features != rhs.features) return false;
        if (!structurally_equal(lhs.platform, rhs.platform)) return false;
        if (lhs.extra_info != rhs.extra_info) return false;
        if (lhs.constraint != rhs.constraint) return false;
        if (lhs.host != rhs.host) return false;

        return true;
    }
    bool operator!=(const Dependency& lhs, const Dependency& rhs);

    bool operator==(const DependencyOverride& lhs, const DependencyOverride& rhs)
    {
        if (lhs.name != rhs.name) return false;
        if (lhs.version != rhs.version) return false;
        if (lhs.scheme != rhs.scheme) return false;
        return lhs.extra_info == rhs.extra_info;
    }
    bool operator!=(const DependencyOverride& lhs, const DependencyOverride& rhs);

    bool operator==(const DependencyRequestedFeature& lhs, const DependencyRequestedFeature& rhs)
    {
        return lhs.name == rhs.name && structurally_equal(lhs.platform, rhs.platform);
    }

    bool operator!=(const DependencyRequestedFeature& lhs, const DependencyRequestedFeature& rhs)
    {
        return !(lhs == rhs);
    }

    struct UrlDeserializer : Json::StringDeserializer
    {
        LocalizedString type_name() const override { return msg::format(msgAUrl); }

        static const UrlDeserializer instance;
    };

    const UrlDeserializer UrlDeserializer::instance;

    template<class Lhs, class Rhs>
    static bool paragraph_equal(const Lhs& lhs, const Rhs& rhs)
    {
        return std::equal(
            lhs.begin(), lhs.end(), rhs.begin(), rhs.end(), [](const std::string& lhs, const std::string& rhs) {
                return Strings::trim(StringView(lhs)) == Strings::trim(StringView(rhs));
            });
    }

    bool operator==(const SourceParagraph& lhs, const SourceParagraph& rhs)
    {
        if (lhs.name != rhs.name) return false;
        if (lhs.version_scheme != rhs.version_scheme) return false;
        if (lhs.version != rhs.version) return false;
        if (!paragraph_equal(lhs.description, rhs.description)) return false;
        if (!paragraph_equal(lhs.maintainers, rhs.maintainers)) return false;
        if (lhs.homepage != rhs.homepage) return false;
        if (lhs.documentation != rhs.documentation) return false;
        if (lhs.dependencies != rhs.dependencies) return false;
        if (lhs.default_features != rhs.default_features) return false;
        if (lhs.license != rhs.license) return false;

        if (!structurally_equal(lhs.supports_expression, rhs.supports_expression)) return false;

        if (lhs.extra_info != rhs.extra_info) return false;

        return true;
    }

    bool operator==(const FeatureParagraph& lhs, const FeatureParagraph& rhs)
    {
        if (lhs.name != rhs.name) return false;
        if (lhs.dependencies != rhs.dependencies) return false;
        if (!paragraph_equal(lhs.description, rhs.description)) return false;
        if (lhs.extra_info != rhs.extra_info) return false;

        return true;
    }

    bool operator==(const SourceControlFile& lhs, const SourceControlFile& rhs)
    {
        if (*lhs.core_paragraph != *rhs.core_paragraph) return false;
        return std::equal(lhs.feature_paragraphs.begin(),
                          lhs.feature_paragraphs.end(),
                          rhs.feature_paragraphs.begin(),
                          rhs.feature_paragraphs.end(),
                          [](const std::unique_ptr<FeatureParagraph>& lhs,
                             const std::unique_ptr<FeatureParagraph>& rhs) { return *lhs == *rhs; });
    }

    static void trim_all(std::vector<std::string>& arr)
    {
        for (auto& el : arr)
        {
            Strings::inplace_trim(el);
        }
    }

    namespace
    {
        constexpr static struct Canonicalize
        {
            struct FeatureLess
            {
                bool operator()(const std::unique_ptr<FeatureParagraph>& lhs,
                                const std::unique_ptr<FeatureParagraph>& rhs) const
                {
                    return (*this)(*lhs, *rhs);
                }
                bool operator()(const FeatureParagraph& lhs, const FeatureParagraph& rhs) const
                {
                    return lhs.name < rhs.name;
                }
            };
            struct FeatureEqual
            {
                bool operator()(const std::unique_ptr<FeatureParagraph>& lhs,
                                const std::unique_ptr<FeatureParagraph>& rhs) const
                {
                    return (*this)(*lhs, *rhs);
                }
                bool operator()(const FeatureParagraph& lhs, const FeatureParagraph& rhs) const
                {
                    return lhs.name == rhs.name;
                }
            };

            struct DependencyFeatureLess
            {
                bool operator()(const DependencyRequestedFeature& lhs, const DependencyRequestedFeature& rhs) const
                {
                    if (lhs.name == rhs.name)
                    {
                        auto platform_cmp = compare(lhs.platform, rhs.platform);
                        return platform_cmp < 0;
                    }
                    return lhs.name < rhs.name;
                }
            };

            // assume canonicalized feature list
            struct DependencyLess
            {
                bool operator()(const std::unique_ptr<Dependency>& lhs, const std::unique_ptr<Dependency>& rhs) const
                {
                    return (*this)(*lhs, *rhs);
                }
                bool operator()(const Dependency& lhs, const Dependency& rhs) const
                {
                    auto cmp = lhs.name.compare(rhs.name);
                    if (cmp < 0) return true;
                    if (cmp > 0) return false;

                    // same dependency name

                    // order by platform string:
                    auto platform_cmp = compare(lhs.platform, rhs.platform);
                    if (platform_cmp < 0) return true;
                    if (platform_cmp > 0) return false;

                    // then order by features
                    // smaller list first, then lexicographical
                    if (lhs.features.size() < rhs.features.size()) return true;
                    if (rhs.features.size() < lhs.features.size()) return false;

                    // then finally order by feature list
                    if (std::lexicographical_compare(lhs.features.begin(),
                                                     lhs.features.end(),
                                                     rhs.features.begin(),
                                                     rhs.features.end(),
                                                     DependencyFeatureLess{}))
                    {
                        return true;
                    }
                    return false;
                }
            };

            template<class T>
            void operator()(std::unique_ptr<T>& ptr) const
            {
                (*this)(*ptr);
            }

            void operator()(Dependency& dep) const
            {
                std::sort(dep.features.begin(), dep.features.end(), DependencyFeatureLess{});
                dep.extra_info.sort_keys();
            }
            void operator()(SourceParagraph& spgh) const
            {
                std::for_each(spgh.dependencies.begin(), spgh.dependencies.end(), *this);
                std::sort(spgh.dependencies.begin(), spgh.dependencies.end(), DependencyLess{});

                std::sort(spgh.default_features.begin(), spgh.default_features.end(), DependencyFeatureLess{});

                spgh.extra_info.sort_keys();
            }
            void operator()(FeatureParagraph& fpgh) const
            {
                std::for_each(fpgh.dependencies.begin(), fpgh.dependencies.end(), *this);
                std::sort(fpgh.dependencies.begin(), fpgh.dependencies.end(), DependencyLess{});

                fpgh.extra_info.sort_keys();
            }
            [[nodiscard]] Optional<LocalizedString> operator()(SourceControlFile& scf) const
            {
                (*this)(*scf.core_paragraph);
                std::for_each(scf.feature_paragraphs.begin(), scf.feature_paragraphs.end(), *this);
                std::sort(scf.feature_paragraphs.begin(), scf.feature_paragraphs.end(), FeatureLess{});

                auto adjacent_equal =
                    std::adjacent_find(scf.feature_paragraphs.begin(), scf.feature_paragraphs.end(), FeatureEqual{});
                if (adjacent_equal != scf.feature_paragraphs.end())
                {
                    return msg::format_error(
                        msgMultipleFeatures, msg::package_name = scf.to_name(), msg::feature = (*adjacent_equal)->name);
                }
                return nullopt;
            }
        } canonicalize{};
    }

    static ExpectedL<std::unique_ptr<SourceParagraph>> parse_source_paragraph(StringView origin, Paragraph&& fields)
    {
        ParagraphParser parser(origin, std::move(fields));

        auto spgh = std::make_unique<SourceParagraph>();

        spgh->name = parser.required_field(ParagraphIdSource);
        spgh->version.text = parser.required_field(ParagraphIdVersion);

        TextRowCol pv_position;
        auto pv_str = parser.optional_field(ParagraphIdPortVersion, pv_position);
        if (!pv_str.empty())
        {
            auto pv_opt = Strings::strto<int>(pv_str);
            if (auto pv = pv_opt.get())
            {
                spgh->version.port_version = *pv;
            }
            else
            {
                parser.add_error(pv_position, msgPortVersionControlMustBeANonNegativeInteger);
            }
        }

        spgh->description = Strings::split(parser.optional_field(ParagraphIdDescription), '\n');
        trim_all(spgh->description);

        spgh->maintainers = Strings::split(parser.optional_field(ParagraphIdMaintainer), '\n');
        trim_all(spgh->maintainers);

        spgh->homepage = parser.optional_field(ParagraphIdHomepage);
        TextRowCol textrowcol;
        std::string buf = parser.optional_field(ParagraphIdBuildDepends, textrowcol);

        auto maybe_dependencies = parse_dependencies_list(buf, origin, textrowcol);
        if (const auto dependencies = maybe_dependencies.get())
        {
            spgh->dependencies = *dependencies;
        }
        else
        {
            return std::move(maybe_dependencies).error();
        }

        buf = parser.optional_field(ParagraphIdDefaultFeatures, textrowcol);

        auto maybe_default_features = parse_default_features_list(buf, origin, textrowcol);
        if (const auto default_features = maybe_default_features.get())
        {
            for (auto&& default_feature : *default_features)
            {
                if (default_feature == FeatureNameCore)
                {
                    return msg::format_error(msgDefaultFeatureCore);
                }

                if (default_feature == FeatureNameDefault)
                {
                    return msg::format_error(msgDefaultFeatureDefault);
                }

                if (!Json::IdentifierDeserializer::is_ident(default_feature))
                {
                    return msg::format_error(msgDefaultFeatureIdentifier)
                        .append_raw('\n')
                        .append_raw(NotePrefix)
                        .append(msgParseIdentifierError, msg::value = default_feature, msg::url = docs::manifests_url);
                }

                spgh->default_features.push_back({std::move(default_feature)});
            }
        }
        else
        {
            return std::move(maybe_default_features).error();
        }

        TextRowCol supports_position;
        auto supports_text = parser.optional_field(ParagraphIdSupports, supports_position);
        if (!supports_text.empty())
        {
            auto maybe_expr = PlatformExpression::parse_platform_expression(
                supports_text, PlatformExpression::MultipleBinaryOperators::Allow);
            if (auto expr = maybe_expr.get())
            {
                spgh->supports_expression = std::move(*expr);
            }
            else
            {
                parser.add_error(supports_position, msgControlSupportsMustBeAPlatformExpression);
            }
        }

        // This is leftover from a previous attempt to add "alias ports", not currently used.
        (void)parser.optional_field(ParagraphIdType);
        auto maybe_error = parser.error();
        if (auto error = maybe_error.get())
        {
            return std::move(*error);
        }

        return spgh;
    }

    static ExpectedL<std::unique_ptr<FeatureParagraph>> parse_feature_paragraph(StringView origin, Paragraph&& fields)
    {
        ParagraphParser parser(origin, std::move(fields));

        auto fpgh = std::make_unique<FeatureParagraph>();

        fpgh->name = parser.required_field(ParagraphIdFeature);
        fpgh->description = Strings::split(parser.required_field(ParagraphIdDescription), '\n');
        trim_all(fpgh->description);

        auto maybe_dependencies = parse_dependencies_list(parser.optional_field(ParagraphIdBuildDepends), origin);
        if (maybe_dependencies.has_value())
        {
            fpgh->dependencies = maybe_dependencies.value_or_exit(VCPKG_LINE_INFO);
        }
        else
        {
            return std::move(maybe_dependencies).error();
        }

        auto maybe_error = parser.error();
        if (auto error = maybe_error.get())
        {
            return std::move(*error);
        }

        return fpgh;
    }

    ExpectedL<std::unique_ptr<SourceControlFile>> SourceControlFile::parse_control_file(
        StringView origin, std::vector<Paragraph>&& control_paragraphs)
    {
        auto control_file = std::make_unique<SourceControlFile>();

        auto maybe_source = parse_source_paragraph(origin, std::move(control_paragraphs.front()));
        if (const auto source = maybe_source.get())
            control_file->core_paragraph = std::move(*source);
        else
            return std::move(maybe_source).error();

        control_paragraphs.erase(control_paragraphs.begin());

        for (auto&& feature_pgh : control_paragraphs)
        {
            auto maybe_feature = parse_feature_paragraph(origin, std::move(feature_pgh));
            if (const auto feature = maybe_feature.get())
                control_file->feature_paragraphs.emplace_back(std::move(*feature));
            else
                return std::move(maybe_feature).error();
        }

        auto maybe_error = canonicalize(*control_file);
        if (auto error = maybe_error.get())
        {
            return std::move(*error);
        }

        return control_file;
    }

    struct PlatformExprDeserializer final : Json::IDeserializer<PlatformExpression::Expr>
    {
        virtual LocalizedString type_name() const override { return msg::format(msgAPlatformExpression); }

        virtual Optional<PlatformExpression::Expr> visit_string(Json::Reader& r, StringView sv) const override
        {
            auto opt =
                PlatformExpression::parse_platform_expression(sv, PlatformExpression::MultipleBinaryOperators::Deny);
            if (auto res = opt.get())
            {
                return std::move(*res);
            }
            else
            {
                r.add_generic_error(type_name(), std::move(opt).error());
                return PlatformExpression::Expr();
            }
        }

        static const PlatformExprDeserializer instance;
    };
    const PlatformExprDeserializer PlatformExprDeserializer::instance;

    struct DefaultFeatureNameDeserializer : Json::IDeserializer<std::string>
    {
        virtual LocalizedString type_name() const override { return msg::format(msgADefaultFeature); }
        virtual Optional<std::string> visit_string(Json::Reader& r, StringView sv) const override
        {
            if (sv == FeatureNameCore)
            {
                r.add_generic_error(type_name(), msg::format(msgDefaultFeatureCore));
                return sv.to_string();
            }

            if (sv == FeatureNameDefault)
            {
                r.add_generic_error(type_name(), msg::format(msgDefaultFeatureDefault));
                return sv.to_string();
            }

            return Json::FeatureNameDeserializer::instance.visit_string(r, sv);
        }
        static const DefaultFeatureNameDeserializer instance;
    };

    const DefaultFeatureNameDeserializer DefaultFeatureNameDeserializer::instance;

    struct DefaultFeatureDeserializer : Json::IDeserializer<DependencyRequestedFeature>
    {
        LocalizedString type_name() const override { return msg::format(msgADefaultFeature); }

        Span<const StringView> valid_fields() const override
        {
            static const StringView t[] = {
                JsonIdName,
                JsonIdPlatform,
            };
            return t;
        }

        Optional<DependencyRequestedFeature> visit_string(Json::Reader& r, StringView sv) const override
        {
            return DefaultFeatureNameDeserializer::instance.visit_string(r, sv).map(
                [](std::string&& name) { return DependencyRequestedFeature{std::move(name)}; });
        }

        Optional<DependencyRequestedFeature> visit_object(Json::Reader& r, const Json::Object& obj) const override
        {
            DependencyRequestedFeature answer;
            r.required_object_field(
                type_name(), obj, JsonIdName, answer.name, DefaultFeatureNameDeserializer::instance);
            r.optional_object_field(obj, JsonIdPlatform, answer.platform, PlatformExprDeserializer::instance);
            return answer;
        }

        const static DefaultFeatureDeserializer instance;
    };
    const DefaultFeatureDeserializer DefaultFeatureDeserializer::instance;

    struct DefaultFeatureArrayDeserializer : Json::ArrayDeserializer<DefaultFeatureDeserializer>
    {
        LocalizedString type_name() const override { return msg::format(msgAnArrayOfDefaultFeatures); }

        static const DefaultFeatureArrayDeserializer instance;
    };
    const DefaultFeatureArrayDeserializer DefaultFeatureArrayDeserializer::instance;

    struct DependencyFeatureNameDeserializer : Json::IDeserializer<std::string>
    {
        virtual LocalizedString type_name() const override { return msg::format(msgAFeatureName); }
        virtual Optional<std::string> visit_string(Json::Reader& r, StringView sv) const override
        {
            if (sv == FeatureNameCore)
            {
                r.add_generic_error(type_name(), msg::format(msgDependencyFeatureCore));
                return sv.to_string();
            }

            if (sv == FeatureNameDefault)
            {
                r.add_generic_error(type_name(), msg::format(msgDependencyFeatureDefault));
                return sv.to_string();
            }

            return Json::FeatureNameDeserializer::instance.visit_string(r, sv);
        }
        static const DependencyFeatureNameDeserializer instance;
    };

    const DependencyFeatureNameDeserializer DependencyFeatureNameDeserializer::instance;

    struct DependencyFeatureDeserializer : Json::IDeserializer<DependencyRequestedFeature>
    {
        LocalizedString type_name() const override { return msg::format(msgADependencyFeature); }

        Span<const StringView> valid_fields() const override
        {
            static const StringView t[] = {
                JsonIdName,
                JsonIdPlatform,
            };
            return t;
        }

        Optional<DependencyRequestedFeature> visit_string(Json::Reader& r, StringView sv) const override
        {
            return DependencyFeatureNameDeserializer::instance.visit_string(r, sv).map(
                [](std::string&& name) { return DependencyRequestedFeature{std::move(name)}; });
        }

        Optional<DependencyRequestedFeature> visit_object(Json::Reader& r, const Json::Object& obj) const override
        {
            DependencyRequestedFeature result;
            r.required_object_field(
                type_name(), obj, JsonIdName, result.name, DependencyFeatureNameDeserializer::instance);
            r.optional_object_field(obj, JsonIdPlatform, result.platform, PlatformExprDeserializer::instance);
            return result;
        }

        const static DependencyFeatureDeserializer instance;
    };
    const DependencyFeatureDeserializer DependencyFeatureDeserializer::instance;

    struct DependencyFeatureArrayDeserializer : Json::ArrayDeserializer<DependencyFeatureDeserializer>
    {
        LocalizedString type_name() const override { return msg::format(msgAnArrayOfFeatures); }

        static const DependencyFeatureArrayDeserializer instance;
    };
    const DependencyFeatureArrayDeserializer DependencyFeatureArrayDeserializer::instance;

    struct DependencyDeserializer final : Json::IDeserializer<Dependency>
    {
        virtual LocalizedString type_name() const override { return msg::format(msgADependency); }

        virtual Span<const StringView> valid_fields() const override
        {
            static constexpr StringView t[] = {
                JsonIdName,
                JsonIdHost,
                JsonIdFeatures,
                JsonIdDefaultFeatures,
                JsonIdPlatform,
                JsonIdVersionGreaterEqual,
            };

            return t;
        }

        virtual Optional<Dependency> visit_string(Json::Reader& r, StringView sv) const override
        {
            return Json::PackageNameDeserializer::instance.visit_string(r, sv).map(
                [](std::string&& name) { return Dependency{std::move(name)}; });
        }

        virtual Optional<Dependency> visit_object(Json::Reader& r, const Json::Object& obj) const override
        {
            Dependency dep;

            for (const auto& el : obj)
            {
                if (Strings::starts_with(el.first, "$"))
                {
                    dep.extra_info.insert_or_replace(el.first.to_string(), el.second);
                }
            }

            r.required_object_field(type_name(), obj, JsonIdName, dep.name, Json::PackageNameDeserializer::instance);
            r.optional_object_field(obj, JsonIdFeatures, dep.features, DependencyFeatureArrayDeserializer::instance);

            r.optional_object_field(
                obj, JsonIdDefaultFeatures, dep.default_features, Json::BooleanDeserializer::instance);
            r.optional_object_field(obj, JsonIdHost, dep.host, Json::BooleanDeserializer::instance);
            r.optional_object_field(obj, JsonIdPlatform, dep.platform, PlatformExprDeserializer::instance);

            std::string raw_version_ge_text;
            auto has_ge_constraint = r.optional_object_field(
                obj, JsonIdVersionGreaterEqual, raw_version_ge_text, VersionConstraintStringDeserializer::instance);
            if (has_ge_constraint)
            {
                dep.constraint.type = VersionConstraintKind::Minimum;
                auto maybe_version = Version::parse(raw_version_ge_text);
                if (auto version = maybe_version.get())
                {
                    dep.constraint.version = std::move(*version);
                }
                else
                {
                    r.add_generic_error(type_name(), msg::format(msgVersionConstraintPortVersionMustBePositiveInteger));
                }
            }

            return dep;
        }

        static DependencyDeserializer instance;
    };
    DependencyDeserializer DependencyDeserializer::instance;

    struct DependencyArrayDeserializer final : Json::IDeserializer<std::vector<Dependency>>
    {
        virtual LocalizedString type_name() const override { return msg::format(msgAnArrayOfDependencies); }

        virtual Optional<std::vector<Dependency>> visit_array(Json::Reader& r, const Json::Array& arr) const override
        {
            return r.array_elements(arr, DependencyDeserializer::instance);
        }

        static const DependencyArrayDeserializer instance;
    };

    const DependencyArrayDeserializer DependencyArrayDeserializer::instance;

    struct DependencyOverrideDeserializer final : Json::IDeserializer<DependencyOverride>
    {
        virtual LocalizedString type_name() const override { return msg::format(msgAnOverride); }

        virtual Span<const StringView> valid_fields() const override
        {
            static constexpr StringView u[] = {JsonIdName};
            static const auto t = Util::Vectors::concat<StringView>(schemed_deserializer_fields(), u);
            return t;
        }

        virtual Optional<DependencyOverride> visit_object(Json::Reader& r, const Json::Object& obj) const override
        {
            DependencyOverride dep;

            for (const auto& el : obj)
            {
                if (Strings::starts_with(el.first, "$"))
                {
                    dep.extra_info.insert_or_replace(el.first.to_string(), el.second);
                }
            }

            const auto type_name = this->type_name();
            r.required_object_field(type_name, obj, JsonIdName, dep.name, Json::PackageNameDeserializer::instance);
            auto schemed_version = visit_required_schemed_deserializer(type_name, r, obj, true);
            dep.version = std::move(schemed_version.version);
            dep.scheme = schemed_version.scheme;

            return dep;
        }

        static const DependencyOverrideDeserializer instance;
    };

    const DependencyOverrideDeserializer DependencyOverrideDeserializer::instance;

    struct DependencyOverrideArrayDeserializer : Json::ArrayDeserializer<DependencyOverrideDeserializer>
    {
        LocalizedString type_name() const override { return msg::format(msgAnArrayOfDependencyOverrides); }

        static const DependencyOverrideArrayDeserializer instance;
    };

    const DependencyOverrideArrayDeserializer DependencyOverrideArrayDeserializer::instance;

    static constexpr StringLiteral VALID_LICENSES[] = {
#include "spdx-licenses.inc"
    };
    static constexpr StringLiteral VALID_EXCEPTIONS[] = {
#include "spdx-exceptions.inc"
    };

    // The "license" field; either:
    // * a string, which must be an SPDX license expression.
    //   EBNF located at: https://learn.microsoft.com/vcpkg/reference/vcpkg-json#license
    // * `null`, for when the license of the package cannot be described by an SPDX expression
    struct SpdxLicenseExpressionParser : ParserBase
    {
        SpdxLicenseExpressionParser(StringView sv, StringView origin) : ParserBase(sv, origin) { }

        static const StringLiteral* case_insensitive_find(View<StringLiteral> lst, StringView id)
        {
            return Util::find_if(lst,
                                 [id](StringLiteral el) { return Strings::case_insensitive_ascii_equals(id, el); });
        }
        static constexpr bool is_idstring_element(char32_t ch) { return is_alphanumdash(ch) || ch == '.'; }

        enum class Expecting
        {
            License,        // at the beginning, or after a compound (AND, OR)
            Exception,      // after a WITH
            CompoundOrWith, // after a license
            Compound,       // after an exception (only one WITH is allowed), or after a close paren
        };

        void eat_idstring(std::string& result, Expecting& expecting)
        {
            auto loc = cur_loc();
            auto token = match_while(is_idstring_element);

            if (Strings::starts_with(token, "DocumentRef-"))
            {
                add_error(msg::format(msgLicenseExpressionDocumentRefUnsupported), loc);
                if (cur() == ':')
                {
                    next();
                }
                return;
            }
            else if (token == "AND" || token == "OR" || token == "WITH")
            {
                if (expecting == Expecting::License)
                {
                    add_error(msg::format(msgLicenseExpressionExpectLicenseFoundCompound, msg::value = token), loc);
                }
                if (expecting == Expecting::Exception)
                {
                    add_error(msg::format(msgLicenseExpressionExpectExceptionFoundCompound, msg::value = token), loc);
                }

                if (token == "WITH")
                {
                    if (expecting == Expecting::Compound)
                    {
                        add_error(msg::format(msgLicenseExpressionExpectCompoundFoundWith), loc);
                    }
                    expecting = Expecting::Exception;
                }
                else
                {
                    expecting = Expecting::License;
                }

                result.push_back(' ');
                result.append(token.begin(), token.end());
                result.push_back(' ');
                return;
            }

            switch (expecting)
            {
                case Expecting::Compound:
                    add_error(msg::format(msgLicenseExpressionExpectCompoundFoundWord, msg::value = token), loc);
                    break;
                case Expecting::CompoundOrWith:
                    add_error(msg::format(msgLicenseExpressionExpectCompoundOrWithFoundWord, msg::value = token), loc);
                    break;
                case Expecting::License:
                    if (Strings::starts_with(token, "LicenseRef-"))
                    {
                        result.append(token.begin(), token.end());
                    }
                    else
                    {
                        auto it = case_insensitive_find(VALID_LICENSES, token);
                        if (it != std::end(VALID_LICENSES))
                        {
                            result.append(it->begin(), it->end());
                        }
                        else
                        {
                            add_warning(msg::format(msgLicenseExpressionUnknownLicense, msg::value = token), loc);
                            result.append(token.begin(), token.end());
                        }

                        if (cur() == '+')
                        {
                            next();
                            result.push_back('+');
                        }
                    }
                    expecting = Expecting::CompoundOrWith;
                    break;
                case Expecting::Exception:
                    auto it = case_insensitive_find(VALID_EXCEPTIONS, token);
                    if (it != std::end(VALID_EXCEPTIONS))
                    {
                        // case normalization
                        result.append(it->begin(), it->end());
                    }
                    else
                    {
                        add_warning(msg::format(msgLicenseExpressionUnknownException, msg::value = token), loc);
                        result.append(token.begin(), token.end());
                    }
                    expecting = Expecting::Compound;
                    break;
            }
        }

        std::string parse()
        {
            if (cur() == Unicode::end_of_file)
            {
                add_error(msg::format(msgEmptyLicenseExpression));
                return {};
            }

            Expecting expecting = Expecting::License;
            std::string result;

            size_t open_parens = 0;
            while (!at_eof())
            {
                skip_whitespace();
                switch (cur())
                {
                    case '(':
                        if (expecting == Expecting::Compound || expecting == Expecting::CompoundOrWith)
                        {
                            add_error(msg::format(msgLicenseExpressionExpectCompoundFoundParen));
                        }
                        if (expecting == Expecting::Exception)
                        {
                            add_error(msg::format(msgLicenseExpressionExpectExceptionFoundParen));
                        }
                        result.push_back('(');
                        expecting = Expecting::License;
                        ++open_parens;
                        next();
                        break;
                    case ')':
                        if (expecting == Expecting::License)
                        {
                            add_error(msg::format(msgLicenseExpressionExpectLicenseFoundParen));
                        }
                        else if (expecting == Expecting::Exception)
                        {
                            add_error(msg::format(msgLicenseExpressionExpectExceptionFoundParen));
                        }
                        if (open_parens == 0)
                        {
                            add_error(msg::format(msgLicenseExpressionImbalancedParens));
                        }
                        result.push_back(')');
                        expecting = Expecting::Compound;
                        --open_parens;
                        next();
                        break;
                    case '+':
                        add_error(msg::format(msgLicenseExpressionContainsExtraPlus));
                        next();
                        break;
                    default:
                        if (cur() > 0x7F)
                        {
                            auto ch = cur();
                            auto first = it().pointer_to_current();
                            next();
                            auto last = it().pointer_to_current();
                            add_error(msg::format(msgLicenseExpressionContainsUnicode,
                                                  msg::value = static_cast<uint32_t>(ch),
                                                  msg::pretty_value = StringView{first, last}));
                            break;
                        }
                        if (!is_idstring_element(cur()))
                        {
                            add_error(msg::format(msgLicenseExpressionContainsInvalidCharacter,
                                                  msg::value = static_cast<char>(cur())));
                            next();
                            break;
                        }
                        eat_idstring(result, expecting);
                        break;
                }
            }

            if (expecting == Expecting::License)
            {
                add_error(msg::format(msgLicenseExpressionExpectLicenseFoundEof));
            }
            if (expecting == Expecting::Exception)
            {
                add_error(msg::format(msgLicenseExpressionExpectExceptionFoundEof));
            }

            return result;
        }
    };

    std::string parse_spdx_license_expression(StringView sv, ParseMessages& messages)
    {
        auto license_string = msg::format(msgLicenseExpressionString); // must live through parse
        auto parser = SpdxLicenseExpressionParser(sv, license_string);
        auto result = parser.parse();
        messages = parser.extract_messages();
        return result;
    }

    struct LicenseExpressionDeserializer final : Json::IDeserializer<std::string>
    {
        virtual LocalizedString type_name() const override { return msg::format(msgAnSpdxLicenseExpression); }

        virtual Optional<std::string> visit_null(Json::Reader&) const override { return {std::string()}; }

        // if `sv` is a valid SPDX license expression, returns sv,
        // but with whitespace normalized
        virtual Optional<std::string> visit_string(Json::Reader& r, StringView sv) const override
        {
            auto parser = SpdxLicenseExpressionParser(sv, "");
            auto res = parser.parse();

            for (const auto& warning : parser.messages().warnings)
            {
                r.add_warning(type_name(), warning.format("", MessageKind::Warning));
            }
            if (auto err = parser.get_error())
            {
                r.add_generic_error(type_name(), LocalizedString::from_raw(err->to_string()));
                return std::string();
            }

            return res;
        }

        static const LicenseExpressionDeserializer instance;
    };

    const LicenseExpressionDeserializer LicenseExpressionDeserializer::instance;

    // reasoning for these two distinct types -- FeatureDeserializer and ArrayFeatureDeserializer:
    // `"features"` may be defined in one of two ways:
    // - An array of feature objects, which contains the `"name"` field
    // - An object mapping feature names to feature objects, which do not contain the `"name"` field
    // `ArrayFeatureDeserializer` is used for the former, `FeatureDeserializer` is used for the latter.
    struct FeatureDeserializer final : Json::IDeserializer<std::unique_ptr<FeatureParagraph>>
    {
        virtual LocalizedString type_name() const override { return msg::format(msgAFeature); }

        virtual Span<const StringView> valid_fields() const override
        {
            static constexpr StringView t[] = {JsonIdDescription, JsonIdDependencies, JsonIdSupports, JsonIdLicense};
            return t;
        }

        virtual Optional<std::unique_ptr<FeatureParagraph>> visit_object(Json::Reader& r,
                                                                         const Json::Object& obj) const override
        {
            auto feature = std::make_unique<FeatureParagraph>();
            for (const auto& el : obj)
            {
                if (Strings::starts_with(el.first, "$"))
                {
                    feature->extra_info.insert_or_replace(el.first.to_string(), el.second);
                }
            }

            r.required_object_field(
                type_name(), obj, JsonIdDescription, feature->description, Json::ParagraphDeserializer::instance);
            r.optional_object_field(
                obj, JsonIdDependencies, feature->dependencies, DependencyArrayDeserializer::instance);
            r.optional_object_field(
                obj, JsonIdSupports, feature->supports_expression, PlatformExprDeserializer::instance);
            std::string license;
            if (r.optional_object_field(obj, JsonIdLicense, license, LicenseExpressionDeserializer::instance))
            {
                feature->license = {std::move(license)};
            }

            return std::move(feature); // gcc-7 bug workaround redundant move
        }

        static const FeatureDeserializer instance;
    };

    const FeatureDeserializer FeatureDeserializer::instance;

    struct FeaturesObject
    {
        std::vector<std::unique_ptr<FeatureParagraph>> feature_paragraphs;
        Json::Object extra_features_info;
    };

    struct FeaturesFieldDeserializer final : Json::IDeserializer<FeaturesObject>
    {
        virtual LocalizedString type_name() const override { return msg::format(msgASetOfFeatures); }

        virtual Span<const StringView> valid_fields() const override { return {}; }

        virtual Optional<FeaturesObject> visit_object(Json::Reader& r, const Json::Object& obj) const override
        {
            FeaturesObject res;
            std::vector<std::string> extra_fields;

            for (const auto& pr : obj)
            {
                if (Strings::starts_with(pr.first, "$"))
                {
                    res.extra_features_info.insert(pr.first.to_string(), pr.second);
                    continue;
                }
                if (!Json::IdentifierDeserializer::is_ident(pr.first))
                {
                    r.add_generic_error(type_name(), msg::format(msgInvalidFeature));
                    continue;
                }
                std::unique_ptr<FeatureParagraph> v;
                r.visit_in_key(pr.second, pr.first, v, FeatureDeserializer::instance);
                if (v)
                {
                    v->name = pr.first.to_string();
                    res.feature_paragraphs.push_back(std::move(v));
                }
            }

            return std::move(res); // gcc-7 bug workaround redundant move
        }

        static const FeaturesFieldDeserializer instance;
    };

    const FeaturesFieldDeserializer FeaturesFieldDeserializer::instance;

    struct ContactsDeserializer final : Json::IDeserializer<Json::Object>
    {
        virtual LocalizedString type_name() const override { return msg::format(msgADictionaryOfContacts); }

        virtual Optional<Json::Object> visit_object(Json::Reader& r, const Json::Object& obj) const override
        {
            (void)r;
            return obj;
        }

        static const ContactsDeserializer instance;
    };

    const ContactsDeserializer ContactsDeserializer::instance;

    struct BaselineCommitDeserializer final : Json::IDeserializer<std::string>
    {
        virtual LocalizedString type_name() const override { return msg::format(msgAVcpkgRepositoryCommit); }

        virtual Optional<std::string> visit_string(Json::Reader&, StringView s) const override
        {
            // We allow non-sha strings here to allow the core vcpkg code to provide better error
            // messages including the current git commit
            return s.to_string();
        }

        static BaselineCommitDeserializer instance;
    };
    BaselineCommitDeserializer BaselineCommitDeserializer::instance;

    struct ManifestDeserializer : Json::IDeserializer<std::unique_ptr<SourceControlFile>>
    {
        virtual LocalizedString type_name() const override { return msg::format(msgAManifest); }

        virtual Span<const StringView> valid_fields() const override
        {
            static constexpr StringView u[] = {
                JsonIdName,
                JsonIdMaintainers,
                JsonIdContacts,
                JsonIdSummary,
                JsonIdDescription,
                JsonIdHomepage,
                JsonIdDocumentation,
                JsonIdLicense,
                JsonIdDependencies,
                JsonIdFeatures,
                JsonIdDefaultFeatures,
                JsonIdSupports,
                JsonIdOverrides,
                JsonIdBuiltinBaseline,
                JsonIdVcpkgConfiguration,
            };
            static const auto t = Util::Vectors::concat<StringView>(schemed_deserializer_fields(), u);

            return t;
        }

        vcpkg::Optional<std::unique_ptr<vcpkg::SourceControlFile>> visit_object_common(
            const vcpkg::Json::Object& obj,
            vcpkg::SourceParagraph& spgh,
            vcpkg::Json::Reader& r,
            std::unique_ptr<vcpkg::SourceControlFile>& control_file) const
        {
            for (const auto& el : obj)
            {
                if (Strings::starts_with(el.first, "$"))
                {
                    spgh.extra_info.insert_or_replace(el.first.to_string(), el.second);
                }
            }

            r.optional_object_field(obj, JsonIdMaintainers, spgh.maintainers, Json::ParagraphDeserializer::instance);
            r.optional_object_field(obj, JsonIdContacts, spgh.contacts, ContactsDeserializer::instance);
            r.optional_object_field(obj, JsonIdSummary, spgh.summary, Json::ParagraphDeserializer::instance);
            r.optional_object_field(obj, JsonIdDescription, spgh.description, Json::ParagraphDeserializer::instance);
            r.optional_object_field(obj, JsonIdHomepage, spgh.homepage, UrlDeserializer::instance);
            r.optional_object_field(obj, JsonIdDocumentation, spgh.documentation, UrlDeserializer::instance);

            std::string license;
            if (r.optional_object_field(obj, JsonIdLicense, license, LicenseExpressionDeserializer::instance))
            {
                spgh.license = {std::move(license)};
            }

            r.optional_object_field(obj, JsonIdDependencies, spgh.dependencies, DependencyArrayDeserializer::instance);
            r.optional_object_field(
                obj, JsonIdOverrides, spgh.overrides, DependencyOverrideArrayDeserializer::instance);

            std::string baseline;
            if (r.optional_object_field(obj, JsonIdBuiltinBaseline, baseline, BaselineCommitDeserializer::instance))
            {
                spgh.builtin_baseline = std::move(baseline);
            }

            r.optional_object_field(obj, JsonIdSupports, spgh.supports_expression, PlatformExprDeserializer::instance);
            r.optional_object_field(
                obj, JsonIdDefaultFeatures, spgh.default_features, DefaultFeatureArrayDeserializer::instance);

            FeaturesObject features_tmp;
            r.optional_object_field(obj, JsonIdFeatures, features_tmp, FeaturesFieldDeserializer::instance);
            control_file->feature_paragraphs = std::move(features_tmp.feature_paragraphs);
            control_file->extra_features_info = std::move(features_tmp.extra_features_info);

            if (auto configuration = obj.get(JsonIdVcpkgConfiguration))
            {
                if (configuration->is_object())
                {
                    spgh.vcpkg_configuration.emplace(configuration->object(VCPKG_LINE_INFO));
                }
                else
                {
                    r.add_generic_error(type_name(),
                                        msg::format(msgJsonFieldNotObject, msg::json_field = JsonIdVcpkgConfiguration));
                }
            }

            auto maybe_error = canonicalize(*control_file);
            if (auto error = maybe_error.get())
            {
                Checks::msg_exit_with_message(VCPKG_LINE_INFO, *error);
            }

            return std::move(control_file); // gcc-7 bug workaround redundant move
        }
    };

    struct ProjectManifestDeserializer final : ManifestDeserializer
    {
        virtual Optional<std::unique_ptr<SourceControlFile>> visit_object(Json::Reader& r,
                                                                          const Json::Object& obj) const override
        {
            auto control_file = std::make_unique<SourceControlFile>();
            control_file->core_paragraph = std::make_unique<SourceParagraph>();

            auto& spgh = *control_file->core_paragraph;

            r.optional_object_field(obj, JsonIdName, spgh.name, Json::PackageNameDeserializer::instance);
            auto maybe_schemed_version = visit_optional_schemed_deserializer(type_name(), r, obj, false);
            if (auto p = maybe_schemed_version.get())
            {
                spgh.version_scheme = p->scheme;
                spgh.version = p->version;
            }
            else
            {
                spgh.version_scheme = VersionScheme::Missing;
            }

            return visit_object_common(obj, spgh, r, control_file);
        }

        static const ProjectManifestDeserializer instance;
    };

    const ProjectManifestDeserializer ProjectManifestDeserializer::instance;

    struct PortManifestDeserializer final : ManifestDeserializer
    {
        virtual Optional<std::unique_ptr<SourceControlFile>> visit_object(Json::Reader& r,
                                                                          const Json::Object& obj) const override
        {
            auto control_file = std::make_unique<SourceControlFile>();
            control_file->core_paragraph = std::make_unique<SourceParagraph>();

            auto& spgh = *control_file->core_paragraph;

            r.required_object_field(type_name(), obj, JsonIdName, spgh.name, Json::PackageNameDeserializer::instance);
            auto schemed_version = visit_required_schemed_deserializer(type_name(), r, obj, false);
            spgh.version_scheme = schemed_version.scheme;
            spgh.version = schemed_version.version;

            return visit_object_common(obj, spgh, r, control_file);
        }

        static const PortManifestDeserializer instance;
    };

    const PortManifestDeserializer PortManifestDeserializer::instance;

    // Extracts just the configuration information from a manifest object
    struct ManifestConfigurationDeserializer final : Json::IDeserializer<ManifestConfiguration>
    {
        virtual LocalizedString type_name() const override { return msg::format(msgAManifest); }

        virtual Optional<ManifestConfiguration> visit_object(Json::Reader& r, const Json::Object& obj) const override
        {
            Optional<ManifestConfiguration> x;
            ManifestConfiguration& ret = x.emplace();
            if (!r.optional_object_field(
                    obj, JsonIdVcpkgConfiguration, ret.config.emplace(), get_configuration_deserializer()))
            {
                ret.config = nullopt;
            }
            if (!r.optional_object_field(
                    obj, JsonIdBuiltinBaseline, ret.builtin_baseline.emplace(), BaselineCommitDeserializer::instance))
            {
                ret.builtin_baseline = nullopt;
            }
            return x;
        }

        static ManifestConfigurationDeserializer instance;
    };
    ManifestConfigurationDeserializer ManifestConfigurationDeserializer::instance;

    ExpectedL<ManifestConfiguration> parse_manifest_configuration(const Json::Object& manifest,
                                                                  StringView origin,
                                                                  MessageSink& warningsSink)
    {
        Json::Reader reader(origin);
        auto res = reader.visit(manifest, ManifestConfigurationDeserializer::instance);

        if (!reader.warnings().empty())
        {
            warningsSink.println(Color::warning, msgWarnOnParseConfig, msg::path = origin);
            for (auto&& warning : reader.warnings())
            {
                warningsSink.println(Color::warning, LocalizedString::from_raw(warning));
            }
            warningsSink.println(Color::warning, msgExtendedDocumentationAtUrl, msg::url = docs::registries_url);
            warningsSink.println(Color::warning, msgExtendedDocumentationAtUrl, msg::url = docs::manifests_url);
        }

        if (!reader.errors().empty())
        {
            LocalizedString ret;
            ret.append(msgFailedToParseConfig, msg::path = origin);
            ret.append_raw('\n');
            for (auto&& err : reader.errors())
            {
                ret.append_indent().append(err).append_raw("\n");
            }
            ret.append(msgExtendedDocumentationAtUrl, msg::url = docs::registries_url);
            ret.append_raw('\n');
            ret.append(msgExtendedDocumentationAtUrl, msg::url = docs::manifests_url);
            ret.append_raw('\n');
            return std::move(ret);
        }

        return std::move(res).value_or_exit(VCPKG_LINE_INFO);
    }

    SourceControlFile SourceControlFile::clone() const
    {
        SourceControlFile ret;
        ret.core_paragraph = std::make_unique<SourceParagraph>(*core_paragraph);
        for (const auto& feat_ptr : feature_paragraphs)
        {
            ret.feature_paragraphs.push_back(std::make_unique<FeatureParagraph>(*feat_ptr));
        }
        return ret;
    }

    template<class ManifestDeserializerType>
    static ExpectedL<std::unique_ptr<SourceControlFile>> parse_manifest_object_impl(StringView control_path,
                                                                                    const Json::Object& manifest,
                                                                                    MessageSink& warnings_sink)
    {
        Json::Reader reader(control_path);

        auto res = reader.visit(manifest, ManifestDeserializerType::instance);

        for (auto&& w : reader.warnings())
        {
            warnings_sink.println(Color::warning, w);
        }

        switch (reader.errors().size())
        {
            case 0:
                if (auto p = res.get())
                {
                    return std::move(*p);
                }
                else
                {
                    Checks::unreachable(VCPKG_LINE_INFO);
                }
            case 1: return reader.errors()[0];
            default:
            {
                LocalizedString result;
                auto first = reader.errors().begin();
                const auto last = reader.errors().end();
                for (;;)
                {
                    result.append(*first);
                    if (++first == last)
                    {
                        return result;
                    }

                    result.append_raw('\n');
                }
            }
        }
    }

    ExpectedL<std::unique_ptr<SourceControlFile>> SourceControlFile::parse_project_manifest_object(
        StringView control_path, const Json::Object& manifest, MessageSink& warnings_sink)
    {
        return parse_manifest_object_impl<ProjectManifestDeserializer>(control_path, manifest, warnings_sink);
    }

    ExpectedL<std::unique_ptr<SourceControlFile>> SourceControlFile::parse_port_manifest_object(
        StringView control_path, const Json::Object& manifest, MessageSink& warnings_sink)
    {
        return parse_manifest_object_impl<PortManifestDeserializer>(control_path, manifest, warnings_sink);
    }

    ExpectedL<Unit> SourceControlFile::check_against_feature_flags(const Path& origin,
                                                                   const FeatureFlagSettings& flags,
                                                                   bool is_default_builtin_registry) const
    {
        if (!flags.versions)
        {
            auto check_deps = [&](View<Dependency> deps) -> ExpectedL<Unit> {
                for (auto&& dep : deps)
                {
                    if (dep.constraint.type != VersionConstraintKind::None)
                    {
                        get_global_metrics_collector().track_define(DefineMetric::ErrorVersioningDisabled);
                        return msg::format_error(
                            msgVersionRejectedDueToFeatureFlagOff, msg::path = origin, msg::json_field = "version>=");
                    }
                }

                return Unit{};
            };

            {
                auto maybe_good = check_deps(core_paragraph->dependencies);
                if (!maybe_good)
                {
                    return maybe_good;
                }
            }

            for (auto&& fpgh : feature_paragraphs)
            {
                auto maybe_good = check_deps(fpgh->dependencies);
                if (!maybe_good)
                {
                    return maybe_good;
                }
            }

            if (core_paragraph->overrides.size() != 0)
            {
                get_global_metrics_collector().track_define(DefineMetric::ErrorVersioningDisabled);
                return msg::format_error(
                    msgVersionRejectedDueToFeatureFlagOff, msg::path = origin, msg::json_field = JsonIdOverrides);
            }

            if (core_paragraph->builtin_baseline.has_value())
            {
                get_global_metrics_collector().track_define(DefineMetric::ErrorVersioningDisabled);
                return msg::format_error(
                    msgVersionRejectedDueToFeatureFlagOff, msg::path = origin, msg::json_field = JsonIdBuiltinBaseline);
            }
        }
        else
        {
            if (!core_paragraph->builtin_baseline.has_value() && is_default_builtin_registry)
            {
                if (std::any_of(core_paragraph->dependencies.begin(),
                                core_paragraph->dependencies.end(),
                                [](const auto& dependency) {
                                    return dependency.constraint.type != VersionConstraintKind::None;
                                }))
                {
                    get_global_metrics_collector().track_define(DefineMetric::ErrorVersioningNoBaseline);
                    return msg::format_error(
                        msgVersionRejectedDueToBaselineMissing, msg::path = origin, msg::json_field = "version>=");
                }

                if (!core_paragraph->overrides.empty())
                {
                    get_global_metrics_collector().track_define(DefineMetric::ErrorVersioningNoBaseline);
                    return msg::format_error(
                        msgVersionRejectedDueToBaselineMissing, msg::path = origin, msg::json_field = "overrides");
                }
            }
        }

        return Unit{};
    }

    static const char* after_nl(const char* first, const char* last)
    {
        const auto it = std::find(first, last, '\n');
        return it == last ? last : it + 1;
    }

    static bool starts_with_error(StringView sv)
    {
        return Strings::starts_with(sv, "Error") || Strings::starts_with(sv, "error: ");
    }

    void print_error_message(const LocalizedString& message)
    {
        // To preserve previous behavior, each line starting with "Error" should be error-colored. All other lines
        // should be neutral color.

        // To minimize the number of print calls on Windows (which is a significant performance bottleneck), this
        // algorithm chunks groups of similarly-colored lines.
        const char* start_of_chunk = message.data().data();
        const char* end_of_chunk = start_of_chunk;
        const char* const last = start_of_chunk + message.data().size();
        while (end_of_chunk != last)
        {
            while (end_of_chunk != last && starts_with_error({end_of_chunk, last}))
            {
                end_of_chunk = after_nl(end_of_chunk, last);
            }
            if (start_of_chunk != end_of_chunk)
            {
                msg::write_unlocalized_text(Color::error, StringView{start_of_chunk, end_of_chunk});
                start_of_chunk = end_of_chunk;
            }

            while (end_of_chunk != last && !starts_with_error({end_of_chunk, last}))
            {
                end_of_chunk = after_nl(end_of_chunk, last);
            }
            if (start_of_chunk != end_of_chunk)
            {
                msg::write_unlocalized_text(Color::error, StringView{start_of_chunk, end_of_chunk});
                start_of_chunk = end_of_chunk;
            }
        }

        msg::println();
    }

    Optional<const FeatureParagraph&> SourceControlFile::find_feature(StringView featurename) const
    {
        auto it = Util::find_if(feature_paragraphs,
                                [&](const std::unique_ptr<FeatureParagraph>& p) { return p->name == featurename; });
        if (it != feature_paragraphs.end())
            return **it;
        else
            return nullopt;
    }

    bool SourceControlFile::has_qualified_dependencies() const
    {
        for (auto&& dep : core_paragraph->dependencies)
        {
            if (!dep.platform.is_empty()) return true;
        }
        for (auto&& fpgh : feature_paragraphs)
        {
            for (auto&& dep : fpgh->dependencies)
            {
                if (!dep.platform.is_empty()) return true;
            }
        }
        return false;
    }

    Optional<const std::vector<Dependency>&> SourceControlFile::find_dependencies_for_feature(
        const std::string& featurename) const
    {
        if (featurename == "core")
        {
            return core_paragraph->dependencies;
        }
        else if (auto p_feature = find_feature(featurename).get())
            return p_feature->dependencies;
        else
            return nullopt;
    }

    std::vector<FullPackageSpec> filter_dependencies(const std::vector<vcpkg::Dependency>& deps,
                                                     Triplet target,
                                                     Triplet host,
                                                     const std::unordered_map<std::string, std::string>& cmake_vars)
    {
        std::vector<FullPackageSpec> ret;
        for (auto&& dep : deps)
        {
            if (dep.platform.evaluate(cmake_vars))
            {
                std::vector<std::string> features;
                features.reserve(dep.features.size());
                for (const auto& f : dep.features)
                {
                    if (f.platform.evaluate(cmake_vars)) features.push_back(f.name);
                }
                ret.emplace_back(dep.to_full_spec(features, target, host));
            }
        }
        return ret;
    }

    static bool is_dependency_trivial(const Dependency& dep)
    {
        return dep.features.empty() && dep.default_features && dep.platform.is_empty() && dep.extra_info.is_empty() &&
               dep.constraint.type == VersionConstraintKind::None && !dep.host;
    }

    Json::Object serialize_manifest(const SourceControlFile& scf)
    {
        auto serialize_paragraph =
            [&](Json::Object& obj, StringLiteral name, const std::vector<std::string>& pgh, bool always = false) {
                if (pgh.empty())
                {
                    if (always)
                    {
                        obj.insert(name, Json::Array());
                    }
                    return;
                }
                if (pgh.size() == 1)
                {
                    obj.insert(name, pgh.front());
                    return;
                }

                auto& arr = obj.insert(name, Json::Array());
                for (const auto& s : pgh)
                {
                    arr.push_back(Json::Value::string(s));
                }
            };
        auto serialize_optional_string = [&](Json::Object& obj, StringLiteral name, const std::string& s) {
            if (!s.empty())
            {
                obj.insert(name, s);
            }
        };
        auto serialize_dependency_features = [&](Json::Object& obj, StringLiteral name, const auto& features) {
            if (!features.empty())
            {
                auto& features_array = obj.insert(name, Json::Array());
                for (const auto& f : features)
                {
                    if (f.platform.is_empty())
                    {
                        features_array.push_back(Json::Value::string(f.name));
                    }
                    else
                    {
                        Json::Object entry;
                        entry.insert(JsonIdName, f.name);
                        entry.insert(JsonIdPlatform, to_string(f.platform));
                        features_array.push_back(std::move(entry));
                    }
                }
            }
        };
        auto serialize_dependency = [&](Json::Array& arr, const Dependency& dep) {
            if (is_dependency_trivial(dep))
            {
                arr.push_back(Json::Value::string(dep.name));
            }
            else
            {
                auto& dep_obj = arr.push_back(Json::Object());
                for (const auto& el : dep.extra_info)
                {
                    dep_obj.insert(el.first.to_string(), el.second);
                }

                dep_obj.insert(JsonIdName, dep.name);
                if (dep.host) dep_obj.insert(JsonIdHost, Json::Value::boolean(true));

                if (!dep.default_features)
                {
                    dep_obj.insert(JsonIdDefaultFeatures, Json::Value::boolean(false));
                }
                serialize_dependency_features(dep_obj, JsonIdFeatures, dep.features);
                serialize_optional_string(dep_obj, JsonIdPlatform, to_string(dep.platform));
                if (dep.constraint.type == VersionConstraintKind::Minimum)
                {
                    dep_obj.insert(JsonIdVersionGreaterEqual, dep.constraint.version.to_string());
                }
            }
        };

        auto serialize_override = [&](Json::Array& arr, const DependencyOverride& dep) {
            auto& dep_obj = arr.push_back(Json::Object());
            for (const auto& el : dep.extra_info)
            {
                dep_obj.insert(el.first.to_string(), el.second);
            }

            dep_obj.insert(JsonIdName, Json::Value::string(dep.name));
            serialize_schemed_version(dep_obj, dep.scheme, dep.version);
        };

        auto serialize_license =
            [&](Json::Object& obj, StringLiteral name, const Optional<std::string>& maybe_license) {
                if (auto license = maybe_license.get())
                {
                    if (license->empty())
                    {
                        obj.insert(name, Json::Value::null(nullptr));
                    }
                    else
                    {
                        obj.insert(name, Json::Value::string(*license));
                    }
                }
            };

        Json::Object obj;

        for (const auto& el : scf.core_paragraph->extra_info)
        {
            obj.insert(el.first.to_string(), el.second);
        }

        if (auto configuration = scf.core_paragraph->vcpkg_configuration.get())
        {
            auto maybe_configuration = parse_configuration(*configuration, JsonIdVcpkgConfiguration, out_sink);
            obj.insert(JsonIdVcpkgConfiguration, maybe_configuration.value_or_exit(VCPKG_LINE_INFO).serialize());
        }

        serialize_optional_string(obj, JsonIdName, scf.to_name());

        auto version_scheme = scf.to_version_scheme();
        if (version_scheme != VersionScheme::Missing)
        {
            serialize_schemed_version(obj, version_scheme, scf.to_version());
        }

        serialize_paragraph(obj, JsonIdMaintainers, scf.core_paragraph->maintainers);
        if (scf.core_paragraph->contacts.size() > 0)
        {
            obj.insert(JsonIdContacts, scf.core_paragraph->contacts);
        }
        serialize_paragraph(obj, JsonIdSummary, scf.core_paragraph->summary);
        serialize_paragraph(obj, JsonIdDescription, scf.core_paragraph->description);

        serialize_optional_string(obj, JsonIdHomepage, scf.core_paragraph->homepage);
        serialize_optional_string(obj, JsonIdDocumentation, scf.core_paragraph->documentation);
        serialize_license(obj, JsonIdLicense, scf.core_paragraph->license);

        serialize_optional_string(obj, JsonIdSupports, to_string(scf.core_paragraph->supports_expression));
        if (scf.core_paragraph->builtin_baseline.has_value())
        {
            obj.insert(JsonIdBuiltinBaseline,
                       Json::Value::string(scf.core_paragraph->builtin_baseline.value_or_exit(VCPKG_LINE_INFO)));
        }

        if (!scf.core_paragraph->dependencies.empty())
        {
            auto& deps = obj.insert(JsonIdDependencies, Json::Array());

            for (const auto& dep : scf.core_paragraph->dependencies)
            {
                serialize_dependency(deps, dep);
            }
        }

        serialize_dependency_features(obj, JsonIdDefaultFeatures, scf.core_paragraph->default_features);

        if (!scf.feature_paragraphs.empty() || !scf.extra_features_info.is_empty())
        {
            auto& map = obj.insert(JsonIdFeatures, Json::Object());
            for (const auto& pr : scf.extra_features_info)
            {
                map.insert(pr.first.to_string(), pr.second);
            }
            for (const auto& feature : scf.feature_paragraphs)
            {
                auto& feature_obj = map.insert(feature->name, Json::Object());
                for (const auto& el : feature->extra_info)
                {
                    feature_obj.insert(el.first.to_string(), el.second);
                }

                serialize_paragraph(feature_obj, JsonIdDescription, feature->description, true);
                serialize_optional_string(feature_obj, JsonIdSupports, to_string(feature->supports_expression));
                serialize_license(feature_obj, JsonIdLicense, feature->license);

                if (!feature->dependencies.empty())
                {
                    auto& deps = feature_obj.insert(JsonIdDependencies, Json::Array());
                    for (const auto& dep : feature->dependencies)
                    {
                        serialize_dependency(deps, dep);
                    }
                }
            }
        }

        if (!scf.core_paragraph->overrides.empty())
        {
            auto& overrides = obj.insert(JsonIdOverrides, Json::Array());

            for (const auto& over : scf.core_paragraph->overrides)
            {
                serialize_override(overrides, over);
            }
        }

        return obj;
    }
}
