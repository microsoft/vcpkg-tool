#pragma once

#include <vcpkg/base/stringview.h>

namespace vcpkg
{
    namespace docs
    {
        static constexpr StringLiteral registries_url =
            "https://github.com/Microsoft/vcpkg/tree/master/docs/users/registries.md";
        static constexpr StringLiteral manifests_url =
            "https://github.com/Microsoft/vcpkg/tree/master/docs/users/manifests.md";
        static constexpr StringLiteral assetcaching_url =
            "https://github.com/Microsoft/vcpkg/tree/master/docs/users/assetcaching.md";
        static constexpr StringLiteral binarycaching_url =
            "https://github.com/Microsoft/vcpkg/tree/master/docs/users/binarycaching.md";
        static constexpr StringLiteral versioning_url =
            "https://github.com/Microsoft/vcpkg/tree/master/docs/users/versioning.md";
        static constexpr StringLiteral vcpkg_visual_studio_path_url =
            "https://github.com/microsoft/vcpkg/blob/master/docs/users/triplets.md#VCPKG_VISUAL_STUDIO_PATH";
    }
}
