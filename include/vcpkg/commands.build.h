#pragma once

#include <vcpkg/base/fwd/system.process.h>

#include <vcpkg/fwd/binarycaching.h>
#include <vcpkg/fwd/build.h>
#include <vcpkg/fwd/cmakevars.h>
#include <vcpkg/fwd/dependencies.h>
#include <vcpkg/fwd/portfileprovider.h>
#include <vcpkg/fwd/vcpkgcmdarguments.h>

#include <vcpkg/base/cache.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/optional.h>
#include <vcpkg/base/path.h>
#include <vcpkg/base/stringview.h>
#include <vcpkg/base/system.process.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.integrate.h>
#include <vcpkg/packagespec.h>
#include <vcpkg/statusparagraphs.h>
#include <vcpkg/triplet.h>
#include <vcpkg/vcpkgpaths.h>

#include <map>
#include <memory>
#include <set>
#include <vector>

namespace vcpkg
{
    struct IBuildLogsRecorder
    {
        virtual void record_build_result(const VcpkgPaths& paths,
                                         const PackageSpec& spec,
                                         BuildResult result) const = 0;
    };

    extern const IBuildLogsRecorder& null_build_logs_recorder;

    struct CiBuildLogsRecorder final : IBuildLogsRecorder
    {
        explicit CiBuildLogsRecorder(const Path& base_path_, int64_t minimum_last_write_time);

        CiBuildLogsRecorder(const CiBuildLogsRecorder&) = delete;
        CiBuildLogsRecorder& operator=(const CiBuildLogsRecorder&) = delete;

        void record_build_result(const VcpkgPaths& paths, const PackageSpec& spec, BuildResult result) const override;

    private:
        Path base_path;
        int64_t minimum_last_write_time;
    };

    struct PackagesDirAssigner
    {
        explicit PackagesDirAssigner(const Path& packages_dir);
        Path generate(const PackageSpec& spec);

    private:
        Path m_packages_dir;
        std::map<std::string, std::size_t, std::less<>> m_next_dir_count;
    };

    bool is_package_dir_match(StringView filename, StringView spec_dir);

    void purge_packages_dirs(const VcpkgPaths& paths, View<std::string> spec_dirs);
    template<class ActionKindVector,
             std::enable_if_t<std::is_convertible<typename ActionKindVector::value_type, BasicAction>::value, int> = 0>
    void purge_packages_dirs(const VcpkgPaths& paths, const ActionKindVector& actions)
    {
        purge_packages_dirs(paths, Util::fmap(actions, [](const auto& action) { return action.spec.dir(); }));
    }

    extern const CommandMetadata CommandBuildMetadata;
    int command_build_ex(const VcpkgCmdArguments& args,
                         const VcpkgPaths& paths,
                         Triplet host_triplet,
                         const BuildPackageOptions& build_options,
                         const FullPackageSpec& full_spec,
                         const PathsPortFileProvider& provider,
                         const IBuildLogsRecorder& build_logs_recorder);
    void command_build_and_exit_ex(const VcpkgCmdArguments& args,
                                   const VcpkgPaths& paths,
                                   Triplet host_triplet,
                                   const BuildPackageOptions& build_options,
                                   const FullPackageSpec& full_spec,
                                   const PathsPortFileProvider& provider,
                                   const IBuildLogsRecorder& build_logs_recorder);

    void command_build_and_exit(const VcpkgCmdArguments& args,
                                const VcpkgPaths& paths,
                                Triplet default_triplet,
                                Triplet host_triplet);

    struct BuildPackageOptions
    {
        BuildMissing build_missing;
        AllowDownloads allow_downloads;
        OnlyDownloads only_downloads;
        CleanBuildtrees clean_buildtrees;
        CleanPackages clean_packages;
        CleanDownloads clean_downloads;
        BackcompatFeatures backcompat_features;
        KeepGoing keep_going;
    };

    struct BuildResultCounts
    {
        int succeeded = 0;
        int build_failed = 0;
        int post_build_checks_failed = 0;
        int file_conflicts = 0;
        int cascaded_due_to_missing_dependencies = 0;
        int excluded = 0;
        int cache_missing = 0;
        int downloaded = 0;
        int removed = 0;

