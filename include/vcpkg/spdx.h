#pragma once

#include <vcpkg/base/fwd/files.h>
#include <vcpkg/base/fwd/json.h>
#include <vcpkg/base/fwd/stringview.h>

#include <vcpkg/fwd/dependencies.h>

#include <vcpkg/base/optional.h>
#include <vcpkg/base/span.h>

#include <string>

namespace vcpkg
{
    /// Generate an SPDX 2.3 manifest (https://spdx.github.io/spdx-spec)
    /// @param action Install action to be represented by this manifest
    /// @param relative_paths Must contain relative paths of all files in the port directory (from the port directory)
    /// @param hashes Must contain ordered hashes of `relative_paths`
    /// @param relative_package_files Must contain relative paths of all files in the package directory
    /// @param package_hashes Must contain ordered hashes of `relative_package_files`
    /// @param created_time SPDX creation time in YYYY-MM-DDThh:mm:ssZ format
    /// @param document_namespace Universally unique URI representing this SPDX document. See
    /// https://spdx.github.io/spdx-spec/document-creation-information/#65-spdx-document-namespace-field
    /// @param resource_doc An additional document to concatenate into the created document. This is intended to
    /// capture fetched resources, such as tools or source archives.
    std::string create_spdx_sbom(const InstallPlanAction& action,
                                 View<Path> relative_paths,
                                 View<std::string> hashes,
                                 View<Path> relative_package_files,
                                 View<std::string> package_hashes,
                                 std::string created_time,
                                 std::string document_namespace,
                                 Json::Object&& resource_doc);

    std::string calculate_spdx_license(const InstallPlanAction& action);

    Optional<std::string> read_spdx_license_text(StringView text, StringView origin);
}
