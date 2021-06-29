#include <vcpkg/base/checks.h>
#include <vcpkg/base/expected.h>
#include <vcpkg/base/jsonreader.h>
#include <vcpkg/base/span.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.print.h>
#include <vcpkg/base/util.h>

#include <vcpkg/metrics.h>
#include <vcpkg/packagespec.h>
#include <vcpkg/platform-expression.h>
#include <vcpkg/sourceparagraph.h>
#include <vcpkg/triplet.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/versiondeserializers.h>

namespace vcpkg
{
    using namespace vcpkg::Parse;

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
        if (lhs.version != rhs.version) return false;
        if (lhs.version_scheme != rhs.version_scheme) return false;
        if (lhs.port_version != rhs.port_version) return false;
        if (!paragraph_equal(lhs.description, rhs.description)) return false;
        if (!paragraph_equal(lhs.maintainers, rhs.maintainers)) return false;
        if (lhs.homepage != rhs.homepage) return false;
        if (lhs.documentation != rhs.documentation) return false;
        if (lhs.dependencies != rhs.dependencies) return false;
        if (lhs.default_features != rhs.default_features) return false;
        if (lhs.license != rhs.license) return false;

        if (lhs.type != rhs.type) return false;
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

    namespace SourceParagraphFields
    {
        static const std::string BUILD_DEPENDS = "Build-Depends";
        static const std::string DEFAULT_FEATURES = "Default-Features";
        static const std::string DESCRIPTION = "Description";
        static const std::string FEATURE = "Feature";
        static const std::string MAINTAINERS = "Maintainer";
        static const std::string NAME = "Source";
        static const std::string VERSION = "Version";
        static const std::string PORT_VERSION = "Port-Version";
        static const std::string HOMEPAGE = "Homepage";
        static const std::string TYPE = "Type";
        static const std::string SUPPORTS = "Supports";
    }

    static Span<const StringView> get_list_of_valid_fields()
    {
        static const StringView valid_fields[] = {
            SourceParagraphFields::NAME,
            SourceParagraphFields::VERSION,
            SourceParagraphFields::PORT_VERSION,
            SourceParagraphFields::DESCRIPTION,
            SourceParagraphFields::MAINTAINERS,
            SourceParagraphFields::BUILD_DEPENDS,
            SourceParagraphFields::HOMEPAGE,
            SourceParagraphFields::TYPE,
            SourceParagraphFields::SUPPORTS,
            SourceParagraphFields::DEFAULT_FEATURES,
        };

        return valid_fields;
    }

    void print_error_message(Span<const std::unique_ptr<Parse::ParseControlErrorInfo>> error_info_list);

    std::string Type::to_string(const Type& t)
    {
        switch (t.type)
        {
            case Type::ALIAS: return "Alias";
            case Type::PORT: return "Port";
            default: return "Unknown";
        }
    }

    Type Type::from_string(const std::string& t)
    {
        if (t == "Alias") return Type{Type::ALIAS};
        if (t == "Port" || t.empty()) return Type{Type::PORT};
        return Type{Type::UNKNOWN};
    }

    bool operator==(const Type& lhs, const Type& rhs) { return lhs.type == rhs.type; }
    bool operator!=(const Type& lhs, const Type& rhs) { return !(lhs == rhs); }