        void increment(const BuildResult build_result);
        LocalizedString format(const Triplet& triplet) const;
    };

    StringLiteral to_string_locale_invariant(const BuildResult build_result);
    LocalizedString to_string(const BuildResult build_result);
    LocalizedString create_user_troubleshooting_message(const InstallPlanAction& action,
                                                        CIKind detected_ci,
                                                        const VcpkgPaths& paths,
                                                        const std::vector<std::string>& error_logs,
                                                        const Optional<Path>& maybe_issue_body);
    inline void print_user_troubleshooting_message(const InstallPlanAction& action,
                                                   CIKind detected_ci,
                                                   const VcpkgPaths& paths,
                                                   const std::vector<std::string>& error_logs,
                                                   Optional<Path>&& maybe_issue_body)
    {
        msg::println(Color::error,
                     create_user_troubleshooting_message(action, detected_ci, paths, error_logs, maybe_issue_body));
    }

    /// <summary>
    /// Settings from the triplet file which impact the build environment and post-build checks
    /// </summary>
    struct PreBuildInfo
    {
        PreBuildInfo(const VcpkgPaths& paths,
                     Triplet triplet,
                     const std::unordered_map<std::string, std::string>& cmakevars);

        PreBuildInfo(const PreBuildInfo&) = delete;
        PreBuildInfo& operator=(const PreBuildInfo&) = delete;

        Triplet triplet;
        bool load_vcvars_env = false;
        bool disable_compiler_tracking = false;
        bool target_is_xbox = false;
        std::string target_architecture;
        std::string cmake_system_name;
        std::string cmake_system_version;
        Optional<std::string> platform_toolset;
        Optional<std::string> platform_toolset_version;
        Optional<Path> visual_studio_path;
        Optional<Path> external_toolchain_file;
        Optional<ConfigurationType> build_type;
        Optional<std::string> public_abi_override;
        std::vector<std::string> passthrough_env_vars;
        std::vector<std::string> passthrough_env_vars_tracked;
        std::vector<Path> hash_additional_files;
        std::vector<Path> post_portfile_includes;
        Optional<Path> gamedk_latest_path;

        Path toolchain_file() const;
        bool using_vcvars() const;

    private:
        const VcpkgPaths& m_paths;
    };

    vcpkg::Command make_build_env_cmd(const PreBuildInfo& pre_build_info, const Toolset& toolset);

    struct ExtendedBuildResult
    {
        explicit ExtendedBuildResult(BuildResult code);
        explicit ExtendedBuildResult(BuildResult code, vcpkg::Path stdoutlog, std::vector<std::string>&& error_logs);
        ExtendedBuildResult(BuildResult code, std::vector<FullPackageSpec>&& unmet_deps);
        ExtendedBuildResult(BuildResult code, std::unique_ptr<BinaryControlFile>&& bcf);

        BuildResult code;
        std::vector<FullPackageSpec> unmet_dependencies;
        std::unique_ptr<BinaryControlFile> binary_control_file;
        Optional<vcpkg::Path> stdoutlog;
        std::vector<std::string> error_logs;
    };

    void append_log(const Path& path, const std::string& log, size_t max_size, std::string& out);
    void append_logs(std::vector<std::pair<Path, std::string>>&& logs, size_t max_size, std::string& out);

    LocalizedString create_error_message(const ExtendedBuildResult& build_result, const PackageSpec& spec);

    std::string create_github_issue(const VcpkgCmdArguments& args,
                                    const ExtendedBuildResult& build_result,
                                    const VcpkgPaths& paths,
                                    const InstallPlanAction& action,
                                    bool include_manifest);

    ExtendedBuildResult build_package(const VcpkgCmdArguments& args,
                                      const VcpkgPaths& paths,
                                      Triplet host_triplet,
                                      const BuildPackageOptions& build_options,
                                      const InstallPlanAction& config,
                                      const IBuildLogsRecorder& build_logs_recorder,
                                      const StatusParagraphs& status_db);

    StringLiteral to_string_view(BuildPolicy policy);
    std::string to_string(BuildPolicy policy);
    StringLiteral to_cmake_variable(BuildPolicy policy);

