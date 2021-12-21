#pragma once

#include <vcpkg/fwd/dependencies.h>
#include <vcpkg/fwd/vcpkgpaths.h>

#include <vcpkg/base/downloads.h>
#include <vcpkg/base/expected.h>
#include <vcpkg/base/files.h>

#include <vcpkg/packagespec.h>

#include <iterator>
#include <string>
#include <unordered_map>
#include <vector>

namespace vcpkg
{
    enum class RestoreResult
    {
        unavailable,
        restored,
    };

    enum class CacheAvailability
    {
        unavailable,
        available,
    };

    enum class CacheStatusState
    {
        unknown,   // the cache status of the indicated package ABI is unknown
        available, // the cache is known to contain the package ABI, but it has not been restored
        restored,  // the cache contains the ABI and it has been restored to the packages tree
    };

    struct IBinaryProvider;

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
        virtual RestoreResult try_restore(const VcpkgPaths& paths,
                                          const Dependencies::InstallPlanAction& action) const = 0;

        /// Called upon a successful build of `action` to store those contents in the binary cache.
        /// Prerequisite: action has a package_abi()
        virtual void push_success(const VcpkgPaths& paths, const Dependencies::InstallPlanAction& action) const = 0;

        /// Gives the IBinaryProvider an opportunity to batch any downloading or server communication for
        /// executing `actions`.
        /// `cache_status` is a vector with the same number of entries of actions, where each index corresponds
        /// to the action at the same index in `actions`. The provider must mark the cache status as appropriate.
        virtual void prefetch(const VcpkgPaths& paths,
                              View<Dependencies::InstallPlanAction> actions,
                              View<CacheStatus*> cache_status) const = 0;

        /// Checks whether the `actions` are present in the cache, without restoring them. Used by CI to determine
        /// missing packages.
        /// `cache_status` is a view with the same number of entries of actions, where each index corresponds
        /// to the action at the same index in `actions`. The provider must mark the cache status as appropriate.
        virtual void precheck(const VcpkgPaths& paths,
                              View<Dependencies::InstallPlanAction> actions,
                              View<CacheStatus*> cache_status) const = 0;
    };

    ExpectedS<std::vector<std::unique_ptr<IBinaryProvider>>> create_binary_providers_from_configs(
        View<std::string> args);
    ExpectedS<std::vector<std::unique_ptr<IBinaryProvider>>> create_binary_providers_from_configs_pure(
        const std::string& env_string, View<std::string> args);

    struct BinaryCache
    {
        BinaryCache() = default;
        explicit BinaryCache(const VcpkgCmdArguments& args);

        void install_providers(std::vector<std::unique_ptr<IBinaryProvider>>&& providers);
        void install_providers_for(const VcpkgCmdArguments& args);

        /// Attempts to restore the package referenced by `action` into the packages directory.
        RestoreResult try_restore(const VcpkgPaths& paths, const Dependencies::InstallPlanAction& action);

        /// Called upon a successful build of `action` to store those contents in the binary cache.
        void push_success(const VcpkgPaths& paths, const Dependencies::InstallPlanAction& action);

        /// Gives the IBinaryProvider an opportunity to batch any downloading or server communication for
        /// executing `actions`.
        void prefetch(const VcpkgPaths& paths, View<Dependencies::InstallPlanAction> actions);

        /// Checks whether the `actions` are present in the cache, without restoring them. Used by CI to determine
        /// missing packages.
        /// Returns a vector where each index corresponds to the matching index in `actions`.
        std::vector<CacheAvailability> precheck(const VcpkgPaths& paths, View<Dependencies::InstallPlanAction> actions);

    private:
        std::unordered_map<std::string, CacheStatus> m_status;
        std::vector<std::unique_ptr<IBinaryProvider>> m_providers;
    };

    ExpectedS<DownloadManagerConfig> parse_download_configuration(const Optional<std::string>& arg);

    std::string generate_nuget_packages_config(const Dependencies::ActionPlan& action);

    void help_topic_asset_caching(const VcpkgPaths& paths);
    void help_topic_binary_caching(const VcpkgPaths& paths);
}
