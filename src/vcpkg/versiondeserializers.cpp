#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/strings.h>

#include <vcpkg/versiondeserializers.h>
#include <vcpkg/versions.h>

using namespace vcpkg;

namespace
{
    template<const msg::MessageT<>& type_name_msg>
    struct VersionStringDeserializer final : Json::IDeserializer<std::string>
    {
        LocalizedString type_name() const override { return msg::format(type_name_msg); }

        Optional<std::string> visit_string(Json::Reader& r, StringView sv) const override
        {
            Optional<std::string> result;
            auto it = std::find(sv.begin(), sv.end(), '#');
            StringView pv(it, sv.end());
            if (pv.size() == 1)
            {
                r.add_generic_error(type_name(), msg::format(msgInvalidSharpInVersion));
            }
            else if (pv.size() > 1)
            {
                r.add_generic_error(type_name(),
                                    msg::format(msgInvalidSharpInVersionDidYouMean, msg::value = pv.substr(1)));
            }
            else
            {
                result.emplace(sv.begin(), it);
            }

            return result;
        }
    };

    template<const msg::MessageT<>& type_name_msg>
    struct VersionOverrideVersionStringDeserializer final : Json::IDeserializer<std::pair<std::string, Optional<int>>>
    {
        LocalizedString type_name() const override { return msg::format(type_name_msg); }

        Optional<std::pair<std::string, Optional<int>>> visit_string(Json::Reader& r, StringView sv) const override
        {
            Optional<std::pair<std::string, Optional<int>>> result;
            auto it = std::find(sv.begin(), sv.end(), '#');
            StringView pv(it, sv.end());
            if (pv.size() == 1)
            {
                r.add_generic_error(type_name(), msg::format(msgVersionSharpMustBeFollowedByPortVersion));
                return result;
            }

            Optional<int> maybe_parsed_pv;
            if (pv.size() > 1)
            {
                auto parsed_pv = Strings::strto<int>(pv.substr(1)).value_or(-1);
                if (parsed_pv < 0)
                {
                    r.add_generic_error(type_name(),
                                        msg::format(msgVersionSharpMustBeFollowedByPortVersionNonNegativeInteger));
                    return result;
                }

                maybe_parsed_pv.emplace(parsed_pv);
            }

            result.emplace(std::piecewise_construct,
                           std::forward_as_tuple(sv.begin(), it),
                           std::forward_as_tuple(std::move(maybe_parsed_pv)));
            return result;
        }
    };

    struct BaselineVersionTagDeserializer : Json::IDeserializer<Version>
    {
        LocalizedString type_name() const override { return msg::format(msgAVersionObject); }

        Optional<Version> visit_object(Json::Reader& r, const Json::Object& obj) const override
        {
            Optional<Version> result;
            auto& target = result.emplace();

            static const VersionStringDeserializer<msgAVersionOfAnyType> version_deserializer{};

            r.required_object_field(type_name(), obj, JsonIdBaseline, target.text, version_deserializer);
            r.optional_object_field(
                obj, JsonIdPortVersion, target.port_version, Json::NaturalNumberDeserializer::instance);

            return result;
        }
    };
}

namespace vcpkg
{
    Optional<SchemedVersion> visit_optional_schemed_version(const LocalizedString& parent_type,
                                                            Json::Reader& r,
                                                            const Json::Object& obj)
    {
        Optional<SchemedVersion> result;
        std::string version_text;

        static const VersionStringDeserializer<msgAnExactVersionString> version_exact_deserializer{};
        static const VersionStringDeserializer<msgARelaxedVersionString> version_relaxed_deserializer{};
        static const VersionStringDeserializer<msgASemanticVersionString> version_semver_deserializer{};
        static const VersionStringDeserializer<msgADateVersionString> version_date_deserializer{};

        bool has_exact = r.optional_object_field(obj, JsonIdVersionString, version_text, version_exact_deserializer);
        bool has_relax = r.optional_object_field(obj, JsonIdVersion, version_text, version_relaxed_deserializer);
        bool has_semver = r.optional_object_field(obj, JsonIdVersionSemver, version_text, version_semver_deserializer);
        bool has_date = r.optional_object_field(obj, JsonIdVersionDate, version_text, version_date_deserializer);
        int num_versions = (int)has_exact + (int)has_relax + (int)has_semver + (int)has_date;
        int port_version = 0;
        bool has_port_version =
            r.optional_object_field(obj, JsonIdPortVersion, port_version, Json::NaturalNumberDeserializer::instance);

        if (num_versions == 0)
        {
            if (has_port_version)
            {
                r.add_generic_error(parent_type, msg::format(msgUnexpectedPortversion));
            }

            return result;
        }

        if (num_versions > 1)
        {
            r.add_generic_error(parent_type, msg::format(msgExpectedOneVersioningField));
            return result;
        }

        if (has_exact)
        {
            result.emplace(VersionScheme::String, std::move(version_text), port_version);
            return result;
        }

        if (has_relax)
        {
            auto maybe_parsed = DotVersion::try_parse_relaxed(version_text);
            if (maybe_parsed)
            {
                result.emplace(VersionScheme::Relaxed, std::move(version_text), port_version);
                return result;
            }

            r.add_generic_error(parent_type, std::move(maybe_parsed).error());
            return result;
        }

        if (has_semver)
        {
            auto maybe_parsed = DotVersion::try_parse_semver(version_text);
            if (maybe_parsed)
            {
                result.emplace(VersionScheme::Semver, std::move(version_text), port_version);
                return result;
            }

            r.add_generic_error(parent_type, std::move(maybe_parsed).error());
            return result;
        }

        if (has_date)
        {
            auto maybe_parsed = DateVersion::try_parse(version_text);
            if (maybe_parsed)
            {
                result.emplace(VersionScheme::Date, std::move(version_text), port_version);
                return result;
            }

            r.add_generic_error(parent_type, std::move(maybe_parsed).error());
            return result;
        }

        Checks::unreachable(VCPKG_LINE_INFO);
    }

