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

    struct SplitUrlView
    {
        StringView scheme;
        Optional<StringView> authority;
        StringView path_query_fragment;
    };

    // e.g. {"https","//example.org", "/index.html"}
    Optional<SplitUrlView> parse_split_url_view(StringView raw_url);

    View<std::string> azure_blob_headers();

    // Parses a curl output line for curl invoked with
    // -w "PREFIX%{http_code} %{exitcode} %{errormsg}"
    // with specific handling for curl version < 7.75.0 which does not understand %{exitcode} %{errormsg}
    // If the line is malformed for any reason, no entry to http_codes is added.
    // Returns: true if the new version of curl's output with exitcode and errormsg was parsed; otherwise, false.
    bool parse_curl_status_line(DiagnosticContext& context,
                                std::vector<int>& http_codes,
                                StringLiteral prefix,
                                StringView this_line);

    std::vector<int> download_files_no_cache(DiagnosticContext& context,
                                             View<std::pair<std::string, Path>> url_pairs,
                                             View<std::string> headers,
                                             View<std::string> secrets);

    bool submit_github_dependency_graph_snapshot(DiagnosticContext& context,
                                                 const Optional<std::string>& maybe_github_server_url,
                                                 const std::string& github_token,
                                                 const std::string& github_repository,
                                                 const Json::Object& snapshot);

    Optional<std::string> invoke_http_request(DiagnosticContext& context,
                                              StringLiteral method,
                                              View<std::string> headers,
                                              StringView url,
                                              View<std::string> secrets,
                                              StringView data = {});

    std::string format_url_query(StringView base_url, View<std::string> query_params);

    std::vector<int> url_heads(DiagnosticContext& context,
                               View<std::string> urls,
                               View<std::string> headers,
                               View<std::string> secrets);

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
                              StringLiteral method,
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

    Optional<unsigned long long> try_parse_curl_max5_size(StringView sv);

    struct CurlProgressData
    {
        unsigned int total_percent;
        unsigned long long total_size;
        unsigned int received_percent;
        unsigned long long received_size;
        unsigned int transfer_percent;
        unsigned long long transfer_size;
        unsigned long long average_download_speed; // bytes per second
        unsigned long long average_upload_speed;   // bytes per second
        // ElapsedTime total_time;
        // ElapsedTime time_spent;
        // ElapsedTime time_left;
        unsigned long long current_speed;
    };

    Optional<CurlProgressData> try_parse_curl_progress_data(StringView curl_progress_line);

    // Replaces spaces with %20 for purposes of including in a URL.
    // This is typically used to filter a command line passed to `x-download` or similar which
    // might contain spaces that we, in turn, pass to curl.
    //
    // Notably, callers of this function can't use Strings::percent_encode because the URL
    // is likely to contain query parameters or similar.
    std::string url_encode_spaces(StringView url);
}

VCPKG_FORMAT_WITH_TO_STRING(vcpkg::SanitizedUrl);
