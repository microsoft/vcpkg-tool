#pragma once
#include <vcpkg/base/jsonreader.h>

#include <vcpkg/registries.h>

namespace vcpkg
{
    struct FilesystemVersionDbEntryDeserializer final : Json::IDeserializer<FilesystemVersionDbEntry>
    {
        LocalizedString type_name() const override;
        View<StringLiteral> valid_fields() const noexcept override;
        Optional<FilesystemVersionDbEntry> visit_object(Json::Reader& r, const Json::Object& obj) const override;
        FilesystemVersionDbEntryDeserializer(const Path& root) : registry_root(root) { }

    private:
        Path registry_root;
    };

    struct FilesystemVersionDbEntryArrayDeserializer final : Json::IDeserializer<std::vector<FilesystemVersionDbEntry>>
    {
        virtual LocalizedString type_name() const override;
        virtual Optional<std::vector<FilesystemVersionDbEntry>> visit_array(Json::Reader& r,
                                                                            const Json::Array& arr) const override;
        FilesystemVersionDbEntryArrayDeserializer(const Path& root) : underlying{root} { }

    private:
        FilesystemVersionDbEntryDeserializer underlying;
    };

    struct GitVersionDbEntryDeserializer final : Json::IDeserializer<GitVersionDbEntry>
    {
        LocalizedString type_name() const override;
        View<StringLiteral> valid_fields() const noexcept override;
        Optional<GitVersionDbEntry> visit_object(Json::Reader& r, const Json::Object& obj) const override;
    };

    struct GitVersionDbEntryArrayDeserializer final : Json::IDeserializer<std::vector<GitVersionDbEntry>>
    {
        virtual LocalizedString type_name() const override;
        virtual Optional<std::vector<GitVersionDbEntry>> visit_array(Json::Reader& r,
                                                                     const Json::Array& arr) const override;
    };
}
