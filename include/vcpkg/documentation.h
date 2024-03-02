#pragma once

#include <vcpkg/base/stringview.h>

namespace vcpkg
{
    namespace docs
    {
        inline constexpr StringLiteral registries_url = "https://learn.microsoft.com/vcpkg/users/registries";
        inline constexpr StringLiteral manifests_url = "https://learn.microsoft.com/vcpkg/users/manifests";
        inline constexpr StringLiteral package_name_url = "https://learn.microsoft.com/vcpkg/reference/vcpkg-json#name";
        inline constexpr StringLiteral assetcaching_url = "https://learn.microsoft.com/vcpkg/users/assetcaching";
        inline constexpr StringLiteral binarycaching_url = "https://learn.microsoft.com/vcpkg/users/binarycaching";
        inline constexpr StringLiteral versioning_url = "https://learn.microsoft.com/vcpkg/users/versioning";
        inline constexpr StringLiteral vcpkg_visual_studio_path_url =
            "https://learn.microsoft.com/vcpkg/users/triplets#VCPKG_VISUAL_STUDIO_PATH";
    }
}