    SchemedVersion visit_required_schemed_version(const LocalizedString& parent_type,
                                                  Json::Reader& r,
                                                  const Json::Object& obj)
    {
        auto maybe_schemed_version = visit_optional_schemed_version(parent_type, r, obj);
        if (auto p = maybe_schemed_version.get())
        {
            return std::move(*p);
        }
        else
        {
            r.add_generic_error(parent_type, msg::format(msgVersionMissing));
            return {};
        }
    }

    Version visit_version_override_version(const LocalizedString& parent_type, Json::Reader& r, const Json::Object& obj)
    {
        std::pair<std::string, Optional<int>> proto_version;

        static const VersionOverrideVersionStringDeserializer<msgAnExactVersionString> version_exact_deserializer{};
        static const VersionOverrideVersionStringDeserializer<msgARelaxedVersionString> version_relaxed_deserializer{};
        static const VersionOverrideVersionStringDeserializer<msgASemanticVersionString> version_semver_deserializer{};
        static const VersionOverrideVersionStringDeserializer<msgADateVersionString> version_date_deserializer{};

        bool has_exact = r.optional_object_field(obj, JsonIdVersionString, proto_version, version_exact_deserializer);
        bool has_relax = r.optional_object_field(obj, JsonIdVersion, proto_version, version_relaxed_deserializer);
        bool has_semver = r.optional_object_field(obj, JsonIdVersionSemver, proto_version, version_semver_deserializer);
        bool has_date = r.optional_object_field(obj, JsonIdVersionDate, proto_version, version_date_deserializer);
        int num_versions = (int)has_exact + (int)has_relax + (int)has_semver + (int)has_date;
        int port_version = proto_version.second.value_or(0);
        bool has_port_version =
            r.optional_object_field(obj, JsonIdPortVersion, port_version, Json::NaturalNumberDeserializer::instance);

        if (has_port_version && proto_version.second)
        {
            r.add_generic_error(parent_type, msg::format(msgPortVersionMultipleSpecification));
        }

        if (num_versions == 0)
        {
            if (has_port_version)
            {
                r.add_generic_error(parent_type, msg::format(msgUnexpectedPortversion));
                return Version();
            }

            r.add_generic_error(parent_type, msg::format(msgVersionMissing));
            return Version();
        }

        if (num_versions > 1)
        {
            r.add_generic_error(parent_type, msg::format(msgExpectedOneVersioningField));
            return Version();
        }

        if (has_semver)
        {
            auto maybe_parsed = DotVersion::try_parse_semver(proto_version.first);
            if (!maybe_parsed)
            {
                r.add_generic_error(parent_type, std::move(maybe_parsed).error());
                return Version();
            }
        }

        if (has_date)
        {
            auto maybe_parsed = DateVersion::try_parse(proto_version.first);
            if (!maybe_parsed)
            {
                r.add_generic_error(parent_type, std::move(maybe_parsed).error());
                return Version();
            }
        }

        // Note that we allow everything in "version" of overrides, not only relaxed versions.
        return Version(std::move(proto_version.first), port_version);
    }

    View<StringView> schemed_deserializer_fields()
    {
        static constexpr StringView t[] = {
            JsonIdVersion, JsonIdVersionSemver, JsonIdVersionString, JsonIdVersionDate, JsonIdPortVersion};
        return t;
    }

    void serialize_schemed_version(Json::Object& out_obj, VersionScheme scheme, const Version& version)
    {
        auto version_field = [](VersionScheme version_scheme) {
            switch (version_scheme)
            {
                case VersionScheme::String: return JsonIdVersionString;
                case VersionScheme::Semver: return JsonIdVersionSemver;
                case VersionScheme::Relaxed: return JsonIdVersion;
                case VersionScheme::Date: return JsonIdVersionDate;
                default: Checks::unreachable(VCPKG_LINE_INFO);
            }
        };

        out_obj.insert(version_field(scheme), Json::Value::string(version.text));

        if (version.port_version != 0)
        {
            out_obj.insert(JsonIdPortVersion, Json::Value::integer(version.port_version));
        }
    }

    LocalizedString VersionConstraintStringDeserializer::type_name() const
    {
        return msg::format(msgAVersionConstraint);
    }

    const VersionConstraintStringDeserializer VersionConstraintStringDeserializer::instance;

    static const BaselineVersionTagDeserializer baseline_version_tag_deserializer_instance;
    const Json::IDeserializer<Version>& baseline_version_tag_deserializer = baseline_version_tag_deserializer_instance;
}
