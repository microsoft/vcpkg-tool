#pragma once

#include <vcpkg/fwd/cmakevars.h>
#include <vcpkg/fwd/dependencies.h>
#include <vcpkg/fwd/portfileprovider.h>

#include <vcpkg/base/files.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/optional.h>
#include <vcpkg/base/stringview.h>
#include <vcpkg/base/system.process.h>

#include <vcpkg/commands.integrate.h>
#include <vcpkg/packagespec.h>
#include <vcpkg/statusparagraphs.h>
#include <vcpkg/triplet.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

#include <array>
#include <map>
#include <set>
#include <vector>

namespace vcpkg
{
    struct BinaryCache;
    struct Environment;

    DECLARE_MESSAGE(ElapsedForPackage, (msg::spec, msg::elapsed), "", "Elapsed time to handle {spec}: {elapsed}");

    enum class BuildResult
    {
        SUCCEEDED,
        BUILD_FAILED,
        POST_BUILD_CHECKS_FAILED,
        FILE_CONFLICTS,
        CASCADED_DUE_TO_MISSING_DEPENDENCIES,
        EXCLUDED,
        CACHE_MISSING,
        DOWNLOADED,
        REMOVED
    };

    struct IBuildLogsRecorder
    {
        virtual void record_build_result(const VcpkgPaths& paths,
                                         const PackageSpec& spec,
                                         BuildResult result) const = 0;
    };

    const IBuildLogsRecorder& null_build_logs_recorder() noexcept;

    namespace Build
    {
        int perform_ex(const VcpkgCmdArguments& args,
                       const FullPackageSpec& full_spec,
                       Triplet host_triplet,
                       const PathsPortFileProvider& provider,
                       BinaryCache& binary_cache,
                       const IBuildLogsRecorder& build_logs_recorder,
                       const VcpkgPaths& paths);
        void perform_and_exit_ex(const VcpkgCmdArguments& args,
                                 const FullPackageSpec& full_spec,
                                 Triplet host_triplet,
                                 const PathsPortFileProvider& provider,
                                 BinaryCache& binary_cache,
                                 const IBuildLogsRecorder& build_logs_recorder,
                                 const VcpkgPaths& paths);

        int perform(const VcpkgCmdArguments& args,
                    const VcpkgPaths& paths,
                    Triplet default_triplet,
                    Triplet host_triplet);
        void perform_and_exit(const VcpkgCmdArguments& args,
                              const VcpkgPaths& paths,
                              Triplet default_triplet,
                              Triplet host_triplet);
    } // namespace vcpkg::Build

    enum class UseHeadVersion
    {
        NO = 0,
        YES
    };

    enum class AllowDownloads
    {
        NO = 0,
        YES
    };

    enum class OnlyDownloads
    {
        NO = 0,
        YES
    };

    enum class CleanBuildtrees
    {
        NO = 0,
        YES
    };

    enum class CleanPackages
    {
        NO = 0,
        YES
    };

    enum class CleanDownloads
    {
        NO = 0,
        YES
    };

    enum class ConfigurationType
    {
        DEBUG,
        RELEASE,
    };

    enum class DownloadTool
    {
        BUILT_IN,
        ARIA2,
    };
    const std::string& to_string(DownloadTool tool);
    enum class PurgeDecompressFailure
    {
        NO = 0,
        YES
    };

    enum class Editable
    {
        NO = 0,
        YES
    };

    enum class BackcompatFeatures
    {
        ALLOW = 0,
        PROHIBIT
    };

    enum class BuildMissing
    {
        NO = 0,
        YES
    };

    enum class PrintUsage
    {
        YES = 0,
        NO
    };

    struct BuildPackageOptions
    {
        BuildMissing build_missing;
        UseHeadVersion use_head_version;
        AllowDownloads allow_downloads;
        OnlyDownloads only_downloads;
        CleanBuildtrees clean_buildtrees;
        CleanPackages clean_packages;
        CleanDownloads clean_downloads;
        DownloadTool download_tool;
        PurgeDecompressFailure purge_decompress_failure;
        Editable editable;
        BackcompatFeatures backcompat_features;
        PrintUsage print_usage;
    };

