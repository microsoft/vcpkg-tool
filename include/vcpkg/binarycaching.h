#pragma once

#include <vcpkg/fwd/binarycaching.h>
#include <vcpkg/fwd/dependencies.h>
#include <vcpkg/fwd/vcpkgpaths.h>

#include <vcpkg/base/downloads.h>
#include <vcpkg/base/expected.h>
#include <vcpkg/base/files.h>

#include <vcpkg/packagespec.h>

#include <iterator>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
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

    struct IBinaryProvider
    {
        virtual ~IBinaryProvider() = default;

        /// Attempts to restore the package referenced by `action` into the packages directory.
        /// Prerequisite: action has a package_abi()
        virtual RestoreResult try_restore(const InstallPlanAction& action) const = 0;

        /// Called upon a successful build of `action` to store those contents in the binary cache.
        /// Prerequisite: action has a package_abi()
        virtual void push_success(const InstallPlanAction& action) const = 0;

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
        std::string instantiate_variables(const InstallPlanAction& action) const;
    };

    struct AwsPrefixConfig
    {
        std::string endpoint_url;
        std::string profile;
    };

    using AwsPrefix = std::pair<std::string, AwsPrefixConfig>;

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

        std::vector<AwsPrefix> aws_read_prefixes;
        std::vector<AwsPrefix> aws_write_prefixes;
        bool aws_no_sign_request = false;

        std::vector<std::string> cos_read_prefixes;
        std::vector<std::string> cos_write_prefixes;

        std::vector<std::string> sources_to_read;
        std::vector<std::string> sources_to_write;

        std::vector<Path> configs_to_read;
        std::vector<Path> configs_to_write;

        std::vector<std::string> secrets;

        void clear();
    };

    ExpectedS<BinaryConfigParserState> create_binary_providers_from_configs_pure(const std::string& env_string,
                                                                                 View<std::string> args);
    ExpectedS<std::vector<std::unique_ptr<IBinaryProvider>>> create_binary_providers_from_configs(
        const VcpkgPaths& paths, View<std::string> args);

    struct BinaryCache
    {
        BinaryCache() = default;
        explicit BinaryCache(const VcpkgCmdArguments& args, const VcpkgPaths& paths);

        void install_providers(std::vector<std::unique_ptr<IBinaryProvider>>&& providers);
        void install_providers_for(const VcpkgCmdArguments& args, const VcpkgPaths& paths);

        /// Attempts to restore the package referenced by `action` into the packages directory.
        RestoreResult try_restore(const InstallPlanAction& action);

        /// Called upon a successful build of `action` to store those contents in the binary cache.
        void push_success(const InstallPlanAction& action);

        /// Gives the IBinaryProvider an opportunity to batch any downloading or server communication for
        /// executing `actions`.
        void prefetch(View<InstallPlanAction> actions);

        /// Checks whether the `actions` are present in the cache, without restoring them. Used by CI to determine
        /// missing packages.
        /// Returns a vector where each index corresponds to the matching index in `actions`.
        std::vector<CacheAvailability> precheck(View<InstallPlanAction> actions);

    private:
        std::unordered_map<std::string, CacheStatus> m_status;
        std::vector<std::unique_ptr<IBinaryProvider>> m_providers;
    };

    ExpectedS<DownloadManagerConfig> parse_download_configuration(const Optional<std::string>& arg);

    std::string generate_nuget_packages_config(const ActionPlan& action);

    LocalizedString format_help_topic_asset_caching();
    LocalizedString format_help_topic_binary_caching();
}
