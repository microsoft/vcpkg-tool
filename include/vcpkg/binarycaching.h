#pragma once

// #include <vcpkg/base/fwd/optional.h>

#include <vcpkg/fwd/binarycaching.h>
#include <vcpkg/fwd/dependencies.h>
#include <vcpkg/fwd/tools.h>
#include <vcpkg/fwd/vcpkgpaths.h>

#include <vcpkg/base/expected.h>
#include <vcpkg/base/files.h>

#include <vcpkg/packagespec.h>
#include <vcpkg/sourceparagraph.h>

#include <condition_variable>
#include <iterator>
#include <queue>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace vcpkg
{
    struct CacheStatus
    {
        bool should_attempt_precheck(const IBinaryProvider* sender) const noexcept;
        bool should_attempt_restore(const IBinaryProvider* sender) const noexcept;

        bool is_unavailable(size_t total_providers) const noexcept;
        const IBinaryProvider* get_available_provider() const noexcept;
        bool is_restored() const noexcept;

        void mark_unavailable(const IBinaryProvider* sender);
        void mark_available(const IBinaryProvider* sender) noexcept;
        void mark_restored() noexcept;

    private:
        CacheStatusState m_status = CacheStatusState::unknown;

        // The set of providers who know they do not have the associated cache entry.
        // Flat vector set because N is tiny.
        std::vector<const IBinaryProvider*> m_known_unavailable_providers; // meaningful iff m_status == unknown

        // The provider who affirmatively has the associated cache entry.
        const IBinaryProvider* m_available_provider = nullptr; // meaningful iff m_status == available
    };

    struct BinaryPackageInformation
    {
        explicit BinaryPackageInformation(const InstallPlanAction& action);
        std::string package_abi;
        // The following fields are only needed for the nuget binary cache
        PackageSpec spec;
        SourceControlFile& source_control_file;
        std::string& raw_version;
        std::string compiler_id;
        std::string compiler_version;
        std::string triplet_abi;
        InternalFeatureSet feature_list;
        std::vector<PackageSpec> package_dependencies;
    };

    struct IObjectProvider
    {
        enum class Access : uint8_t
        {
            Read = 0b01,
            Write = 0b10,
            ReadWrite = 0b11,
        };
        Access access;

        bool supports_write() const { return static_cast<uint8_t>(access) & static_cast<uint8_t>(Access::Write); }
        bool supports_read() const { return static_cast<uint8_t>(access) & static_cast<uint8_t>(Access::Read); }

        IObjectProvider(Access access) : access(access) { }
        virtual ~IObjectProvider() = default;

        virtual void download(Optional<const ToolCache&> tool_cache,
                              View<StringView> objects,
                              const Path& target_dir) const = 0;

        virtual void upload(Optional<const ToolCache&> tool_cache,
                            StringView object_id,
                            const Path& object_file,
                            MessageSink& msg_sink) = 0;

        virtual void check_availability(Optional<const ToolCache&> tool_cache,
                                        View<StringView> objects,
                                        Span<bool> cache_status) const = 0;
    };

    struct IBinaryProvider
    {
        virtual ~IBinaryProvider() = default;

        /// Attempts to restore the package referenced by `action` into the packages directory.
        /// Prerequisite: action has a package_abi()
        virtual RestoreResult try_restore(const InstallPlanAction& action) const = 0;

        /// Called upon a successful build of `action` to store those contents in the binary cache.
        /// Prerequisite: action has a package_abi()
        virtual void push_success(const BinaryPackageInformation& info,
                                  const Path& packages_dir,
                                  MessageSink& msg_sink) = 0;

        /// Gives the IBinaryProvider an opportunity to batch any downloading or server communication for
        /// executing `actions`.
        /// `cache_status` is a vector with the same number of entries of actions, where each index corresponds
        /// to the action at the same index in `actions`. The provider must mark the cache status as appropriate.
        /// Note: `actions` *might not* have package ABIs (for example, a --head package)!
        /// Prerequisite: if `actions[i]` has no package ABI, `cache_status[i]` is nullptr.
        virtual void prefetch(View<InstallPlanAction> actions, View<CacheStatus*> cache_status) const = 0;

        /// Checks whether the `actions` are present in the cache, without restoring them. Used by CI to determine
        /// missing packages.
        /// `cache_status` is a view with the same number of entries of actions, where each index corresponds
        /// to the action at the same index in `actions`. The provider must mark the cache status as appropriate.
        /// Prerequisite: `actions` have package ABIs.
        virtual void precheck(View<InstallPlanAction> actions, View<CacheStatus*> cache_status) const = 0;
    };

    struct UrlTemplate
    {
        std::string url_template;
        std::vector<std::string> headers_for_put;
        std::vector<std::string> headers_for_get;

        LocalizedString valid() const;
        std::string instantiate_variable(StringView sha) const;

    protected:
        LocalizedString valid(View<StringLiteral> valid_keys) const;
    };

    struct ExtendedUrlTemplate : private UrlTemplate
    {
        using UrlTemplate::headers_for_get;
        using UrlTemplate::headers_for_put;
        using UrlTemplate::url_template;
        ExtendedUrlTemplate(UrlTemplate&& t) : UrlTemplate(std::move(t)) { }
        LocalizedString valid() const;
        std::string instantiate_variables(const BinaryPackageInformation& info) const;
    };

    struct ObjectCacheConfig
    {
        ObjectCacheConfig() = default;
        ObjectCacheConfig(const ObjectCacheConfig&) = delete;
        ObjectCacheConfig& operator=(const ObjectCacheConfig&) = delete;
        ObjectCacheConfig(ObjectCacheConfig&&) = default;
        ObjectCacheConfig& operator=(ObjectCacheConfig&&) = default;
        virtual ~ObjectCacheConfig() = default;
        std::vector<std::unique_ptr<IObjectProvider>> object_providers;
        std::vector<std::string> secrets;
        virtual void clear();
    };

    struct BinaryConfigParserState : ObjectCacheConfig
    {
        bool nuget_interactive = false;
        std::string nugettimeout = "100";

        std::vector<ExtendedUrlTemplate> url_templates_to_get;
        std::vector<ExtendedUrlTemplate> url_templates_to_put;

        std::vector<std::string> sources_to_read;
        std::vector<std::string> sources_to_write;

        std::vector<Path> configs_to_read;
        std::vector<Path> configs_to_write;

        void clear() override;
    };

    struct DownloadManagerConfig : public ObjectCacheConfig
    {
        bool block_origin = false;
        Optional<std::string> script;
        void clear() override;
    };

    ExpectedS<BinaryConfigParserState> create_binary_providers_from_configs_pure(Filesystem& fs,
                                                                                 const std::string& env_string,
                                                                                 View<std::string> args);
    ExpectedS<std::vector<std::unique_ptr<IBinaryProvider>>> create_binary_providers_from_configs(
        const VcpkgPaths& paths, View<std::string> args);

    struct BinaryCache
    {
        static BinaryCache* current_instance;
        static void wait_for_async_complete();
        BinaryCache(Filesystem& filesystem);
        explicit BinaryCache(const VcpkgCmdArguments& args, const VcpkgPaths& paths);

        ~BinaryCache();

        void install_providers(std::vector<std::unique_ptr<IBinaryProvider>>&& providers);
        void install_providers_for(const VcpkgCmdArguments& args, const VcpkgPaths& paths);

        /// Attempts to restore the package referenced by `action` into the packages directory.
        RestoreResult try_restore(const InstallPlanAction& action);

        /// Called upon a successful build of `action` to store those contents in the binary cache.
        void push_success(const InstallPlanAction& action, Path package_dir);

        /// Gives the IBinaryProvider an opportunity to batch any downloading or server communication for
        /// executing `actions`.
        void prefetch(View<InstallPlanAction> actions);

        /// Checks whether the `actions` are present in the cache, without restoring them. Used by CI to determine
        /// missing packages.
        /// Returns a vector where each index corresponds to the matching index in `actions`.
        std::vector<CacheAvailability> precheck(View<InstallPlanAction> actions);

    private:
        struct ActionToPush
        {
            BinaryPackageInformation info;
            bool clean_after_push;
            Path packages_dir;
        };
        void push_thread_main();

        std::unordered_map<std::string, CacheStatus> m_status;
        std::vector<std::unique_ptr<IBinaryProvider>> m_providers;
        std::condition_variable actions_to_push_notifier;
        std::mutex actions_to_push_mutex;
        std::queue<ActionToPush> actions_to_push;
        std::thread push_thread;
        std::atomic_bool end_push_thread;
        Filesystem& filesystem;
    };

    ExpectedS<DownloadManagerConfig> parse_download_configuration(Filesystem& fs, const Optional<std::string>& arg);

    std::string generate_nuget_packages_config(const ActionPlan& action);

    LocalizedString format_help_topic_asset_caching();
    LocalizedString format_help_topic_binary_caching();
}