    static constexpr BuildPackageOptions default_build_package_options{
        BuildMissing::YES,
        UseHeadVersion::NO,
        AllowDownloads::YES,
        OnlyDownloads::NO,
        CleanBuildtrees::YES,
        CleanPackages::YES,
        CleanDownloads::NO,
        DownloadTool::BUILT_IN,
        PurgeDecompressFailure::YES,
        Editable::NO,
        BackcompatFeatures::ALLOW,
        PrintUsage::YES,
    };

    static constexpr BuildPackageOptions backcompat_prohibiting_package_options{
        BuildMissing::YES,
        UseHeadVersion::NO,
        AllowDownloads::YES,
        OnlyDownloads::NO,
        CleanBuildtrees::YES,
        CleanPackages::YES,
        CleanDownloads::NO,
        DownloadTool::BUILT_IN,
        PurgeDecompressFailure::YES,
        Editable::NO,
        BackcompatFeatures::PROHIBIT,
        PrintUsage::YES,
    };

    struct BuildResultCounts
    {
        unsigned int succeeded = 0;
        unsigned int build_failed = 0;
        unsigned int post_build_checks_failed = 0;
        unsigned int file_conflicts = 0;
        unsigned int cascaded_due_to_missing_dependencies = 0;
        unsigned int excluded = 0;
        unsigned int cache_missing = 0;
        unsigned int downloaded = 0;
        unsigned int removed = 0;

        constexpr void increment(const BuildResult build_result) noexcept
        {
            switch (build_result)
            {
                case BuildResult::SUCCEEDED: ++succeeded; return;
                case BuildResult::BUILD_FAILED: ++build_failed; return;
                case BuildResult::POST_BUILD_CHECKS_FAILED: ++post_build_checks_failed; return;
                case BuildResult::FILE_CONFLICTS: ++file_conflicts; return;
                case BuildResult::CASCADED_DUE_TO_MISSING_DEPENDENCIES: ++cascaded_due_to_missing_dependencies; return;
                case BuildResult::EXCLUDED: ++excluded; return;
                case BuildResult::CACHE_MISSING: ++cache_missing; return;
                case BuildResult::DOWNLOADED: ++downloaded; return;
                case BuildResult::REMOVED: ++removed; return;
                default: Checks::unreachable(VCPKG_LINE_INFO);
            }
        }

        void println(const Triplet& triplet) const;
    };

    StringLiteral to_string_locale_invariant(const BuildResult build_result);
    LocalizedString to_string(const BuildResult build_result);
    LocalizedString create_user_troubleshooting_message(const InstallPlanAction& action, const VcpkgPaths& paths);
    inline void print_user_troubleshooting_message(const InstallPlanAction& action,
                                                   const VcpkgPaths& paths,
                                                   Optional<Path>&& issue_body)
    {
        msg::println_error(create_user_troubleshooting_message(action, paths));
        if (issue_body)
        {
            msg::println(
                Color::warning, msgBuildTroubleshootingMessage4, msg::path = issue_body.value_or_exit(VCPKG_LINE_INFO));
        }
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
        std::string target_architecture;
        std::string cmake_system_name;
        std::string cmake_system_version;
        Optional<std::string> platform_toolset;
        Optional<std::string> platform_toolset_version;
        Optional<Path> visual_studio_path;
        Optional<std::string> external_toolchain_file;
        Optional<ConfigurationType> build_type;
        Optional<std::string> public_abi_override;
        std::vector<std::string> passthrough_env_vars;
        std::vector<std::string> passthrough_env_vars_tracked;

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
        ExtendedBuildResult(BuildResult code, std::vector<FeatureSpec>&& unmet_deps);
        ExtendedBuildResult(BuildResult code, std::unique_ptr<BinaryControlFile>&& bcf);

        BuildResult code;
        std::vector<FeatureSpec> unmet_dependencies;
        std::unique_ptr<BinaryControlFile> binary_control_file;
        Optional<vcpkg::Path> stdoutlog;
        std::vector<std::string> error_logs;
    };

