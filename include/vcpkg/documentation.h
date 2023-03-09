#pragma once

#include <vcpkg/base/stringview.h>

namespace vcpkg
{
    namespace docs
    {
        static constexpr StringLiteral registries_url = "https://learn.microsoft.com/vcpkg/users/registries";
        static constexpr StringLiteral manifests_url = "https://learn.microsoft.com/vcpkg/users/manifests";
        static constexpr StringLiteral assetcaching_url = "https://learn.microsoft.com/vcpkg/users/assetcaching";
        static constexpr StringLiteral binarycaching_url = "https://learn.microsoft.com/vcpkg/users/binarycaching";
        static constexpr StringLiteral versioning_url = "https://learn.microsoft.com/vcpkg/users/versioning";
        static constexpr StringLiteral vcpkg_visual_studio_path_url =
            "https://learn.microsoft.com/vcpkg/users/triplets#VCPKG_VISUAL_STUDIO_PATH";
    }
}