    static void trim_all(std::vector<std::string>& arr)
    {
        for (auto& el : arr)
        {
            el = Strings::trim(std::move(el));
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
                    if (std::lexicographical_compare(
                            lhs.features.begin(), lhs.features.end(), rhs.features.begin(), rhs.features.end()))
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
                std::sort(dep.features.begin(), dep.features.end());
                dep.extra_info.sort_keys();
            }
            void operator()(SourceParagraph& spgh) const
            {
                std::for_each(spgh.dependencies.begin(), spgh.dependencies.end(), *this);
                std::sort(spgh.dependencies.begin(), spgh.dependencies.end(), DependencyLess{});

                std::sort(spgh.default_features.begin(), spgh.default_features.end());

                spgh.extra_info.sort_keys();
            }
            void operator()(FeatureParagraph& fpgh) const
            {
                std::for_each(fpgh.dependencies.begin(), fpgh.dependencies.end(), *this);
                std::sort(fpgh.dependencies.begin(), fpgh.dependencies.end(), DependencyLess{});

                fpgh.extra_info.sort_keys();
            }
            [[nodiscard]] std::unique_ptr<ParseControlErrorInfo> operator()(SourceControlFile& scf) const
            {
                (*this)(*scf.core_paragraph);
                std::for_each(scf.feature_paragraphs.begin(), scf.feature_paragraphs.end(), *this);
                std::sort(scf.feature_paragraphs.begin(), scf.feature_paragraphs.end(), FeatureLess{});

                auto adjacent_equal =
                    std::adjacent_find(scf.feature_paragraphs.begin(), scf.feature_paragraphs.end(), FeatureEqual{});
                if (adjacent_equal != scf.feature_paragraphs.end())
                {
                    auto error_info = std::make_unique<ParseControlErrorInfo>();
                    error_info->name = scf.core_paragraph->name;
                    error_info->error = Strings::format(R"(Multiple features with the same name for port %s: %s
    This is invalid; please make certain that features have distinct names.)",
                                                        scf.core_paragraph->name,
                                                        (*adjacent_equal)->name);
                    return error_info;
                }
                return nullptr;
            }
        } canonicalize{};
    }

    static ParseExpected<SourceParagraph> parse_source_paragraph(const std::string& origin, Paragraph&& fields)
    {
        ParagraphParser parser(std::move(fields));

        auto spgh = std::make_unique<SourceParagraph>();

        parser.required_field(SourceParagraphFields::NAME, spgh->name);
        parser.required_field(SourceParagraphFields::VERSION, spgh->version);

        auto pv_str = parser.optional_field(SourceParagraphFields::PORT_VERSION);
        if (!pv_str.empty())
        {
            auto pv_opt = Strings::strto<int>(pv_str);
            if (auto pv = pv_opt.get())
            {
                spgh->port_version = *pv;
            }
            else
            {
                parser.add_type_error(SourceParagraphFields::PORT_VERSION, "a non-negative integer");
            }
        }

        spgh->description = Strings::split(parser.optional_field(SourceParagraphFields::DESCRIPTION), '\n');
        trim_all(spgh->description);

        spgh->maintainers = Strings::split(parser.optional_field(SourceParagraphFields::MAINTAINERS), '\n');
        trim_all(spgh->maintainers);

        spgh->homepage = parser.optional_field(SourceParagraphFields::HOMEPAGE);
        TextRowCol textrowcol;
        std::string buf;
        parser.optional_field(SourceParagraphFields::BUILD_DEPENDS, {buf, textrowcol});

        auto maybe_dependencies = parse_dependencies_list(buf, origin, textrowcol);
        if (maybe_dependencies.has_value())
        {
            spgh->dependencies = maybe_dependencies.value_or_exit(VCPKG_LINE_INFO);
        }
        else
        {
            auto error_info = std::make_unique<ParseControlErrorInfo>();
            error_info->name = origin;
            error_info->error = maybe_dependencies.error();
            return error_info;
        }

        buf.clear();
        parser.optional_field(SourceParagraphFields::DEFAULT_FEATURES, {buf, textrowcol});

        auto maybe_default_features = parse_default_features_list(buf, origin, textrowcol);
        if (maybe_default_features.has_value())
        {
            spgh->default_features = maybe_default_features.value_or_exit(VCPKG_LINE_INFO);
        }
        else
        {
            auto error_info = std::make_unique<ParseControlErrorInfo>();
            error_info->name = origin;
            error_info->error = maybe_default_features.error();
            return error_info;
        }

        auto supports_expr = parser.optional_field(SourceParagraphFields::SUPPORTS);
        if (!supports_expr.empty())
        {
            auto maybe_expr = PlatformExpression::parse_platform_expression(
                supports_expr, PlatformExpression::MultipleBinaryOperators::Allow);
            if (auto expr = maybe_expr.get())
            {
                spgh->supports_expression = std::move(*expr);
            }
            else
            {
                parser.add_type_error(SourceParagraphFields::SUPPORTS, "a platform expression");
            }
        }

        spgh->type = Type::from_string(parser.optional_field(SourceParagraphFields::TYPE));
        auto err = parser.error_info(spgh->name.empty() ? origin : spgh->name);
        if (err)
            return err;
        else
            return spgh;
    }

    static ParseExpected<FeatureParagraph> parse_feature_paragraph(const std::string& origin, Paragraph&& fields)
    {
        ParagraphParser parser(std::move(fields));

        auto fpgh = std::make_unique<FeatureParagraph>();

        parser.required_field(SourceParagraphFields::FEATURE, fpgh->name);
        fpgh->description = Strings::split(parser.required_field(SourceParagraphFields::DESCRIPTION), '\n');
        trim_all(fpgh->description);

        auto maybe_dependencies =
            parse_dependencies_list(parser.optional_field(SourceParagraphFields::BUILD_DEPENDS), origin);
        if (maybe_dependencies.has_value())
        {
            fpgh->dependencies = maybe_dependencies.value_or_exit(VCPKG_LINE_INFO);
        }
        else
        {
            auto error_info = std::make_unique<ParseControlErrorInfo>();
            error_info->name = origin;
            error_info->error = maybe_dependencies.error();
            return error_info;
        }

        auto err = parser.error_info(fpgh->name.empty() ? origin : fpgh->name);
        if (err)
            return err;
        else
            return fpgh;
    }

    ParseExpected<SourceControlFile> SourceControlFile::parse_control_file(
        const std::string& origin, std::vector<Parse::Paragraph>&& control_paragraphs)
    {
        if (control_paragraphs.size() == 0)
        {
            auto ret = std::make_unique<Parse::ParseControlErrorInfo>();
            ret->name = origin;
            return ret;
        }

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

        if (auto maybe_error = canonicalize(*control_file))
        {
            return std::move(maybe_error);
        }
        return control_file;
    }

    struct PlatformExprDeserializer : Json::IDeserializer<PlatformExpression::Expr>
    {
        virtual StringView type_name() const override { return "a platform expression"; }

        virtual Optional<PlatformExpression::Expr> visit_string(Json::Reader& r, StringView sv) override
        {
            auto opt =
                PlatformExpression::parse_platform_expression(sv, PlatformExpression::MultipleBinaryOperators::Deny);
            if (auto res = opt.get())
            {
                return std::move(*res);
            }
            else
            {
                r.add_generic_error(type_name(), opt.error());
                return PlatformExpression::Expr::Empty();
            }
        }

        static PlatformExprDeserializer instance;
    };
    PlatformExprDeserializer PlatformExprDeserializer::instance;

    struct DependencyDeserializer : Json::IDeserializer<Dependency>
    {
        virtual StringView type_name() const override { return "a dependency"; }

        constexpr static StringLiteral NAME = "name";
        constexpr static StringLiteral HOST = "host";
        constexpr static StringLiteral FEATURES = "features";
        constexpr static StringLiteral DEFAULT_FEATURES = "default-features";
        constexpr static StringLiteral PLATFORM = "platform";
        constexpr static StringLiteral VERSION_GE = "version>=";

        virtual Span<const StringView> valid_fields() const override
        {
            static const StringView t[] = {
                NAME,
                HOST,
                FEATURES,
                DEFAULT_FEATURES,
                PLATFORM,
                VERSION_GE,
            };

            return t;
        }

        virtual Optional<Dependency> visit_string(Json::Reader& r, StringView sv) override
        {
            if (!Json::PackageNameDeserializer::is_package_name(sv))
            {
                r.add_generic_error(type_name(),
                                    "must be lowercase alphanumeric+hyphens, split with periods, and not reserved");
            }

            Dependency dep;
            dep.name = sv.to_string();
            return dep;
        }

        virtual Optional<Dependency> visit_object(Json::Reader& r, const Json::Object& obj) override
        {
            Dependency dep;

            for (const auto& el : obj)
            {
                if (Strings::starts_with(el.first, "$"))
                {
                    dep.extra_info.insert_or_replace(el.first.to_string(), el.second);
                }
            }

            static Json::ArrayDeserializer<Json::IdentifierDeserializer> arr_id_d{"an array of identifiers"};

            r.required_object_field(type_name(), obj, NAME, dep.name, Json::PackageNameDeserializer::instance);
            r.optional_object_field(obj, FEATURES, dep.features, arr_id_d);

            bool default_features = true;
            r.optional_object_field(obj, DEFAULT_FEATURES, default_features, Json::BooleanDeserializer::instance);
            if (!default_features)
            {
                dep.features.push_back("core");
            }
            r.optional_object_field(obj, HOST, dep.host, Json::BooleanDeserializer::instance);

            r.optional_object_field(obj, PLATFORM, dep.platform, PlatformExprDeserializer::instance);

            static Json::StringDeserializer version_deserializer("a version");

            auto has_ge_constraint =
                r.optional_object_field(obj, VERSION_GE, dep.constraint.value, version_deserializer);

            if (has_ge_constraint)
            {
                dep.constraint.type = Versions::Constraint::Type::Minimum;
                auto h = dep.constraint.value.find('#');
                if (h != std::string::npos)
                {
                    auto opt = Strings::strto<int>(dep.constraint.value.c_str() + h + 1);
                    auto v = opt.get();
                    if (v && *v > 0)
                    {
                        dep.constraint.port_version = *v;
                    }
                    else
                    {
                        r.add_generic_error(type_name(),
                                            "embedded port-version ('#') in the primary "
                                            "constraint (\"",
                                            VERSION_GE,
                                            "\") must be a positive integer");
                    }
                    dep.constraint.value.erase(h);
                }
            }

            return dep;
        }

        static DependencyDeserializer instance;
    };
    DependencyDeserializer DependencyDeserializer::instance;

    struct DependencyArrayDeserializer final : Json::IDeserializer<std::vector<Dependency>>
    {
        virtual StringView type_name() const override { return "an array of dependencies"; }

        virtual Optional<std::vector<Dependency>> visit_array(Json::Reader& r, const Json::Array& arr) override
        {
            return r.array_elements(arr, DependencyDeserializer::instance);
        }

        static DependencyArrayDeserializer instance;
    };
    DependencyArrayDeserializer DependencyArrayDeserializer::instance;

    constexpr StringLiteral DependencyDeserializer::NAME;
    constexpr StringLiteral DependencyDeserializer::FEATURES;
    constexpr StringLiteral DependencyDeserializer::DEFAULT_FEATURES;
    constexpr StringLiteral DependencyDeserializer::PLATFORM;
    constexpr StringLiteral DependencyDeserializer::VERSION_GE;

    struct DependencyOverrideDeserializer : Json::IDeserializer<DependencyOverride>
    {
        virtual StringView type_name() const override { return "an override"; }

        constexpr static StringLiteral NAME = "name";

        virtual Span<const StringView> valid_fields() const override
        {
            static const StringView u[] = {NAME};
            static const auto t = Util::Vectors::concat<StringView>(schemed_deserializer_fields(), u);
            return t;
        }

        static void visit_impl(StringView type_name,
                               Json::Reader& r,
                               const Json::Object& obj,
                               std::string& name,
                               std::string& version,
                               Versions::Scheme& version_scheme,
                               int& port_version)
        {
            r.required_object_field(type_name, obj, NAME, name, Json::IdentifierDeserializer::instance);

            auto schemed_version = visit_required_schemed_deserializer(type_name, r, obj);
            version = schemed_version.versiont.text();
            version_scheme = schemed_version.scheme;
            port_version = schemed_version.versiont.port_version();
        }

        virtual Optional<DependencyOverride> visit_object(Json::Reader& r, const Json::Object& obj) override
        {
            DependencyOverride dep;

            for (const auto& el : obj)
            {
                if (Strings::starts_with(el.first, "$"))
                {
                    dep.extra_info.insert_or_replace(el.first.to_string(), el.second);
                }
            }

            visit_impl(type_name(), r, obj, dep.name, dep.version, dep.version_scheme, dep.port_version);

            return dep;
        }

        static DependencyOverrideDeserializer instance;
    };
    DependencyOverrideDeserializer DependencyOverrideDeserializer::instance;

    constexpr StringLiteral DependencyOverrideDeserializer::NAME;

    // reasoning for these two distinct types -- FeatureDeserializer and ArrayFeatureDeserializer:
    // `"features"` may be defined in one of two ways:
    // - An array of feature objects, which contains the `"name"` field
    // - An object mapping feature names to feature objects, which do not contain the `"name"` field
    // `ArrayFeatureDeserializer` is used for the former, `FeatureDeserializer` is used for the latter.
    struct FeatureDeserializer : Json::IDeserializer<std::unique_ptr<FeatureParagraph>>
    {
        virtual StringView type_name() const override { return "a feature"; }

        constexpr static StringLiteral NAME = "name";
        constexpr static StringLiteral DESCRIPTION = "description";
        constexpr static StringLiteral DEPENDENCIES = "dependencies";

        virtual Span<const StringView> valid_fields() const override
        {
            static const StringView t[] = {DESCRIPTION, DEPENDENCIES};
            return t;
        }

        virtual Optional<std::unique_ptr<FeatureParagraph>> visit_object(Json::Reader& r,
                                                                         const Json::Object& obj) override
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
                type_name(), obj, DESCRIPTION, feature->description, Json::ParagraphDeserializer::instance);
            r.optional_object_field(obj, DEPENDENCIES, feature->dependencies, DependencyArrayDeserializer::instance);

            return std::move(feature);
        }
        static FeatureDeserializer instance;
    };
    FeatureDeserializer FeatureDeserializer::instance;
    constexpr StringLiteral FeatureDeserializer::NAME;
    constexpr StringLiteral FeatureDeserializer::DESCRIPTION;
    constexpr StringLiteral FeatureDeserializer::DEPENDENCIES;

    struct ArrayFeatureDeserializer : Json::IDeserializer<std::unique_ptr<FeatureParagraph>>
    {
        virtual StringView type_name() const override { return "a feature"; }

        virtual Span<const StringView> valid_fields() const override
        {
            static const StringView t[] = {
                FeatureDeserializer::NAME,
                FeatureDeserializer::DESCRIPTION,
                FeatureDeserializer::DEPENDENCIES,
            };
            return t;
        }

        virtual Optional<std::unique_ptr<FeatureParagraph>> visit_object(Json::Reader& r,
                                                                         const Json::Object& obj) override
        {
            std::string name;
            r.required_object_field(
                type_name(), obj, FeatureDeserializer::NAME, name, Json::IdentifierDeserializer::instance);
            auto opt = FeatureDeserializer::instance.visit_object(r, obj);
            if (auto p = opt.get())
            {
                p->get()->name = std::move(name);
            }
            return opt;
        }

        static Json::ArrayDeserializer<ArrayFeatureDeserializer> array_instance;
    };
    Json::ArrayDeserializer<ArrayFeatureDeserializer> ArrayFeatureDeserializer::array_instance{
        "an array of feature objects"};

    struct FeaturesFieldDeserializer : Json::IDeserializer<std::vector<std::unique_ptr<FeatureParagraph>>>
    {
        virtual StringView type_name() const override { return "a set of features"; }

        virtual Span<const StringView> valid_fields() const override { return {}; }

        virtual Optional<std::vector<std::unique_ptr<FeatureParagraph>>> visit_array(Json::Reader& r,
                                                                                     const Json::Array& arr) override
        {
            return ArrayFeatureDeserializer::array_instance.visit_array(r, arr);
        }

        virtual Optional<std::vector<std::unique_ptr<FeatureParagraph>>> visit_object(Json::Reader& r,
                                                                                      const Json::Object& obj) override
        {
            std::vector<std::unique_ptr<FeatureParagraph>> res;
            std::vector<std::string> extra_fields;

            for (const auto& pr : obj)
            {
                if (!Json::IdentifierDeserializer::is_ident(pr.first))
                {
                    r.add_generic_error(type_name(),
                                        "unexpected field '",
                                        pr.first,
                                        "': must be lowercase alphanumeric+hyphens and not reserved");
                    continue;
                }
                std::unique_ptr<FeatureParagraph> v;
                r.visit_in_key(pr.second, pr.first, v, FeatureDeserializer::instance);
                if (v)
                {
                    v->name = pr.first.to_string();
                    res.push_back(std::move(v));
                }
            }

            return std::move(res);
        }

        static FeaturesFieldDeserializer instance;
    };
    FeaturesFieldDeserializer FeaturesFieldDeserializer::instance;

    static constexpr StringView EXPRESSION_WORDS[] = {
        "WITH",
        "AND",
        "OR",
    };
    static constexpr StringView VALID_LICENSES[] =
