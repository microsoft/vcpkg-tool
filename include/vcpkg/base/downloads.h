#pragma once

#include <vcpkg/base/fwd/downloads.h>
#include <vcpkg/base/fwd/files.h>
#include <vcpkg/base/fwd/messages.h>

#include <vcpkg/base/expected.h>
#include <vcpkg/base/json.h>
#include <vcpkg/base/optional.h>
#include <vcpkg/base/span.h>
#include <vcpkg/base/stringview.h>

#include <string>
#include <vector>

namespace vcpkg
{
    struct SanitizedUrl
    {
        SanitizedUrl() = default;
        SanitizedUrl(StringView raw_url, View<std::string> secrets);
        const std::string& to_string() const noexcept { return m_sanitized_url; }

    private:
        std::string m_sanitized_url;
    };

    View<std::string> azure_blob_headers();

    std::vector<int> download_files_no_cache(DiagnosticContext& context,
                                             View<std::pair<std::string, Path>> url_pairs,
                                             View<std::string> headers);

    bool submit_github_dependency_graph_snapshot(DiagnosticContext& context,
                                                 const Optional<std::string>& maybe_github_server_url,
                                                 const std::string& github_token,
                                                 const std::string& github_repository,
                                                 const Json::Object& snapshot);

    // Builds the dependency graph snapshots endpoint used for GitHub submission.
    std::string github_dependency_graph_snapshots_uri(const Optional<std::string>& maybe_github_server_url,
                                                      StringView github_repository);

    std::vector<int> url_heads(DiagnosticContext& context, View<std::string> urls, View<std::string> headers);

    struct AssetCachingSettings
    {
        Optional<std::string> m_read_url_template;
        std::vector<std::string> m_read_headers;
        Optional<std::string> m_write_url_template;
        std::vector<std::string> m_write_headers;
        std::vector<std::string> m_secrets;
        bool m_block_origin = false;
        Optional<std::string> m_script;
    };

    // Handles downloading and uploading to a content addressable mirror
    bool download_file_asset_cached(DiagnosticContext& context,
                                    MessageSink& machine_readable_progress,
                                    const AssetCachingSettings& asset_cache_settings,
                                    const Filesystem& fs,
                                    const std::string& url,
                                    View<std::string> headers,
                                    const Path& download_path,
                                    StringView display_path,
                                    const Optional<std::string>& maybe_sha512);

    bool download_file_asset_cached(DiagnosticContext& context,
                                    MessageSink& machine_readable_progress,
                                    const AssetCachingSettings& asset_cache_settings,
                                    const Filesystem& fs,
                                    View<std::string> urls,
                                    View<std::string> headers,
                                    const Path& download_path,
                                    StringView display_path,
                                    const Optional<std::string>& maybe_sha512);

    bool store_to_asset_cache(DiagnosticContext& context,
                              StringView raw_url,
                              const SanitizedUrl& sanitized_url,
                              View<std::string> headers,
                              const Path& file);

    bool store_to_asset_cache(DiagnosticContext& context,
                              const AssetCachingSettings& asset_cache_settings,
                              const Path& file_to_put,
                              StringView sha512);

    bool azcopy_to_asset_cache(DiagnosticContext& context,
                               StringView raw_url,
                               const SanitizedUrl& sanitized_url,
                               const Path& file);

    // Replaces spaces with %20 for purposes of including in a URL.
    // This is typically used to filter a command line passed to `x-download` or similar which
    // might contain spaces that we, in turn, pass to curl.
    //
    // Notably, callers of this function can't use Strings::percent_encode because the URL
    // is likely to contain query parameters or similar.
    std::string url_encode_spaces(StringView url);
}

VCPKG_FORMAT_WITH_TO_STRING(vcpkg::SanitizedUrl);
