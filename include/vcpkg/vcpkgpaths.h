#pragma once

#include <vcpkg/base/fwd/json.h>
#include <vcpkg/base/fwd/system.process.h>

#include <vcpkg/fwd/configuration.h>
#include <vcpkg/fwd/registries.h>
#include <vcpkg/fwd/vcpkgcmdarguments.h>
#include <vcpkg/fwd/vcpkgpaths.h>

#include <vcpkg/base/cache.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/lazy.h>
#include <vcpkg/base/optional.h>
#include <vcpkg/base/system.h>
#include <vcpkg/base/util.h>

namespace vcpkg
{
    struct ToolsetArchOption
    {
        CStringView name;
        System::CPUArchitecture host_arch;
        System::CPUArchitecture target_arch;
    };

    struct Toolset
    {
        fs::path visual_studio_root_path;
        fs::path dumpbin;
        fs::path vcvarsall;
        std::vector<std::string> vcvarsall_options;
        CStringView version;
        std::vector<ToolsetArchOption> supported_architectures;
    };

    namespace Downloads
    {
        struct DownloadManager;
    }

    namespace Build
    {
        struct PreBuildInfo;
        struct AbiInfo;
        struct CompilerInfo;
    }

    namespace System
    {
        struct Environment;
    }

    namespace details
    {
        struct VcpkgPathsImpl;
    }

    struct BinaryParagraph;
    struct PackageSpec;
    struct Triplet;

    struct VcpkgPaths
    {
        struct TripletFile
        {
            std::string name;
            fs::path location;

            TripletFile(const std::string& name, const fs::path& location) : name(name), location(location) { }
        };

        VcpkgPaths(Files::Filesystem& filesystem, const VcpkgCmdArguments& args);
        VcpkgPaths(const VcpkgPaths&) = delete;
        VcpkgPaths(VcpkgPaths&&) = default;
        VcpkgPaths& operator=(const VcpkgPaths&) = delete;
        VcpkgPaths& operator=(VcpkgPaths&&) = default;
        ~VcpkgPaths();

        fs::path package_dir(const PackageSpec& spec) const;
        fs::path build_dir(const PackageSpec& spec) const;
        fs::path build_dir(const std::string& package_name) const;
        fs::path build_info_file_path(const PackageSpec& spec) const;
        fs::path listfile_path(const BinaryParagraph& pgh) const;

        bool is_valid_triplet(Triplet t) const;
        const std::vector<std::string> get_available_triplets_names() const;
        const std::vector<TripletFile>& get_available_triplets() const;
        const std::map<std::string, std::string>& get_cmake_script_hashes() const;
        StringView get_ports_cmake_hash() const;
        const fs::path get_triplet_file_path(Triplet triplet) const;

        LockFile& get_installed_lockfile() const;
        void flush_lockfile() const;

        fs::path original_cwd;
        fs::path root;
        fs::path manifest_root_dir;
        fs::path config_root_dir;
        fs::path buildtrees;
        fs::path downloads;
        fs::path packages;
        fs::path installed;
        fs::path triplets;
        fs::path community_triplets;
        fs::path scripts;
        fs::path prefab;
        fs::path builtin_ports;
        fs::path builtin_registry_versions;

        // When we are in a custom registry, these values are different from builtin_ports, otherwise they are the same
        fs::path current_registry_dot_git_dir;
        fs::path current_registry_ports;
        fs::path current_registry_versions;

        fs::path tools;
        fs::path buildsystems;
        fs::path buildsystems_msbuild_targets;
        fs::path buildsystems_msbuild_props;

        fs::path vcpkg_dir;
        fs::path vcpkg_dir_status_file;
        fs::path vcpkg_dir_info;
        fs::path vcpkg_dir_updates;

        fs::path baselines_dot_git_dir;
        fs::path baselines_work_tree;
        fs::path baselines_output;

        fs::path versions_dot_git_dir;
        fs::path versions_work_tree;
        fs::path versions_output;

        fs::path ports_cmake;

        const fs::path& get_tool_exe(const std::string& tool) const;
        const std::string& get_tool_version(const std::string& tool) const;

        System::Command git_cmd_builder(const fs::path& dot_git_dir, const fs::path& work_tree) const;

        // Git manipulation in the vcpkg directory
        ExpectedS<std::string> get_current_git_sha() const;
        std::string get_current_git_sha_message() const;
        ExpectedS<fs::path> git_checkout_baseline(StringView commit_sha) const;
        ExpectedS<fs::path> git_checkout_port(StringView port_name,
                                              StringView git_tree,
                                              const fs::path& dot_git_dir) const;
        ExpectedS<std::string> git_show(const std::string& treeish, const fs::path& dot_git_dir) const;

        const Downloads::DownloadManager& get_download_manager() const;

        ExpectedS<std::map<std::string, std::string, std::less<>>> git_get_local_port_treeish_map() const;

        // Git manipulation for remote registries
        // runs `git fetch {uri} {treeish}`, and returns the hash of FETCH_HEAD.
        // Use {treeish} of "HEAD" for the default branch
        ExpectedS<std::string> git_fetch_from_remote_registry(StringView uri, StringView treeish) const;
        // runs `git fetch {uri} {treeish}`
        Optional<std::string> git_fetch(StringView uri, StringView treeish) const;
        ExpectedS<std::string> git_show_from_remote_registry(StringView hash,
                                                             const fs::path& relative_path_to_file) const;
        ExpectedS<std::string> git_find_object_id_for_remote_registry_path(StringView hash,
                                                                           const fs::path& relative_path_to_file) const;
        ExpectedS<fs::path> git_checkout_object_from_remote_registry(StringView tree) const;

        Optional<const Json::Object&> get_manifest() const;
        Optional<const fs::path&> get_manifest_path() const;
        const Configuration& get_configuration() const;

        /// <summary>Retrieve a toolset matching a VS version</summary>
        /// <remarks>
        ///   Valid version strings are "v120", "v140", "v141", and "". Empty string gets the latest.
        /// </remarks>
        const Toolset& get_toolset(const Build::PreBuildInfo& prebuildinfo) const;

        View<Toolset> get_all_toolsets() const;

        Files::Filesystem& get_filesystem() const;

        const System::Environment& get_action_env(const Build::AbiInfo& abi_info) const;
        const std::string& get_triplet_info(const Build::AbiInfo& abi_info) const;
        const Build::CompilerInfo& get_compiler_info(const Build::AbiInfo& abi_info) const;
        bool manifest_mode_enabled() const { return get_manifest().has_value(); }

        const FeatureFlagSettings& get_feature_flags() const;
        void track_feature_flag_metrics() const;

        // the directory of the builtin ports
        // this should be used only for helper commands, not core commands like `install`.
        fs::path builtin_ports_directory() const { return this->builtin_ports; }

    private:
        std::unique_ptr<details::VcpkgPathsImpl> m_pimpl;
    };
}