#include "spdx-licenses.inc"
        ;
    static constexpr StringView VALID_EXCEPTIONS[] =
#include "spdx-licenses.inc"
        ;

    // We "parse" this so that we can add actual license parsing at some point in the future
    // without breaking anyone
    struct LicenseExpressionDeserializer : Json::IDeserializer<std::string>
    {
        virtual StringView type_name() const override { return "an SPDX license expression"; }

        enum class Mode
        {
            ExpectExpression,
            ExpectContinue,
            ExpectException,
        };

        virtual Optional<std::string> visit_string(Json::Reader&, StringView sv) override
        {
            Mode mode = Mode::ExpectExpression;
            size_t open_parens = 0;
            std::string current_word;

            const auto check_current_word = [&current_word, &mode] {
                if (current_word.empty())
                {
                    return true;
                }

                Span<const StringView> valid_ids;
                bool case_sensitive = false;
                switch (mode)
                {
                    case Mode::ExpectExpression:
                        valid_ids = VALID_LICENSES;
                        mode = Mode::ExpectContinue;
                        // a single + is allowed on the end of licenses
                        if (current_word.back() == '+')
                        {
                            current_word.pop_back();
                        }
                        break;
                    case Mode::ExpectContinue:
                        valid_ids = EXPRESSION_WORDS;
                        mode = Mode::ExpectExpression;
                        case_sensitive = true;
                        break;
                    case Mode::ExpectException:
                        valid_ids = VALID_EXCEPTIONS;
                        mode = Mode::ExpectContinue;
                        break;
                }

                const auto equal = [&](StringView sv) {
                    if (case_sensitive)
                    {
                        return sv == current_word;
                    }
                    else
                    {
                        return Strings::case_insensitive_ascii_equals(sv, current_word);
                    }
                };

                if (std::find_if(valid_ids.begin(), valid_ids.end(), equal) == valid_ids.end())
                {
                    return false;
                }

                if (current_word == "WITH")
                {
                    mode = Mode::ExpectException;
                }

                current_word.clear();
                return true;
            };

            for (const auto& ch : sv)
            {
                if (ch == ' ' || ch == '\t')
                {
                    if (!check_current_word())
                    {
                        return nullopt;
                    }
                }
                else if (ch == '(')
                {
                    if (!check_current_word())
                    {
                        return nullopt;
                    }
                    if (mode != Mode::ExpectExpression)
                    {
                        return nullopt;
                    }
                    ++open_parens;
                }
                else if (ch == ')')
                {
                    if (!check_current_word())
                    {
                        return nullopt;
                    }
                    if (mode != Mode::ExpectContinue)
                    {
                        return nullopt;
                    }
                    if (open_parens == 0)
                    {
                        return nullopt;
                    }
                    --open_parens;
                }
                else
                {
                    current_word.push_back(ch);
                }
            }

            if (!check_current_word())
            {
                return nullopt;
            }
            else
            {
                return sv.to_string();
            }
        }

        static LicenseExpressionDeserializer instance;
    };
    LicenseExpressionDeserializer LicenseExpressionDeserializer::instance;

    struct BaselineCommitDeserializer final : Json::IDeserializer<std::string>
    {
        virtual StringView type_name() const override { return "a vcpkg repository commit"; }

        virtual Optional<std::string> visit_string(Json::Reader&, StringView s) override
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
        virtual StringView type_name() const override { return "a manifest"; }

        constexpr static StringLiteral NAME = "name";
        constexpr static StringLiteral MAINTAINERS = "maintainers";
        constexpr static StringLiteral DESCRIPTION = "description";
        constexpr static StringLiteral HOMEPAGE = "homepage";
        constexpr static StringLiteral DOCUMENTATION = "documentation";
        constexpr static StringLiteral LICENSE = "license";
        constexpr static StringLiteral DEPENDENCIES = "dependencies";
        constexpr static StringLiteral DEV_DEPENDENCIES = "dev-dependencies";
        constexpr static StringLiteral FEATURES = "features";
        constexpr static StringLiteral DEFAULT_FEATURES = "default-features";
        constexpr static StringLiteral SUPPORTS = "supports";
        constexpr static StringLiteral OVERRIDES = "overrides";
        constexpr static StringLiteral BUILTIN_BASELINE = "builtin-baseline";

        virtual Span<const StringView> valid_fields() const override
        {
            static const StringView u[] = {
                NAME,
                MAINTAINERS,
                DESCRIPTION,
                HOMEPAGE,
                DOCUMENTATION,
                LICENSE,
                DEPENDENCIES,
                DEV_DEPENDENCIES,
                FEATURES,
                DEFAULT_FEATURES,
                SUPPORTS,
                OVERRIDES,
                BUILTIN_BASELINE,
            };
            static const auto t = Util::Vectors::concat<StringView>(schemed_deserializer_fields(), u);

            return t;
        }

        virtual Optional<std::unique_ptr<SourceControlFile>> visit_object(Json::Reader& r,
                                                                          const Json::Object& obj) override
        {
            auto control_file = std::make_unique<SourceControlFile>();
            control_file->core_paragraph = std::make_unique<SourceParagraph>();

            auto& spgh = control_file->core_paragraph;

            for (const auto& el : obj)
            {
                if (Strings::starts_with(el.first, "$"))
                {
                    spgh->extra_info.insert_or_replace(el.first.to_string(), el.second);
                }
            }

            static Json::StringDeserializer url_deserializer{"a url"};

            constexpr static StringView inner_type_name = "vcpkg.json";
            DependencyOverrideDeserializer::visit_impl(
                inner_type_name, r, obj, spgh->name, spgh->version, spgh->version_scheme, spgh->port_version);

            r.optional_object_field(obj, MAINTAINERS, spgh->maintainers, Json::ParagraphDeserializer::instance);
            r.optional_object_field(obj, DESCRIPTION, spgh->description, Json::ParagraphDeserializer::instance);
            r.optional_object_field(obj, HOMEPAGE, spgh->homepage, url_deserializer);
            r.optional_object_field(obj, DOCUMENTATION, spgh->documentation, url_deserializer);
            r.optional_object_field(obj, LICENSE, spgh->license, LicenseExpressionDeserializer::instance);
            r.optional_object_field(obj, DEPENDENCIES, spgh->dependencies, DependencyArrayDeserializer::instance);
            static Json::ArrayDeserializer<DependencyOverrideDeserializer> overrides_deserializer{
                "an array of overrides"};
            r.optional_object_field(obj, OVERRIDES, spgh->overrides, overrides_deserializer);

            if (obj.contains(DEV_DEPENDENCIES))
            {
                r.add_generic_error(type_name(), DEV_DEPENDENCIES, " are not yet supported");
            }
            std::string baseline;
            if (r.optional_object_field(obj, BUILTIN_BASELINE, baseline, BaselineCommitDeserializer::instance))
            {
                spgh->builtin_baseline = std::move(baseline);
            }

            r.optional_object_field(obj, SUPPORTS, spgh->supports_expression, PlatformExprDeserializer::instance);

            r.optional_object_field(
                obj, DEFAULT_FEATURES, spgh->default_features, Json::IdentifierArrayDeserializer::instance);

            r.optional_object_field(
                obj, FEATURES, control_file->feature_paragraphs, FeaturesFieldDeserializer::instance);

            if (auto maybe_error = canonicalize(*control_file))
            {
                Checks::exit_with_message(VCPKG_LINE_INFO, maybe_error->error);
            }
            return std::move(control_file);
        }

        static ManifestDeserializer instance;
    };
    ManifestDeserializer ManifestDeserializer::instance;

    constexpr StringLiteral ManifestDeserializer::NAME;
    constexpr StringLiteral ManifestDeserializer::MAINTAINERS;
    constexpr StringLiteral ManifestDeserializer::DESCRIPTION;
    constexpr StringLiteral ManifestDeserializer::HOMEPAGE;
    constexpr StringLiteral ManifestDeserializer::DOCUMENTATION;
    constexpr StringLiteral ManifestDeserializer::LICENSE;
    constexpr StringLiteral ManifestDeserializer::DEPENDENCIES;
    constexpr StringLiteral ManifestDeserializer::DEV_DEPENDENCIES;
    constexpr StringLiteral ManifestDeserializer::FEATURES;
    constexpr StringLiteral ManifestDeserializer::DEFAULT_FEATURES;
    constexpr StringLiteral ManifestDeserializer::SUPPORTS;
    constexpr StringLiteral ManifestDeserializer::OVERRIDES;

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

    Parse::ParseExpected<SourceControlFile> SourceControlFile::parse_manifest_object(const std::string& origin,
                                                                                     const Json::Object& manifest)
    {
        Json::Reader reader;

        auto res = reader.visit(manifest, ManifestDeserializer::instance);

        if (!reader.errors().empty())
        {
            auto err = std::make_unique<ParseControlErrorInfo>();
            err->name = origin;
            err->other_errors = std::move(reader.errors());
            return std::move(err);
        }
        else if (auto p = res.get())
        {
            return std::move(*p);
        }
        else
        {
            Checks::unreachable(VCPKG_LINE_INFO);
        }
    }

    Optional<std::string> SourceControlFile::check_against_feature_flags(const path& origin,
                                                                         const FeatureFlagSettings& flags,
                                                                         bool is_default_builtin_registry) const
    {
        static constexpr StringLiteral s_extended_help = "See `vcpkg help versioning` for more information.";
        if (!flags.versions)
        {
            auto check_deps = [&](View<Dependency> deps) -> Optional<std::string> {
                for (auto&& dep : deps)
                {
                    if (dep.constraint.type != Versions::Constraint::Type::None)
                    {
                        Metrics::g_metrics.lock()->track_property("error-versioning-disabled", "defined");
                        return Strings::concat(
                            vcpkg::u8string(origin),
                            " was rejected because it uses constraints and the `",
                            VcpkgCmdArguments::VERSIONS_FEATURE,
                            "` feature flag is disabled.\nThis can be fixed by removing uses of \"version>=\".\n",
                            s_extended_help);
                    }
                }
                return nullopt;
            };

            if (auto r = check_deps(core_paragraph->dependencies)) return r;

            for (auto&& fpgh : feature_paragraphs)
            {
                if (auto r = check_deps(fpgh->dependencies)) return r;
            }

            if (core_paragraph->overrides.size() != 0)
            {
                Metrics::g_metrics.lock()->track_property("error-versioning-disabled", "defined");
                return Strings::concat(vcpkg::u8string(origin),
                                       " was rejected because it uses overrides and the `",
                                       VcpkgCmdArguments::VERSIONS_FEATURE,
                                       "` feature flag is disabled.\nThis can be fixed by removing \"overrides\".\n",
                                       s_extended_help);
            }

            if (core_paragraph->builtin_baseline.has_value())
            {
                Metrics::g_metrics.lock()->track_property("error-versioning-disabled", "defined");
                return Strings::concat(
                    vcpkg::u8string(origin),
                    " was rejected because it uses builtin-baseline and the `",
                    VcpkgCmdArguments::VERSIONS_FEATURE,
                    "` feature flag is disabled.\nThis can be fixed by removing \"builtin-baseline\".\n",
                    s_extended_help);
            }
        }
        else
        {
            if (!core_paragraph->builtin_baseline.has_value() && is_default_builtin_registry)
            {
                if (std::any_of(core_paragraph->dependencies.begin(),
                                core_paragraph->dependencies.end(),
                                [](const auto& dependency) {
                                    return dependency.constraint.type != Versions::Constraint::Type::None;
                                }))
                {
                    Metrics::g_metrics.lock()->track_property("error-versioning-no-baseline", "defined");
                    return Strings::concat(
                        vcpkg::u8string(origin),
                        " was rejected because it uses \"version>=\" without setting a \"builtin-baseline\".\n",
                        s_extended_help);
                }

                if (!core_paragraph->overrides.empty())
                {
                    Metrics::g_metrics.lock()->track_property("error-versioning-no-baseline", "defined");
                    return Strings::concat(
                        vcpkg::u8string(origin),
                        " was rejected because it uses \"overrides\" without setting a \"builtin-baseline\".\n",
                        s_extended_help);
                }
            }
        }
        return nullopt;
    }

    Parse::ParseExpected<SourceControlFile> SourceControlFile::parse_manifest_file(const path& manifest_path,
                                                                                   const Json::Object& manifest)
    {
        return parse_manifest_object(vcpkg::u8string(manifest_path), manifest);
    }

    void print_error_message(Span<const std::unique_ptr<Parse::ParseControlErrorInfo>> error_info_list)
    {
        Checks::check_exit(VCPKG_LINE_INFO, error_info_list.size() > 0);

        for (auto&& error_info : error_info_list)
        {
            Checks::check_exit(VCPKG_LINE_INFO, error_info != nullptr);
            if (!error_info->error.empty())
            {
                print2(Color::error, "Error: while loading ", error_info->name, ":\n", error_info->error, '\n');
            }

            if (!error_info->other_errors.empty())
            {
                print2(Color::error, "Errors occurred while parsing ", error_info->name, "\n");
                for (auto&& msg : error_info->other_errors)
                    print2("    ", msg, '\n');
            }
        }

        bool have_remaining_fields = false;
        for (auto&& error_info : error_info_list)
        {
            if (!error_info->extra_fields.empty())
            {
                print2(Color::error,
                       "Error: There are invalid fields in the control or manifest file of ",
                       error_info->name,
                       '\n');
                print2("The following fields were not expected:\n");

                for (const auto& pr : error_info->extra_fields)
                {
                    print2("    In ", pr.first, ": ", Strings::join(", ", pr.second), "\n");
                }
                have_remaining_fields = true;
            }
        }

        if (have_remaining_fields)
        {
            print2("This is the list of valid fields for CONTROL files (case-sensitive): \n\n    ",
                   Strings::join("\n    ", get_list_of_valid_fields()),
                   "\n\n");
#if defined(_WIN32)
            auto bootstrap = ".\\bootstrap-vcpkg.bat";
#else
            auto bootstrap = "./bootstrap-vcpkg.sh";
#endif
            vcpkg::printf("You may need to update the vcpkg binary; try running %s to update.\n\n", bootstrap);
        }

        for (auto&& error_info : error_info_list)
        {
            if (!error_info->missing_fields.empty())
            {
                print2(Color::error, "Error: There are missing fields in the control file of ", error_info->name, '\n');
                print2("The following fields were missing:\n");
                for (const auto& pr : error_info->missing_fields)
                {
                    print2("    In ", pr.first, ": ", Strings::join(", ", pr.second), "\n");
                }
            }
        }

        for (auto&& error_info : error_info_list)
        {
            if (!error_info->expected_types.empty())
            {
                print2(Color::error,
                       "Error: There are invalid field types in the CONTROL or manifest file of ",
                       error_info->name,
                       '\n');
                print2("The following fields had the wrong types:\n\n");

                for (const auto& pr : error_info->expected_types)
                {
                    vcpkg::printf("    %s was expected to be %s\n", pr.first, pr.second);
                }
                print2("\n");
            }
        }
    }

    Optional<const FeatureParagraph&> SourceControlFile::find_feature(const std::string& featurename) const
    {
        auto it = Util::find_if(feature_paragraphs,
                                [&](const std::unique_ptr<FeatureParagraph>& p) { return p->name == featurename; });
        if (it != feature_paragraphs.end())
            return **it;
        else
            return nullopt;
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
                Triplet t = dep.host ? host : target;
                ret.emplace_back(FullPackageSpec({dep.name, t}, dep.features));
            }
        }
        return ret;
    }

    static bool is_dependency_trivial(const Dependency& dep)
    {
        return dep.features.empty() && dep.platform.is_empty() && dep.extra_info.is_empty() &&
               dep.constraint.type == Versions::Constraint::Type::None && !dep.host;
    }

    static Json::Object serialize_manifest_impl(const SourceControlFile& scf, bool debug)
    {
        auto serialize_paragraph =
            [&](Json::Object& obj, StringLiteral name, const std::vector<std::string>& pgh, bool always = false) {
                if (!debug)
                {
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
                        obj.insert(name, Json::Value::string(pgh.front()));
                        return;
                    }
                }

                auto& arr = obj.insert(name, Json::Array());
                for (const auto& s : pgh)
                {
                    arr.push_back(Json::Value::string(s));
                }
            };
        auto serialize_optional_array =
            [&](Json::Object& obj, StringLiteral name, const std::vector<std::string>& pgh) {
                if (pgh.empty() && !debug) return;

                auto& arr = obj.insert(name, Json::Array());
                for (const auto& s : pgh)
                {
                    arr.push_back(Json::Value::string(s));
                }
            };
        auto serialize_optional_string = [&](Json::Object& obj, StringLiteral name, const std::string& s) {
            if (!s.empty() || debug)
            {
                obj.insert(name, Json::Value::string(s));
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

                dep_obj.insert(DependencyDeserializer::NAME, Json::Value::string(dep.name));
                if (dep.host) dep_obj.insert(DependencyDeserializer::HOST, Json::Value::boolean(true));

                auto features_copy = dep.features;
                auto core_it = std::find(features_copy.begin(), features_copy.end(), "core");
                if (core_it != features_copy.end())
                {
                    dep_obj.insert(DependencyDeserializer::DEFAULT_FEATURES, Json::Value::boolean(false));
                    features_copy.erase(core_it);
                }

                serialize_optional_array(dep_obj, DependencyDeserializer::FEATURES, features_copy);
                serialize_optional_string(dep_obj, DependencyDeserializer::PLATFORM, to_string(dep.platform));
                if (dep.constraint.type == Versions::Constraint::Type::Minimum)
                {
                    auto s = dep.constraint.value;
                    if (dep.constraint.port_version != 0)
                    {
                        Strings::append(s, '#', dep.constraint.port_version);
                    }
                    dep_obj.insert(DependencyDeserializer::VERSION_GE, Json::Value::string(std::move(s)));
                }
            }
        };

        auto serialize_override = [&](Json::Array& arr, const DependencyOverride& dep) {
            auto& dep_obj = arr.push_back(Json::Object());
            for (const auto& el : dep.extra_info)
            {
                dep_obj.insert(el.first.to_string(), el.second);
            }

            dep_obj.insert(DependencyOverrideDeserializer::NAME, Json::Value::string(dep.name));

            serialize_schemed_version(dep_obj, dep.version_scheme, dep.version, dep.port_version);
        };

        Json::Object obj;

        for (const auto& el : scf.core_paragraph->extra_info)
        {
            obj.insert(el.first.to_string(), el.second);
        }

        obj.insert(ManifestDeserializer::NAME, Json::Value::string(scf.core_paragraph->name));

        serialize_schemed_version(obj,
                                  scf.core_paragraph->version_scheme,
                                  scf.core_paragraph->version,
                                  scf.core_paragraph->port_version,
                                  debug);

        serialize_paragraph(obj, ManifestDeserializer::MAINTAINERS, scf.core_paragraph->maintainers);
        serialize_paragraph(obj, ManifestDeserializer::DESCRIPTION, scf.core_paragraph->description);

        serialize_optional_string(obj, ManifestDeserializer::HOMEPAGE, scf.core_paragraph->homepage);
        serialize_optional_string(obj, ManifestDeserializer::DOCUMENTATION, scf.core_paragraph->documentation);
        serialize_optional_string(obj, ManifestDeserializer::LICENSE, scf.core_paragraph->license);
        serialize_optional_string(
            obj, ManifestDeserializer::SUPPORTS, to_string(scf.core_paragraph->supports_expression));
        if (scf.core_paragraph->builtin_baseline.has_value())
        {
            obj.insert(ManifestDeserializer::BUILTIN_BASELINE,
                       Json::Value::string(scf.core_paragraph->builtin_baseline.value_or_exit(VCPKG_LINE_INFO)));
        }

        if (!scf.core_paragraph->dependencies.empty() || debug)
        {
            auto& deps = obj.insert(ManifestDeserializer::DEPENDENCIES, Json::Array());

            for (const auto& dep : scf.core_paragraph->dependencies)
            {
                serialize_dependency(deps, dep);
            }
        }

        serialize_optional_array(obj, ManifestDeserializer::DEFAULT_FEATURES, scf.core_paragraph->default_features);

        if (!scf.feature_paragraphs.empty() || debug)
        {
            auto& map = obj.insert(ManifestDeserializer::FEATURES, Json::Object());
            for (const auto& feature : scf.feature_paragraphs)
            {
                auto& feature_obj = map.insert(feature->name, Json::Object());
                for (const auto& el : feature->extra_info)
                {
                    feature_obj.insert(el.first.to_string(), el.second);
                }

                serialize_paragraph(feature_obj, FeatureDeserializer::DESCRIPTION, feature->description, true);

                if (!feature->dependencies.empty() || debug)
                {
                    auto& deps = feature_obj.insert(FeatureDeserializer::DEPENDENCIES, Json::Array());
                    for (const auto& dep : feature->dependencies)
                    {
                        serialize_dependency(deps, dep);
                    }
                }
            }
        }

        if (!scf.core_paragraph->overrides.empty() || debug)
        {
            auto& overrides = obj.insert(ManifestDeserializer::OVERRIDES, Json::Array());

            for (const auto& over : scf.core_paragraph->overrides)
            {
                serialize_override(overrides, over);
            }
        }

        return obj;
    }

    Json::Object serialize_debug_manifest(const SourceControlFile& scf) { return serialize_manifest_impl(scf, true); }

    Json::Object serialize_manifest(const SourceControlFile& scf) { return serialize_manifest_impl(scf, false); }
}
