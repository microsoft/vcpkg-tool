#pragma once

#include <vcpkg/base/jsonreader.h>
#include <vcpkg/base/path.h>

#include <vcpkg/versions.h>

#include <memory>
#include <string>
#include <vector>

namespace vcpkg
{

    struct VersionDbEntry
    {
        Version version;
        VersionScheme scheme = VersionScheme::String;

        // only one of these may be non-empty
        std::string git_tree;
        Path p;
    };

    // VersionDbType::Git => VersionDbEntry.git_tree is filled
    // VersionDbType::Filesystem => VersionDbEntry.path is filled
    enum class VersionDbType
    {
        Git,
        Filesystem,
    };

    std::unique_ptr<Json::IDeserializer<std::vector<VersionDbEntry>>> make_version_db_deserializer(VersionDbType type,
                                                                                                   const Path& root);
}
