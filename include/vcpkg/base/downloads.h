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
    struct SplitURIView
    {
        StringView scheme;
        Optional<StringView> authority;
        StringView path_query_fragment;
    };

    // e.g. {"https","//example.org", "/index.html"}
    ExpectedL<SplitURIView> split_uri_view(StringView uri);

    void verify_downloaded_file_hash(const ReadOnlyFilesystem& fs,
                                     StringView sanitized_url,
                                     const Path& downloaded_path,
                                     StringView sha512);

    View<std::string> azure_blob_headers();

    std::vector<int> download_files(View<std::pair<std::string, Path>> url_pairs,
                                    View<std::string> headers,
                                    View<std::string> secrets);

    bool send_snapshot_to_api(const std::string& github_token,
                              const std::string& github_repository,
                              const Json::Object& snapshot);
    ExpectedL<int> put_file(const ReadOnlyFilesystem&,
                            StringView url,
                            const std::vector<std::string>& secrets,
                            View<std::string> headers,
                            const Path& file,
                            StringView method = "PUT");

    ExpectedL<std::string> invoke_http_request(StringView method,
                                               View<std::string> headers,
                                               StringView url,
                                               StringView data = {});

    std::string format_url_query(StringView base_url, View<std::string> query_params);

    std::vector<int> url_heads(View<std::string> urls, View<std::string> headers, View<std::string> secrets);

    struct DownloadManagerConfig
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
    struct DownloadManager
    {
        DownloadManager() = default;
        explicit DownloadManager(const DownloadManagerConfig& config) : m_config(config) { }
        explicit DownloadManager(DownloadManagerConfig&& config) : m_config(std::move(config)) { }

        void download_file(const Filesystem& fs,
                           const std::string& url,
                           View<std::string> headers,
                           const Path& download_path,
                           const Optional<std::string>& sha512,
                           MessageSink& progress_sink) const;

        // Returns url that was successfully downloaded from
        std::string download_file(const Filesystem& fs,
                                  View<std::string> urls,
                                  View<std::string> headers,
                                  const Path& download_path,
                                  const Optional<std::string>& sha512,
                                  MessageSink& progress_sink) const;

        ExpectedL<int> put_file_to_mirror(const ReadOnlyFilesystem& fs,
                                          const Path& file_to_put,
                                          StringView sha512) const;

    private:
        DownloadManagerConfig m_config;
    };

    Optional<unsigned long long> try_parse_curl_max5_size(StringView sv);

    struct CurlProgressData
    {
        unsigned int total_percent;
        unsigned long long total_size;
        unsigned int recieved_percent;
        unsigned long long recieved_size;
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

    struct ProxyCredentials
    {
        std::wstring username;
        std::wstring password;
    };

    bool operator==(const ProxyCredentials &lhs, const ProxyCredentials &rhs);

    struct ProxyUrlParts
    {
        std::wstring host;
        Optional<ProxyCredentials> credentials;
    };

    bool operator==(const ProxyUrlParts &lhs, const ProxyUrlParts &rhs);

    // Parses strings such as http://login:password@host.com:8080
    // Into plain URL and credentials
    ProxyUrlParts parse_proxy_url(const std::wstring& url);
}
