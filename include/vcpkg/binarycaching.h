#pragma once

#include <vcpkg/base/fwd/message_sinks.h>

#include <vcpkg/fwd/binarycaching.h>
#include <vcpkg/fwd/build.h>
#include <vcpkg/fwd/dependencies.h>
#include <vcpkg/fwd/tools.h>
#include <vcpkg/fwd/vcpkgpaths.h>

#include <vcpkg/base/background-work-queue.h>
#include <vcpkg/base/downloads.h>
#include <vcpkg/base/expected.h>
#include <vcpkg/base/message_sinks.h>
#include <vcpkg/base/path.h>

#include <vcpkg/archives.h>
#include <vcpkg/packagespec.h>
#include <vcpkg/versions.h>

#include <chrono>
#include <iterator>
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
        void mark_unrestored() noexcept;

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
        std::string display_name;
        Version version;
        Path package_dir;
    };

    struct BinaryPackageWriteInfo : BinaryPackageReadInfo
    {
        using BinaryPackageReadInfo::BinaryPackageReadInfo;

        // Filled if BinaryCache has a provider that returns true for needs_nuspec_data()
        Optional<std::string> nuspec;
        // Set to true if there is only one write provider, meaning that one provider can take ownership of the zip file
        bool unique_write_provider = false;
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

    struct AzureUpkgSource
    {
        std::string organization;
        std::string project;
        std::string feed;
    };

    struct AzCopyUrl
    {
        std::string url;
        std::string sas;

        std::string make_object_path(const std::string& abi) const;
        std::string make_container_path() const;
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

        std::vector<UrlTemplate> azblob_templates_to_put;

        std::vector<AzCopyUrl> azcopy_read_templates;
        std::vector<AzCopyUrl> azcopy_write_templates;

        std::vector<std::string> gcs_read_prefixes;
        std::vector<std::string> gcs_write_prefixes;

        std::vector<std::string> aws_read_prefixes;
        std::vector<std::string> aws_write_prefixes;
        bool aws_no_sign_request = false;

        std::vector<std::string> cos_read_prefixes;
        std::vector<std::string> cos_write_prefixes;

        std::vector<AzureUpkgSource> upkg_templates_to_get;
        std::vector<AzureUpkgSource> upkg_templates_to_put;

        std::vector<std::string> sources_to_read;
        std::vector<std::string> sources_to_write;

        std::vector<Path> configs_to_read;
        std::vector<Path> configs_to_write;

        std::vector<std::string> secrets;

        // These are filled in after construction by reading from args and environment
        std::string nuget_prefix;
        bool use_nuget_cache = false;

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
        ReadOnlyBinaryCache(const ReadOnlyBinaryCache&) = delete;
        ReadOnlyBinaryCache& operator=(const ReadOnlyBinaryCache&) = delete;

        /// Gives the IBinaryProvider an opportunity to batch any downloading or server communication for
        /// executing `actions`.
        void fetch(View<InstallPlanAction> actions);

        bool is_restored(const InstallPlanAction& ipa) const;

        void install_read_provider(std::unique_ptr<IReadBinaryProvider>&& provider);

        /// Checks whether the `actions` are present in the cache, without restoring them. Used by CI to determine
        /// missing packages.
        /// Returns a vector where each index corresponds to the matching index in `actions`.
        std::vector<CacheAvailability> precheck(View<const InstallPlanAction*> actions);

        // Informs the binary cache that the packages directory has been reset. Used when the same port-name is built
        // more than once in a single invocation of vcpkg.
        void mark_all_unrestored();

    protected:
        BinaryProviders m_config;

        std::unordered_map<std::string, CacheStatus> m_status;
    };

    struct BinaryCacheSyncState;
    struct BinaryCacheSynchronizer
    {
        using backing_uint_t = std::conditional_t<sizeof(size_t) == 4, uint32_t, uint64_t>;
        using counter_uint_t = std::conditional_t<sizeof(size_t) == 4, uint16_t, uint32_t>;
        static constexpr backing_uint_t SubmissionCompleteBit = static_cast<backing_uint_t>(1)
                                                                << (sizeof(backing_uint_t) * 8 - 1);
        static constexpr backing_uint_t UpperShift = sizeof(counter_uint_t) * 8;
        static constexpr backing_uint_t SubmittedMask =
            static_cast<backing_uint_t>(static_cast<counter_uint_t>(-1) >> 1u);
        static constexpr backing_uint_t CompletedMask = SubmittedMask << UpperShift;
        static constexpr backing_uint_t OneCompleted = static_cast<backing_uint_t>(1) << UpperShift;

        void add_submitted() noexcept;
        BinaryCacheSyncState fetch_add_completed() noexcept;
        counter_uint_t fetch_incomplete_mark_submission_complete() noexcept;

    private:
        // This is morally:
        // struct State {
        //    counter_uint_t jobs_submitted;
        //    bool unused;
        //    counter_uint_t_minus_one_bit jobs_completed;
        //    bool submission_complete;
        // };
        std::atomic<backing_uint_t> m_state = 0;
    };

    struct BinaryCacheSyncState
    {
        BinaryCacheSynchronizer::counter_uint_t jobs_submitted;
        BinaryCacheSynchronizer::counter_uint_t jobs_completed;
        bool submission_complete;
    };

    // compression and upload of binary cache entries happens on a single 'background' thread, `m_push_thread`
    // Thread safety is achieved within the binary cache providers by:
    //   1. Only using one thread in the background for this work.
    //   2. Forming a queue of work for that thread to consume in `m_actions_to_push`, which maintains its own thread
    //   safety
    //   3. Sending any replies from the background thread through `m_bg_msg_sink`
    //   4. Ensuring any supporting data, such as tool exes, is provided before the background thread is started.
    //   5. Ensuring that work is not submitted to the background thread until the corresponding `packages` directory to
    //   upload is no longer being actively written by the foreground thread.
    struct BinaryCache : ReadOnlyBinaryCache
    {
        bool install_providers(const VcpkgCmdArguments& args, const VcpkgPaths& paths, MessageSink& status_sink);

        explicit BinaryCache(const Filesystem& fs);
        BinaryCache(const BinaryCache&) = delete;
        BinaryCache& operator=(const BinaryCache&) = delete;
        ~BinaryCache();
        /// Called upon a successful build of `action` to store those contents in the binary cache.
        void push_success(CleanPackages clean_packages, const InstallPlanAction& action);

        void print_updates();
        void wait_for_async_complete_and_join();

    private:
        struct ActionToPush
        {
            BinaryPackageWriteInfo request;
            CleanPackages clean_after_push;
        };

        ZipTool m_zip_tool;
        bool m_needs_nuspec_data = false;
        bool m_needs_zip_file = false;

        const Filesystem& m_fs;

        BGMessageSink m_bg_msg_sink;
        BackgroundWorkQueue<ActionToPush> m_actions_to_push;
        BinaryCacheSynchronizer m_synchronizer;
        std::thread m_push_thread;

        void push_thread_main();
    };

    ExpectedL<AssetCachingSettings> parse_download_configuration(const Optional<std::string>& arg);

    std::string generate_nuget_packages_config(const ActionPlan& action, StringView prefix);

    LocalizedString format_help_topic_asset_caching();
    LocalizedString format_help_topic_binary_caching();

    std::vector<std::vector<std::string>> batch_command_arguments_with_fixed_length(
        const std::vector<std::string>& entries,
        const std::size_t reserved_len,
        const std::size_t max_len,
        const std::size_t fixed_len,
        const std::size_t separator_len);
}
