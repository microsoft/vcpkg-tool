#pragma once

#include <vcpkg/base/stringview.h>

namespace vcpkg
{
    namespace docs
    {
        static constexpr StringLiteral registries_url =
            "https://learn.microsoft.com/vcpkg/users/registries?WT.mc_id=vcpkg_inproduct_cli";
        static constexpr StringLiteral manifests_url =
            "https://learn.microsoft.com/vcpkg/users/manifests?WT.mc_id=vcpkg_inproduct_cli";
        static constexpr StringLiteral vcpkg_json_ref_name =
            "https://learn.microsoft.com/vcpkg/reference/vcpkg-json?WT.mc_id=vcpkg_inproduct_cli#name";
        static constexpr StringLiteral assetcaching_url =
            "https://learn.microsoft.com/vcpkg/users/assetcaching?WT.mc_id=vcpkg_inproduct_cli";
        static constexpr StringLiteral binarycaching_url =
            "https://learn.microsoft.com/vcpkg/users/binarycaching?WT.mc_id=vcpkg_inproduct_cli";
        static constexpr StringLiteral versioning_url =
            "https://learn.microsoft.com/vcpkg/users/versioning?WT.mc_id=vcpkg_inproduct_cli";
        static constexpr StringLiteral version_schemes =
            "https://learn.microsoft.com/vcpkg/users/versioning?WT.mc_id=vcpkg_inproduct_cli#version-schemes";
        static constexpr StringLiteral triplets_url =
            "https://learn.microsoft.com/vcpkg/users/triplets?WT.mc_id=vcpkg_inproduct_cli";
        static constexpr StringLiteral vcpkg_visual_studio_path_url =
            "https://learn.microsoft.com/vcpkg/users/triplets?WT.mc_id=vcpkg_inproduct_cli#VCPKG_VISUAL_STUDIO_PATH";
        inline constexpr StringLiteral package_name_url =
            "https://learn.microsoft.com/vcpkg/reference/vcpkg-json?WT.mc_id=vcpkg_inproduct_cli#name";
        static constexpr StringLiteral troubleshoot_build_failures_url =
            "https://learn.microsoft.com/vcpkg/troubleshoot/build-failures?WT.mc_id=vcpkg_inproduct_cli";
        static constexpr StringLiteral add_command_url =
            "https://learn.microsoft.com/vcpkg/commands/add?WT.mc_id=vcpkg_inproduct_cli";
        static constexpr StringLiteral add_command_recurse_opt_url =
            "https://learn.microsoft.com/vcpkg/commands/remove?WT.mc_id=vcpkg_inproduct_cli#--recurse";
        static constexpr StringLiteral add_version_command_url =
            "https://learn.microsoft.com/vcpkg/commands/add-version?WT.mc_id=vcpkg_inproduct_cli";
        static constexpr StringLiteral add_version_command_overwrite_version_opt_url =
            "https://learn.microsoft.com/vcpkg/commands/add-version?WT.mc_id=vcpkg_inproduct_cli#--overwrite-version";
        static constexpr StringLiteral radd_version_command_all_opt_url =
            "https://learn.microsoft.com/vcpkg/commands/add-version?WT.mc_id=vcpkg_inproduct_cli#--all";
        static constexpr StringLiteral format_manifest_command_url =
            "https://learn.microsoft.com/vcpkg/commands/format-manifest?WT.mc_id=vcpkg_inproduct_cli";
        static constexpr StringLiteral troubleshoot_binary_cache_url =
            "https://learn.microsoft.com/vcpkg/users/binarycaching-troubleshooting?WT.mc_id=vcpkg_inproduct_cli";
        static constexpr StringLiteral troubleshoot_versioning_url =
            "https://learn.microsoft.com/vcpkg/users/versioning-troubleshooting?WT.mc_id=vcpkg_inproduct_cli";
    }
}
