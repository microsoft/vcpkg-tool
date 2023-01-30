#pragma once

#include <vcpkg/base/span.h>

#include <vcpkg/dependencies.h>

#include <string>

namespace vcpkg
{
    /// Generate an SDPX 2.2.1 manifest (https://spdx.github.io/spdx-spec)
    /// @param action Install action to be represented by this manifest
    /// @param relative_paths Must contain relative paths of all files in the port directory (from the port directory)
    /// @param hashes Must contain ordered hashes of `relative_paths`
    /// @param created_time SPDX creation time in YYYY-MM-DDThh:mm:ssZ format
    /// @param document_namespace Universally unique URI representing this SPDX document. See
    /// https://spdx.github.io/spdx-spec/document-creation-information/#65-spdx-document-namespace-field
    /// @param resource_docs Additional documents to concatenate into the created document. These are intended to
    /// capture fetched resources, such as tools or source archives.
    std::string create_spdx_sbom(const InstallPlanAction& action,
                                 View<Path> relative_paths,
                                 View<std::string> hashes,
                                 std::string created_time,
                                 std::string document_namespace,
                                 std::vector<Json::Value>&& resource_docs);

    Json::Value run_resource_heuristics(StringView contents);
}
