#include <vcpkg/base/util.h>

#include <vcpkg/versiondeserializers.h>

using namespace vcpkg;

namespace
{
    constexpr StringLiteral BASELINE = "baseline";
    constexpr StringLiteral VERSION_RELAXED = "version";
    constexpr StringLiteral VERSION_SEMVER = "version-semver";
    constexpr StringLiteral VERSION_STRING = "version-string";
    constexpr StringLiteral VERSION_DATE = "version-date";
    constexpr StringLiteral PORT_VERSION = "port-version";

    struct VersionDeserializer final : Json::IDeserializer<std::pair<std::string, Optional<int>>>
    {
        VersionDeserializer(LocalizedString type, bool allow_hash_portversion)
            : m_type(type), m_allow_hash_portversion(allow_hash_portversion)
        {
        }

        LocalizedString type_name() const { return m_type; }

        Optional<std::pair<std::string, Optional<int>>> visit_string(Json::Reader& r, StringView sv) const override
        {
            auto it = std::find(sv.begin(), sv.end(), '#');
            StringView pv(it, sv.end());
            std::pair<std::string, Optional<int>> ret{std::string(sv.begin(), it), nullopt};
            if (m_allow_hash_portversion)
            {
                if (pv.size() == 1)
                {
                    r.add_generic_error(type_name(), "'#' in version text must be followed by a port version");
                }
                else if (pv.size() > 1)
                {
                    ret.second = Strings::strto<int>(pv.substr(1));
                    if (ret.second.value_or(-1) < 0)
                    {
                        r.add_generic_error(type_name(),
                                            "port versions after '#' in version text must be non-negative integers");
                    }
                }
            }
            else
            {
                if (pv.size() == 1)
                {
                    r.add_generic_error(type_name(), "invalid character '#' in version text");
                }
                else if (pv.size() > 1)
                {
                    r.add_generic_error(type_name(),
                                        "invalid character '#' in version text. Did you mean \"port-version\": ",
                                        pv.substr(1),
                                        "?");
                }
            }
            return ret;
        }

        LocalizedString m_type;
        bool m_allow_hash_portversion;
    };

    struct GenericVersionDeserializer : Json::IDeserializer<Version>
    {
        GenericVersionDeserializer(StringLiteral version_field) : m_version_field(version_field) { }
        LocalizedString type_name() const override { return msg::format(msgAVersionObject); }

        Optional<Version> visit_object(Json::Reader& r, const Json::Object& obj) const override
        {
            std::pair<std::string, Optional<int>> version;
            int port_version = 0;

            static const VersionDeserializer version_deserializer{msg::format(msgAVersionOfAnyType), false};

            r.required_object_field(type_name(), obj, m_version_field, version, version_deserializer);
            r.optional_object_field(obj, PORT_VERSION, port_version, Json::NaturalNumberDeserializer::instance);

            return Version{std::move(version.first), port_version};
        }
        StringLiteral m_version_field;
    };
}