    LocalizedString create_error_message(const ExtendedBuildResult& build_result, const PackageSpec& spec);

    std::string create_github_issue(const VcpkgCmdArguments& args,
                                    const ExtendedBuildResult& build_result,
                                    const VcpkgPaths& paths,
                                    const InstallPlanAction& action);

    ExtendedBuildResult build_package(const VcpkgCmdArguments& args,
                                      const VcpkgPaths& paths,
                                      const InstallPlanAction& config,
                                      BinaryCache& binary_cache,
                                      const IBuildLogsRecorder& build_logs_recorder,
                                      const StatusParagraphs& status_db);

    enum class BuildPolicy
    {
        EMPTY_PACKAGE,
        DLLS_WITHOUT_LIBS,
        DLLS_WITHOUT_EXPORTS,
        DLLS_IN_STATIC_LIBRARY,
        MISMATCHED_NUMBER_OF_BINARIES,
        ONLY_RELEASE_CRT,
        EMPTY_INCLUDE_FOLDER,
        ALLOW_OBSOLETE_MSVCRT,
        ALLOW_RESTRICTED_HEADERS,
        SKIP_DUMPBIN_CHECKS,
        SKIP_ARCHITECTURE_CHECK,
        CMAKE_HELPER_PORT,
        // Must be last
        COUNT,
    };

    // could be constexpr, but we want to generate this and that's not constexpr in C++14
    extern const std::array<BuildPolicy, size_t(BuildPolicy::COUNT)> ALL_POLICIES;

    const std::string& to_string(BuildPolicy policy);
    ZStringView to_cmake_variable(BuildPolicy policy);

    struct BuildPolicies
    {
        BuildPolicies() = default;
        BuildPolicies(std::unordered_map<BuildPolicy, bool>&& map) : m_policies(std::move(map)) { }

        bool is_enabled(BuildPolicy policy) const
        {
            const auto it = m_policies.find(policy);
            if (it != m_policies.cend()) return it->second;
            return false;
        }

    private:
        std::unordered_map<BuildPolicy, bool> m_policies;
    };

    enum class LinkageType : char
    {
        DYNAMIC,
        STATIC,
    };

    Optional<LinkageType> to_linkage_type(StringView str);

    struct BuildInfo
    {
        LinkageType crt_linkage = LinkageType::DYNAMIC;
        LinkageType library_linkage = LinkageType::DYNAMIC;

        Optional<std::string> version;

        BuildPolicies policies;
    };

    BuildInfo read_build_info(const Filesystem& fs, const Path& filepath);

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

    struct CompilerInfo
    {
        std::string id;
        std::string version;
        std::string hash;
    };

    struct AbiInfo
    {
        std::unique_ptr<PreBuildInfo> pre_build_info;
        Optional<const Toolset&> toolset;
        Optional<const std::string&> triplet_abi;
        std::string package_abi;
        Optional<Path> abi_tag_file;
        Optional<const CompilerInfo&> compiler_info;
        std::vector<Path> relative_port_files;
        std::vector<std::string> relative_port_hashes;
        std::vector<Json::Value> heuristic_resources;
    };

    void compute_all_abis(const VcpkgPaths& paths,
                          ActionPlan& action_plan,
                          const CMakeVars::CMakeVarProvider& var_provider,
                          const StatusParagraphs& status_db);

    struct EnvCache
    {
        explicit EnvCache(bool compiler_tracking) : m_compiler_tracking(compiler_tracking) { }

        const Environment& get_action_env(const VcpkgPaths& paths, const AbiInfo& abi_info);
        const std::string& get_triplet_info(const VcpkgPaths& paths, const AbiInfo& abi_info);
        const CompilerInfo& get_compiler_info(const VcpkgPaths& paths, const AbiInfo& abi_info);

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

        const TripletMapEntry& get_triplet_cache(const Filesystem& fs, const Path& p);

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

    struct BuildCommand : Commands::TripletCommand
    {
        virtual void perform_and_exit(const VcpkgCmdArguments& args,
                                      const VcpkgPaths& paths,
                                      Triplet default_triplet,
                                      Triplet host_triplet) const override;
    };
} // namespace vcpkg
