#pragma once

#include <vcpkg/base/fwd/format.h>

#include <vcpkg/base/expected.h>
#include <vcpkg/base/file-contents.h>
#include <vcpkg/base/optional.h>

#include <string>

namespace vcpkg
{
    struct BundleSettings
    {
        bool read_only = false;
        bool use_git_registry = false;
        Optional<std::string> embedded_git_sha;

        std::string to_string() const;
    };

    ExpectedL<BundleSettings> try_parse_bundle_settings(const FileContents& bundle_contents);
}

VCPKG_FORMAT_WITH_TO_STRING(vcpkg::BundleSettings);
