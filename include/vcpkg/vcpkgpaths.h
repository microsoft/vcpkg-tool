#pragma once

#include <vcpkg/base/fwd/downloads.h>
#include <vcpkg/base/fwd/expected.h>
#include <vcpkg/base/fwd/files.h>
#include <vcpkg/base/fwd/git.h>
#include <vcpkg/base/fwd/system.h>
#include <vcpkg/base/fwd/system.process.h>

#include <vcpkg/fwd/build.h>
#include <vcpkg/fwd/bundlesettings.h>
#include <vcpkg/fwd/configuration.h>
#include <vcpkg/fwd/installedpaths.h>
#include <vcpkg/fwd/packagespec.h>
#include <vcpkg/fwd/registries.h>
#include <vcpkg/fwd/sourceparagraph.h>
#include <vcpkg/fwd/tools.h>
#include <vcpkg/fwd/triplet.h>
#include <vcpkg/fwd/vcpkgcmdarguments.h>
#include <vcpkg/fwd/vcpkgpaths.h>

#include <vcpkg/base/diagnostics.h>
#include <vcpkg/base/optional.h>
#include <vcpkg/base/path.h>

#include <vcpkg/portfileprovider.h>

#include <map>
#include <string>
#include <vector>

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
        Path vcvarsall;
        std::vector<std::string> vcvarsall_options;
        ZStringView version;
        std::string full_version;
        std::vector<ToolsetArchOption> supported_architectures;
    };

    struct VcpkgPaths
    {
        VcpkgPaths(const Filesystem& filesystem, const VcpkgCmdArguments& args, const BundleSettings& bundle);
        VcpkgPaths(const VcpkgPaths&) = delete;
        VcpkgPaths& operator=(const VcpkgPaths&) = delete;
        ~VcpkgPaths();

        Path package_dir(const PackageSpec& spec) const;
        Path build_dir(const PackageSpec& spec) const;
        Path build_dir(StringView package_name) const;

        const TripletDatabase& get_triplet_db() const;
        const std::map<std::string, std::string>& get_cmake_script_hashes() const;
        StringView get_ports_cmake_hash() const;

        LockFile& get_installed_lockfile() const;
        void flush_lockfile() const;

        const Optional<InstalledPaths>& maybe_installed() const;
        const Optional<Path>& maybe_buildtrees() const;
        const Optional<Path>& maybe_packages() const;

        const Path& global_config() const;
        const InstalledPaths& installed() const;
        const Path& buildtrees() const;
        const Path& packages() const;

        Path baselines_output() const;
        Path versions_output() const;

        const Path original_cwd;
        const Path root;

    private:
        const std::unique_ptr<VcpkgPathsImpl> m_pimpl;

    public:
        const Path& scripts;
        const Path& downloads;
        const Path& tools;
        const Path builtin_registry_versions;
        ExpectedL<Path> versions_dot_git_dir() const;
        const Path prefab;
        const Path buildsystems;
        const Path buildsystems_msbuild_targets;
        const Path buildsystems_msbuild_props;
        const Path ports_cmake;

    private:
        const Path triplets;
        const Path community_triplets;
        std::vector<Path> overlay_triplets;

    public:
        OverlayPortPaths overlay_ports;

        std::string get_toolver_diagnostics() const;

        const Filesystem& get_filesystem() const;
        const AssetCachingSettings& get_asset_cache_settings() const;
        const ToolCache& get_tool_cache() const;
        const Path& get_tool_exe(StringView tool, MessageSink& status_messages) const;
        const std::string& get_tool_version(StringView tool, MessageSink& status_messages) const;

        Command git_cmd_builder(const Path& dot_git_dir, const Path& work_tree) const;

        // Git manipulation in the vcpkg directory
        ExpectedL<std::string> get_current_git_sha() const;
        LocalizedString get_current_git_sha_baseline_message() const;
        ExpectedL<Path> git_checkout_port(StringView port_name, StringView git_tree, const Path& dot_git_dir) const;
        ExpectedL<std::string> git_show(StringView treeish, const Path& dot_git_dir) const;
        Optional<std::vector<GitLSTreeEntry>> get_builtin_ports_directory_trees(DiagnosticContext& context) const;

        // Git manipulation for remote registries
        // runs `git fetch {uri} {treeish}`, and returns the hash of FETCH_HEAD.
        // Use {treeish} of "HEAD" for the default branch
        ExpectedL<std::string> git_fetch_from_remote_registry(StringView uri, StringView treeish) const;
        // runs `git fetch {uri} {treeish}`
        ExpectedL<Unit> git_fetch(StringView uri, StringView treeish) const;
        ExpectedL<std::string> git_show_from_remote_registry(StringView hash, const Path& relative_path_to_file) const;
        ExpectedL<std::string> git_find_object_id_for_remote_registry_path(StringView hash,
                                                                           const Path& relative_path_to_file) const;
        ExpectedL<Unit> git_read_tree(const Path& destination, StringView tree, const Path& dot_git_dir) const;
        ExpectedL<Path> git_extract_tree_from_remote_registry(StringView tree) const;

        Optional<const ManifestAndPath&> get_manifest() const;
        bool manifest_mode_enabled() const;
        const ConfigurationAndSource& get_configuration() const;
        std::unique_ptr<RegistrySet> make_registry_set() const;

        // Retrieve a toolset matching the requirements in prebuildinfo
        const Toolset& get_toolset(const PreBuildInfo& prebuildinfo) const;

        const Environment& get_action_env(const PreBuildInfo& pre_build_info, const Toolset& toolset) const;
        const std::string& get_triplet_info(const PreBuildInfo& pre_build_info, const Toolset& toolset) const;
        const CompilerInfo& get_compiler_info(const PreBuildInfo& pre_build_info, const Toolset& toolset) const;

        const FeatureFlagSettings& get_feature_flags() const;

        // the directory of the builtin ports
        // this should be used only for helper commands, not core commands like `install`.
        const Path& builtin_ports_directory() const;

        bool use_git_default_registry() const;

        const Path& artifacts() const;
        const Path& registries_cache() const;
    };
}