    struct BuildPolicies
    {
        BuildPolicies() = default;
        BuildPolicies(std::unordered_map<BuildPolicy, bool>&& map) : m_policies(std::move(map)) { }

        bool is_enabled(BuildPolicy policy) const { return Util::copy_or_default(m_policies, policy); }

    private:
        std::unordered_map<BuildPolicy, bool> m_policies;
    };

    enum class LinkageType : char
    {
        Dynamic,
        Static,
    };

    Optional<LinkageType> to_linkage_type(StringView str);

    struct BuildInfo
    {
        LinkageType crt_linkage = LinkageType::Dynamic;
        LinkageType library_linkage = LinkageType::Dynamic;

        Optional<Version> detected_head_version;

        BuildPolicies policies;
    };

    BuildInfo read_build_info(const ReadOnlyFilesystem& fs, const Path& filepath);

    struct AbiEntry
    {
        std::string key;
        std::string value;

        AbiEntry() = default;
        AbiEntry(StringView key, StringView value) : key(key.to_string()), value(value.to_string()) { }

        bool operator<(const AbiEntry& other) const
        {
            return key < other.key || (key == other.key && value < other.value);
        }
    };

    // The parts of AbiInfo which depend only on the port directory and thus can be reused across multiple feature
    // builds
    struct PortDirAbiInfoCacheEntry
    {
        std::vector<AbiEntry> abi_entries;
        std::vector<Path> files;
        std::vector<std::string> hashes;
        Json::Object heuristic_resources;
    };

    using PortDirAbiInfoCache = Cache<Path, PortDirAbiInfoCacheEntry>;

    struct CompilerInfo
    {
        std::string id;
        std::string version;
        std::string hash;
        std::string path;
    };

    struct AbiInfo
    {
        // These should always be known if an AbiInfo exists
        std::unique_ptr<PreBuildInfo> pre_build_info;
        Optional<const Toolset&> toolset;
        // These might not be known if compiler tracking is turned off or the port is --editable
        Optional<const CompilerInfo&> compiler_info;
        Optional<const std::string&> triplet_abi;
        std::string package_abi;
        Optional<Path> abi_tag_file;
        std::vector<Path> relative_port_files;
        std::vector<std::string> relative_port_hashes;
        std::vector<Json::Object> heuristic_resources;
    };

    void compute_all_abis(const VcpkgPaths& paths,
                          ActionPlan& action_plan,
                          const CMakeVars::CMakeVarProvider& var_provider,
                          const StatusParagraphs& status_db);

    void compute_all_abis(const VcpkgPaths& paths,
                          ActionPlan& action_plan,
                          const CMakeVars::CMakeVarProvider& var_provider,
                          const StatusParagraphs& status_db,
                          PortDirAbiInfoCache& port_dir_cache);

    struct EnvCache
    {
        explicit EnvCache(bool compiler_tracking) : m_compiler_tracking(compiler_tracking) { }

        const Environment& get_action_env(const VcpkgPaths& paths,
                                          const PreBuildInfo& pre_build_info,
                                          const Toolset& toolset);
        const std::string& get_triplet_info(const VcpkgPaths& paths,
                                            const PreBuildInfo& pre_build_info,
                                            const Toolset& toolset);
        const CompilerInfo& get_compiler_info(const VcpkgPaths& paths,
                                              const PreBuildInfo& pre_build_info,
                                              const Toolset& toolset);

    private:
        struct TripletMapEntry
        {
            std::string hash;
            Cache<std::string, std::string> triplet_infos;
            Cache<std::string, std::string> triplet_infos_without_compiler;
            Cache<std::string, CompilerInfo> compiler_info;
        };
        Cache<Path, TripletMapEntry> m_triplet_cache;
        Cache<Path, std::string> m_toolchain_cache;

        const TripletMapEntry& get_triplet_cache(const ReadOnlyFilesystem& fs, const Path& p) const;

#if defined(_WIN32)
        struct EnvMapEntry
        {
            std::unordered_map<std::string, std::string> env_map;
            Cache<vcpkg::Command, Environment, CommandLess> cmd_cache;
        };

        Cache<std::vector<std::string>, EnvMapEntry> envs;
#endif

        bool m_compiler_tracking;
    };
} // namespace vcpkg
