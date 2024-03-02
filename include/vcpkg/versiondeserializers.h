#pragma once

#include <vcpkg/base/fwd/messages.h>

#include <vcpkg/fwd/versions.h>

#include <vcpkg/base/jsonreader.h>
#include <vcpkg/base/stringview.h>

namespace vcpkg
{
    extern const Json::IDeserializer<Version>& baseline_version_tag_deserializer;

    Optional<SchemedVersion> visit_optional_schemed_version(const LocalizedString& parent_type,
                                                            Json::Reader& r,
                                                            const Json::Object& obj);

    SchemedVersion visit_required_schemed_version(const LocalizedString& parent_type,
                                                  Json::Reader& r,
                                                  const Json::Object& obj);

    Version visit_version_override_version(const LocalizedString& parent_type,
                                           Json::Reader& r,
                                           const Json::Object& obj);

    View<StringView> schemed_deserializer_fields();

    void serialize_schemed_version(Json::Object& out_obj, VersionScheme scheme, const Version& version);

    struct VersionConstraintStringDeserializer : Json::StringDeserializer
    {
        virtual LocalizedString type_name() const override;

        static const VersionConstraintStringDeserializer instance;
    };
}
