#pragma once

#include <vcpkg/base/fwd/stringview.h>

#include <vcpkg/base/jsonreader.h>

#include <vcpkg/versions.h>

namespace vcpkg
{
    Json::IDeserializer<Version>& get_version_deserializer_instance();
    Json::IDeserializer<Version>& get_versiontag_deserializer_instance();

    struct SchemedVersion
    {
        VersionScheme scheme;
        Version version;
    };

    Optional<SchemedVersion> visit_optional_schemed_deserializer(StringView parent_type,
                                                                 Json::Reader& r,
                                                                 const Json::Object& obj,
                                                                 bool allow_hash_portversion);

    SchemedVersion visit_required_schemed_deserializer(StringView parent_type,
                                                       Json::Reader& r,
                                                       const Json::Object& obj,
                                                       bool allow_hash_portversion = false);
    View<StringView> schemed_deserializer_fields();

    void serialize_schemed_version(Json::Object& out_obj, VersionScheme scheme, StringView version, int port_version);
}
