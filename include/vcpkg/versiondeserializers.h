#pragma once

#include <vcpkg/base/fwd/stringview.h>

#include <vcpkg/base/jsonreader.h>
#include <vcpkg/base/stringliteral.h>

#include <vcpkg/versions.h>
#include <vcpkg/versiont.h>

namespace vcpkg
{
    Json::IDeserializer<VersionT>& get_versiont_deserializer_instance();
    Json::IDeserializer<VersionT>& get_versiontag_deserializer_instance();

    struct SchemedVersion
    {
        SchemedVersion() = default;
        SchemedVersion(Versions::Scheme scheme_, VersionT versiont_) : scheme(scheme_), versiont(std::move(versiont_))
        {
        }

        Versions::Scheme scheme = Versions::Scheme::String;
        VersionT versiont;
    };

    SchemedVersion visit_required_schemed_deserializer(StringView parent_type,
                                                       Json::Reader& r,
                                                       const Json::Object& obj,
                                                       bool allow_hash_portversion = false);
    View<StringView> schemed_deserializer_fields();

    void serialize_schemed_version(Json::Object& out_obj,
                                   Versions::Scheme scheme,
                                   const std::string& version,
                                   int port_version,
                                   bool always_emit_port_version = false);
}
