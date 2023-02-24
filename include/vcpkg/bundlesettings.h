#pragma once

#include <vcpkg/base/fwd/format.h>

#include <vcpkg/fwd/bundlesettings.h>

#include <vcpkg/base/expected.h>
#include <vcpkg/base/file-contents.h>
#include <vcpkg/base/optional.h>
#include <vcpkg/base/stringview.h>

#include <string>

namespace vcpkg
{
    std::string to_string(DeploymentKind);
    StringLiteral to_string_literal(DeploymentKind) noexcept;

    struct BundleSettings
    {
        bool read_only = false;
        bool use_git_registry = false;
        Optional<std::string> embedded_git_sha;
        DeploymentKind deployment = DeploymentKind::Git;

        std::string to_string() const;
    };

    ExpectedL<BundleSettings> try_parse_bundle_settings(const FileContents& bundle_contents);
}

VCPKG_FORMAT_WITH_TO_STRING_LITERAL_NONMEMBER(vcpkg::DeploymentKind);
VCPKG_FORMAT_WITH_TO_STRING(vcpkg::BundleSettings);
