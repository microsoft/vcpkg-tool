#pragma once

#include <vcpkg/base/fwd/message_sinks.h>

#include <vcpkg/fwd/binarycaching.h>
#include <vcpkg/fwd/dependencies.h>
#include <vcpkg/fwd/tools.h>
#include <vcpkg/fwd/vcpkgpaths.h>

#include <vcpkg/base/batch-quere.h>
#include <vcpkg/base/downloads.h>
#include <vcpkg/base/expected.h>
#include <vcpkg/base/message_sinks.h>
#include <vcpkg/base/path.h>

#include <vcpkg/archives.h>
#include <vcpkg/packagespec.h>
#include <vcpkg/sourceparagraph.h>

#include <chrono>
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
        bool should_attempt_precheck(const IReadBinaryProvider* sender) const noexcept;
        bool should_attempt_restore(const IReadBinaryProvider* sender) const noexcept;

        bool is_unavailable(const IReadBinaryProvider* sender) const noexcept;
        const IReadBinaryProvider* get_available_provider() const noexcept;
        bool is_restored() const noexcept;

        void mark_unavailable(const IReadBinaryProvider* sender);
        void mark_available(const IReadBinaryProvider* sender) noexcept;
        void mark_restored() noexcept;

    private:
        CacheStatusState m_status = CacheStatusState::unknown;

        // The set of providers who know they do not have the associated cache entry.
        // Flat vector set because N is tiny.
        std::vector<const IReadBinaryProvider*> m_known_unavailable_providers;

        // The provider who affirmatively has the associated cache entry.
        const IReadBinaryProvider* m_available_provider = nullptr; // meaningful iff m_status == available
    };

    struct BinaryPackageReadInfo
    {
        explicit BinaryPackageReadInfo(const InstallPlanAction& action);
        std::string package_abi;
        PackageSpec spec;
        std::string raw_version;
        Path package_dir;
    };

    struct BinaryPackageWriteInfo : BinaryPackageReadInfo
    {
        using BinaryPackageReadInfo::BinaryPackageReadInfo;

        // Filled if BinaryCache has a provider that returns true for needs_nuspec_data()
        Optional<std::string> nuspec;
        // Filled if BinaryCache has a provider that returns true for needs_zip_file()
        // Note: this can be empty if an error occurred while compressing.
        Optional<Path> zip_path;
    };

    struct IWriteBinaryProvider
    {
        virtual ~IWriteBinaryProvider() = default;

        /// Called upon a successful build of `action` to store those contents in the binary cache.
        /// returns the number of successful uploads
        virtual size_t push_success(const BinaryPackageWriteInfo& request, MessageSink& msg_sink) = 0;

        virtual bool needs_nuspec_data() const = 0;
        virtual bool needs_zip_file() const = 0;
    };

    struct IReadBinaryProvider
    {
        virtual ~IReadBinaryProvider() = default;

        /// Gives the IBinaryProvider an opportunity to batch any downloading or server communication for executing
        /// `actions`.
        ///
        /// IBinaryProvider should set out_status[i] to RestoreResult::restored for each fetched package.
        ///
        /// Prerequisites: actions[i].package_abi(), out_status.size() == actions.size()
        virtual void fetch(View<const InstallPlanAction*> actions, Span<RestoreResult> out_status) const = 0;

        /// Checks whether the `actions` are present in the cache, without restoring them.
        ///
        /// Used by CI to determine missing packages. For each `i`, out_status[i] should be set to
        /// CacheAvailability::available or CacheAvailability::unavailable
        ///
        /// Prerequisites: actions[i].package_abi(), out_status.size() == actions.size()
        virtual void precheck(View<const InstallPlanAction*> actions, Span<CacheAvailability> out_status) const = 0;

        virtual LocalizedString restored_message(size_t count,
                                                 std::chrono::high_resolution_clock::duration elapsed) const = 0;
    };

    struct UrlTemplate
    {
        std::string url_template;
        std::vector<std::string> headers;

        LocalizedString valid() const;
        std::string instantiate_variables(const BinaryPackageReadInfo& info) const;
    };

    struct NuGetRepoInfo
    {
        std::string repo;
        std::string branch;
        std::string commit;
    };

    struct BinaryConfigParserState
    {
        bool nuget_interactive = false;
        std::set<StringLiteral> binary_cache_providers;

        std::string nugettimeout = "100";

        std::vector<Path> archives_to_read;
        std::vector<Path> archives_to_write;

        std::vector<UrlTemplate> url_templates_to_get;
        std::vector<UrlTemplate> url_templates_to_put;

        std::vector<std::string> gcs_read_prefixes;
        std::vector<std::string> gcs_write_prefixes;

        std::vector<std::string> aws_read_prefixes;
        std::vector<std::string> aws_write_prefixes;
        bool aws_no_sign_request = false;

        std::vector<std::string> cos_read_prefixes;
        std::vector<std::string> cos_write_prefixes;

        bool gha_write = false;
        bool gha_read = false;

        std::vector<std::string> sources_to_read;
        std::vector<std::string> sources_to_write;

        std::vector<Path> configs_to_read;
        std::vector<Path> configs_to_write;

        std::vector<std::string> secrets;

        // These are filled in after construction by reading from args and environment
        std::string nuget_prefix;
        bool use_nuget_cache = false;
        NuGetRepoInfo nuget_repo_info;

        void clear();
    };

    ExpectedL<BinaryConfigParserState> parse_binary_provider_configs(const std::string& env_string,
                                                                     View<std::string> args);

    struct BinaryProviders
    {
        std::vector<std::unique_ptr<IReadBinaryProvider>> read;
        std::vector<std::unique_ptr<IWriteBinaryProvider>> write;
        std::string nuget_prefix;
        NuGetRepoInfo nuget_repo;
    };

    struct ReadOnlyBinaryCache
    {
        ReadOnlyBinaryCache() = default;
        ReadOnlyBinaryCache(BinaryProviders&& providers);

        /// Gives the IBinaryProvider an opportunity to batch any downloading or server communication for
        /// executing `actions`.
        void fetch(View<InstallPlanAction> actions);

        bool is_restored(const InstallPlanAction& ipa) const;

        /// Checks whether the `actions` are present in the cache, without restoring them. Used by CI to determine
        /// missing packages.
        /// Returns a vector where each index corresponds to the matching index in `actions`.
        std::vector<CacheAvailability> precheck(View<InstallPlanAction> actions);

    protected:
        BinaryProviders m_config;

        std::unordered_map<std::string, CacheStatus> m_status;
    };

    struct BinaryCache : ReadOnlyBinaryCache
    {
        static ExpectedL<std::unique_ptr<BinaryCache>> make(const VcpkgCmdArguments& args,
                                                            const VcpkgPaths& paths,
                                                            MessageSink& sink);

        BinaryCache(Filesystem& fs);
        BinaryCache(const BinaryCache&) = delete;
        BinaryCache(BinaryCache&&) = delete;
        ~BinaryCache();

        /// Called upon a successful build of `action` to store those contents in the binary cache.
        void push_success(const InstallPlanAction& action);

        void print_push_success_messages();
        void wait_for_async_complete();

    private:
        BinaryCache(BinaryProviders&& providers, Filesystem& fs);

        Filesystem& m_fs;
        Optional<ZipTool> m_zip_tool;
        bool m_needs_nuspec_data = false;
        bool m_needs_zip_file = false;

        struct ActionToPush
        {
            BinaryPackageWriteInfo request;
            bool clean_after_push = false;
        };

        void push_thread_main();

        BGMessageSink m_bg_msg_sink;
        BGThreadBatchQueue<ActionToPush> m_actions_to_push;
        std::atomic_int m_remaining_packages_to_push = 0;
        std::thread m_push_thread;
    };

    ExpectedL<DownloadManagerConfig> parse_download_configuration(const Optional<std::string>& arg);

    std::string generate_nuget_packages_config(const ActionPlan& action, StringView prefix);

    LocalizedString format_help_topic_asset_caching();
    LocalizedString format_help_topic_binary_caching();
}