namespace vcpkg
{
    Optional<SchemedVersion> visit_optional_schemed_deserializer(const LocalizedString& parent_type,
                                                                 Json::Reader& r,
                                                                 const Json::Object& obj,
                                                                 bool allow_hash_portversion)
    {
        VersionScheme version_scheme = VersionScheme::String;
        std::pair<std::string, Optional<int>> version;

        VersionDeserializer version_exact_deserializer{msg::format(msgAnExactVersionString), allow_hash_portversion};
        VersionDeserializer version_relaxed_deserializer{msg::format(msgARelaxedVersionString), allow_hash_portversion};
        VersionDeserializer version_semver_deserializer{msg::format(msgASemanticVersionString), allow_hash_portversion};
        VersionDeserializer version_date_deserializer{msg::format(msgADateVersionString), allow_hash_portversion};

        bool has_exact = r.optional_object_field(obj, VERSION_STRING, version, version_exact_deserializer);
        bool has_relax = r.optional_object_field(obj, VERSION_RELAXED, version, version_relaxed_deserializer);
        bool has_semver = r.optional_object_field(obj, VERSION_SEMVER, version, version_semver_deserializer);
        bool has_date = r.optional_object_field(obj, VERSION_DATE, version, version_date_deserializer);
        int num_versions = (int)has_exact + (int)has_relax + (int)has_semver + (int)has_date;
        int port_version = version.second.value_or(0);
        bool has_port_version =
            r.optional_object_field(obj, PORT_VERSION, port_version, Json::NaturalNumberDeserializer::instance);

        if (has_port_version && version.second)
        {
            r.add_generic_error(parent_type, "\"port_version\" cannot be combined with an embedded '#' in the version");
        }

        if (num_versions == 0)
        {
            if (!has_port_version)
            {
                return nullopt;
            }
            else
            {
                r.add_generic_error(parent_type, "unexpected \"port_version\" without a versioning field");
            }
        }
        else if (num_versions > 1)
        {
            r.add_generic_error(parent_type, "expected only one versioning field");
        }
        else
        {
            if (has_exact)
            {
                version_scheme = VersionScheme::String;
            }
            else if (has_relax)
            {
                version_scheme = VersionScheme::Relaxed;
                auto v = DotVersion::try_parse_relaxed(version.first);
                if (!v)
                {
                    r.add_generic_error(parent_type, "'version' text was not a relaxed version:\n", v.error());
                }
            }
            else if (has_semver)
            {
                version_scheme = VersionScheme::Semver;
                auto v = DotVersion::try_parse_semver(version.first);
                if (!v)
                {
                    r.add_generic_error(parent_type, "'version-semver' text was not a semantic version:\n", v.error());
                }
            }
            else if (has_date)
            {
                version_scheme = VersionScheme::Date;
                auto v = DateVersion::try_parse(version.first);
                if (!v)
                {
                    r.add_generic_error(parent_type, "'version-date' text was not a date version:\n", v.error());
                }
            }
            else
            {
                Checks::unreachable(VCPKG_LINE_INFO);
            }
        }

        return SchemedVersion{version_scheme, Version{std::move(version.first), port_version}};
    }

    SchemedVersion visit_required_schemed_deserializer(const LocalizedString& parent_type,
                                                       Json::Reader& r,
                                                       const Json::Object& obj,
                                                       bool allow_hash_portversion)
    {
        auto maybe_schemed_version = visit_optional_schemed_deserializer(parent_type, r, obj, allow_hash_portversion);
        if (auto p = maybe_schemed_version.get())
        {
            return std::move(*p);
        }
        else
        {
            r.add_generic_error(parent_type, "expected a versioning field (example: ", VERSION_STRING, ")");
            return {};
        }
    }

    View<StringView> schemed_deserializer_fields()
    {
        static constexpr StringView t[] = {VERSION_RELAXED, VERSION_SEMVER, VERSION_STRING, VERSION_DATE, PORT_VERSION};
        return t;
    }

    void serialize_schemed_version(Json::Object& out_obj, VersionScheme scheme, StringView version, int port_version)
    {
        auto version_field = [](VersionScheme version_scheme) {
            switch (version_scheme)
            {
                case VersionScheme::String: return VERSION_STRING;
                case VersionScheme::Semver: return VERSION_SEMVER;
                case VersionScheme::Relaxed: return VERSION_RELAXED;
                case VersionScheme::Date: return VERSION_DATE;
                default: Checks::unreachable(VCPKG_LINE_INFO);
            }
        };

        out_obj.insert(version_field(scheme), Json::Value::string(version));

        if (port_version != 0)
        {
            out_obj.insert(PORT_VERSION, Json::Value::integer(port_version));
        }
    }

    LocalizedString VersionConstraintStringDeserializer::type_name() const
    {
        return msg::format(msgAVersionConstraint);
    }

    const VersionConstraintStringDeserializer VersionConstraintStringDeserializer::instance;

    const Json::IDeserializer<Version>& get_version_deserializer_instance()
    {
        static const GenericVersionDeserializer deserializer(VERSION_STRING);
        return deserializer;
    }

    const Json::IDeserializer<Version>& get_versiontag_deserializer_instance()
    {
        static const GenericVersionDeserializer deserializer(BASELINE);
        return deserializer;
    }
}
