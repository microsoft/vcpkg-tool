#pragma once

#include <vcpkg/base/fwd/message_sinks.h>

#include <vcpkg/fwd/binarycaching.h>
#include <vcpkg/fwd/build.h>
#include <vcpkg/fwd/dependencies.h>
#include <vcpkg/fwd/tools.h>
#include <vcpkg/fwd/vcpkgpaths.h>

#include <vcpkg/base/downloads.h>
#include <vcpkg/base/expected.h>
#include <vcpkg/base/path.h>

#include <vcpkg/archives.h>
#include <vcpkg/packagespec.h>
#include <vcpkg/versions.h>

#include <chrono>
#include <iterator>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace vcpkg
{
    /// Unique identifier for a provider
    using ProviderId = size_t;

    struct CacheStatus
    {
        using ReaderProviders = std::vector<const IReadBinaryProvider*>;

        bool should_attempt_precheck(const IReadBinaryProvider* sender) const noexcept;
        bool should_attempt_restore(const IReadBinaryProvider* sender) const noexcept;
        bool should_attempt_write_back(ProviderId provider_id) const noexcept;

        bool is_unavailable(const IReadBinaryProvider* sender) const noexcept;
        const IReadBinaryProvider* get_available_provider() const noexcept;
        ReaderProviders& get_unavailable_providers() noexcept;
        bool is_restored() const noexcept;

        void mark_unavailable(const IReadBinaryProvider* sender);
        void mark_available(const IReadBinaryProvider* sender) noexcept;
        void mark_restored() noexcept;
        void mark_written_back(ProviderId provider_id) noexcept;

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
        Version version;
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
        virtual size_t push_success(const BinaryPackageWriteInfo& request,
                                    MessageSink& msg_sink,
                                    CacheStatus& cache_status) = 0;

        virtual bool needs_nuspec_data() const = 0;
        virtual bool needs_zip_file() const = 0;
        virtual std::vector<ProviderId> get_provider_ids() const = 0;
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

        /// Flag to indicate if the provider supports an efficient check to see if a certain set of packages are
        /// available. If not, packages will assumed to be present & will always be fetched.
        virtual bool can_precheck() const = 0;

        /// Checks whether the `actions` are present in the cache, without restoring them.
        ///
        /// Used by CI to determine missing packages. For each `i`, out_status[i] should be set to
        /// CacheAvailability::available or CacheAvailability::unavailable
        ///
        /// Prerequisites: actions[i].package_abi(), out_status.size() == actions.size()
        virtual void precheck(View<const InstallPlanAction*> actions, Span<CacheAvailability> out_status) const = 0;

        virtual LocalizedString restored_message(size_t count,
                                                 std::chrono::high_resolution_clock::duration elapsed) const = 0;

        /// Unique identifier for this provider.
        ///
        /// Used by the cache to exclude cache providers during the write-back phase.
        virtual ProviderId id() const = 0;
    };

    struct UrlTemplate
    {
        std::string url_template;
        std::vector<std::string> headers;

        LocalizedString valid() const;
        std::string instantiate_variables(const BinaryPackageReadInfo& info) const;
    };

    struct GithubActionsInfo
    {
    };

    struct NuGetRepoInfo
    {
        std::string repo;
        std::string branch;
        std::string commit;
    };

    enum class CacheType
    {
        Read,
        Write,
        ReadWrite
    };

    template<typename T>
    struct CacheProvider
    {
        ProviderId id;
        T source;
        CacheType cache_type;

        [[nodiscard]] constexpr bool is_read() const noexcept
        {
            return cache_type == CacheType::Read || cache_type == CacheType::ReadWrite;
        }

        [[nodiscard]] constexpr bool is_write() const noexcept
        {
            return cache_type == CacheType::Write || cache_type == CacheType::ReadWrite;
        }
    };

    template<typename T>
    using ProviderList = std::vector<CacheProvider<T>>;

    struct BinaryConfigParserState
    {
        ProviderId provider_count = 0;
        bool nuget_interactive = false;
        std::set<StringLiteral> binary_cache_providers;

        std::string nugettimeout = "100";

        ProviderList<Path> archives;
        ProviderList<UrlTemplate> url_templates;
        ProviderList<std::string> gcs_prefixes;
        ProviderList<std::string> aws_prefixes;
        bool aws_no_sign_request = false;
        ProviderList<std::string> cos_prefixes;
        Optional<CacheProvider<GithubActionsInfo>> gha;
        ProviderList<std::string> sources;
        ProviderList<Path> configs;

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

    [[nodiscard]] bool HasWriteOnlyProviders(const BinaryProviders& providers);

    struct ReadOnlyBinaryCache
    {
        ReadOnlyBinaryCache() = default;
        ReadOnlyBinaryCache(BinaryProviders&& providers);

        /// Gives the IBinaryProvider an opportunity to batch any downloading or server communication for
        /// executing `actions`.
        void fetch(View<InstallPlanAction> actions);

        Optional<CacheStatus> cache_status(const InstallPlanAction& ipa) const;

        /// Checks whether the `actions` are present in the cache, without restoring them. Used by CI to determine
        /// missing packages.
        /// Returns a vector where each index corresponds to the matching index in `actions`.
        std::vector<CacheAvailability> precheck(View<InstallPlanAction> actions);

    protected:
        BinaryProviders m_config;

        /// Flag to indicate that at least one provider is write-only. This implies that the write-back phase should
        /// always take place for every Cache item.
        bool m_has_write_only_providers;

        std::unordered_map<std::string, CacheStatus> m_status;
    };

    struct BinaryCache : ReadOnlyBinaryCache
    {
        static ExpectedL<BinaryCache> make(const VcpkgCmdArguments& args, const VcpkgPaths& paths, MessageSink& sink);

        BinaryCache(const Filesystem& fs);
        BinaryCache(const BinaryCache&) = delete;
        BinaryCache(BinaryCache&&) = default;

        /// Called upon a successful build of `action` to store those contents in the binary cache.
        void push_success(CleanPackages clean_packages, const InstallPlanAction& action);

    private:
        BinaryCache(BinaryProviders&& providers, const Filesystem& fs);

        const Filesystem& m_fs;
        Optional<ZipTool> m_zip_tool;
        bool m_needs_nuspec_data = false;
        bool m_needs_zip_file = false;
    };

    ExpectedL<DownloadManagerConfig> parse_download_configuration(const Optional<std::string>& arg);

    std::string generate_nuget_packages_config(const ActionPlan& action, StringView prefix);

    LocalizedString format_help_topic_asset_caching();
    LocalizedString format_help_topic_binary_caching();
}
