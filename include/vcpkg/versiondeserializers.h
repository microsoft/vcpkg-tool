#pragma once

#include <vcpkg/base/fwd/stringview.h>

#include <vcpkg/base/jsonreader.h>

#include <vcpkg/versions.h>

namespace vcpkg
{
    const Json::IDeserializer<Version>& get_version_deserializer_instance();
    const Json::IDeserializer<Version>& get_versiontag_deserializer_instance();

    Optional<SchemedVersion> visit_optional_schemed_deserializer(const LocalizedString& parent_type,
                                                                 Json::Reader& r,
                                                                 const Json::Object& obj,
                                                                 bool allow_hash_portversion);

    SchemedVersion visit_required_schemed_deserializer(const LocalizedString& parent_type,
                                                       Json::Reader& r,
                                                       const Json::Object& obj,
                                                       bool allow_hash_portversion = false);
    View<StringView> schemed_deserializer_fields();

    void serialize_schemed_version(Json::Object& out_obj, VersionScheme scheme, StringView version, int port_version);

    struct VersionConstraintStringDeserializer : Json::StringDeserializer
    {
        LocalizedString type_name() const;

        static const VersionConstraintStringDeserializer instance;
    };
}
