#pragma once

#include <vcpkg/base/fwd/json.h>
#include <vcpkg/base/fwd/system.process.h>

#include <vcpkg/fwd/configuration.h>
#include <vcpkg/fwd/installedpaths.h>
#include <vcpkg/fwd/registries.h>
#include <vcpkg/fwd/vcpkgcmdarguments.h>
#include <vcpkg/fwd/vcpkgpaths.h>

#include <vcpkg/base/cache.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/lazy.h>
#include <vcpkg/base/optional.h>
#include <vcpkg/base/system.h>
#include <vcpkg/base/util.h>

#include <vcpkg/packagespec.h>
#include <vcpkg/triplet.h>

namespace vcpkg
{
    struct ToolsetArchOption
    {
        ZStringView name;
        CPUArchitecture host_arch;
        CPUArchitecture target_arch;
    };

    struct Toolset
    {
        Path visual_studio_root_path;
        Path dumpbin;
        Path vcvarsall;
        std::vector<std::string> vcvarsall_options;
        ZStringView version;
        std::string full_version;
        std::vector<ToolsetArchOption> supported_architectures;
    };

    struct DownloadManager;

    namespace Build
    {
        struct PreBuildInfo;
        struct AbiInfo;
        struct CompilerInfo;
    }

    namespace details
    {
        struct VcpkgPathsImpl;
    }

    struct BinaryParagraph;
    struct Environment;
    struct PackageSpec;
    struct Triplet;

    struct VcpkgPaths
    {
        struct TripletFile
        {
            std::string name;
            Path location;

            TripletFile(StringView name, StringView location) : name(name.data(), name.size()), location(location) { }
        };

        VcpkgPaths(Filesystem& filesystem, const VcpkgCmdArguments& args);
        VcpkgPaths(const VcpkgPaths&) = delete;
        VcpkgPaths& operator=(const VcpkgPaths&) = delete;
        ~VcpkgPaths();

        Path package_dir(const PackageSpec& spec) const;
        Path build_dir(const PackageSpec& spec) const;
        Path build_dir(const std::string& package_name) const;
        Path build_info_file_path(const PackageSpec& spec) const;

        bool is_valid_triplet(Triplet t) const;
        const std::vector<std::string> get_available_triplets_names() const;
        const std::vector<TripletFile>& get_available_triplets() const;
        const std::map<std::string, std::string>& get_cmake_script_hashes() const;
        StringView get_ports_cmake_hash() const;
        const Path get_triplet_file_path(Triplet triplet) const;

        LockFile& get_installed_lockfile() const;
        void flush_lockfile() const;

        const Optional<InstalledPaths>& maybe_installed() const;
        const Optional<Path>& maybe_buildtrees() const;
        const Optional<Path>& maybe_packages() const;

        const InstalledPaths& installed() const;
        const Path& buildtrees() const;
        const Path& packages() const;

        Path baselines_output() const;
        Path versions_output() const;

        const Path original_cwd;
        const Path root;

    private:
        const std::unique_ptr<details::VcpkgPathsImpl> m_pimpl;

    public:
        const Path builtin_registry_versions;
        const Path scripts;
        const Path prefab;
        const Path buildsystems;
        const Path buildsystems_msbuild_targets;
        const Path buildsystems_msbuild_props;
        const Path downloads;
        const Path tools;
        const Path ports_cmake;
        const Path triplets;
        const Path community_triplets;

        std::string get_toolver_diagnostics() const;

        const Path& get_tool_exe(StringView tool) const;
        const std::string& get_tool_version(StringView tool) const;

        Command git_cmd_builder(const Path& dot_git_dir, const Path& work_tree) const;

        // Git manipulation in the vcpkg directory
        ExpectedS<std::string> get_current_git_sha() const;
        std::string get_current_git_sha_baseline_message() const;
        ExpectedS<Path> git_checkout_port(StringView port_name, StringView git_tree, const Path& dot_git_dir) const;
        ExpectedS<std::string> git_show(const std::string& treeish, const Path& dot_git_dir) const;

        const DownloadManager& get_download_manager() const;

        ExpectedS<std::map<std::string, std::string, std::less<>>> git_get_local_port_treeish_map() const;

        // Git manipulation for remote registries
        // runs `git fetch {uri} {treeish}`, and returns the hash of FETCH_HEAD.
        // Use {treeish} of "HEAD" for the default branch
        ExpectedS<std::string> git_fetch_from_remote_registry(StringView uri, StringView treeish) const;
        // runs `git fetch {uri} {treeish}`
        Optional<std::string> git_fetch(StringView uri, StringView treeish) const;
        ExpectedS<std::string> git_show_from_remote_registry(StringView hash, const Path& relative_path_to_file) const;
        ExpectedS<std::string> git_find_object_id_for_remote_registry_path(StringView hash,
                                                                           const Path& relative_path_to_file) const;
        ExpectedS<Path> git_checkout_object_from_remote_registry(StringView tree) const;

        Optional<const Json::Object&> get_manifest() const;
        Optional<const Path&> get_manifest_path() const;
        const RegistrySet& get_registry_set() const;

        // Retrieve a toolset matching the requirements in prebuildinfo
        const Toolset& get_toolset(const Build::PreBuildInfo& prebuildinfo) const;

        Filesystem& get_filesystem() const;

        const Environment& get_action_env(const Build::AbiInfo& abi_info) const;
        const std::string& get_triplet_info(const Build::AbiInfo& abi_info) const;
        const Build::CompilerInfo& get_compiler_info(const Build::AbiInfo& abi_info) const;
        bool manifest_mode_enabled() const { return get_manifest().has_value(); }

        const FeatureFlagSettings& get_feature_flags() const;
        void track_feature_flag_metrics() const;

        // the directory of the builtin ports
        // this should be used only for helper commands, not core commands like `install`.
        const Path& builtin_ports_directory() const;

        bool use_git_default_registry() const;
    };
}
