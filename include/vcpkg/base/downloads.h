#pragma once

#include <vcpkg/base/fwd/downloads.h>
#include <vcpkg/base/fwd/messages.h>

#include <vcpkg/base/expected.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/optional.h>
#include <vcpkg/base/span.h>

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

    void verify_downloaded_file_hash(const Filesystem& fs,
                                     StringView sanitized_url,
                                     const Path& downloaded_path,
                                     StringView sha512);

    View<std::string> azure_blob_headers();

    std::vector<int> download_files(Filesystem& fs,
                                    View<std::pair<std::string, Path>> url_pairs,
                                    View<std::string> headers);

    ExpectedL<int> put_file(const Filesystem&,
                            StringView url,
                            const std::vector<std::string>& secrets,
                            View<std::string> headers,
                            const Path& file,
                            StringView method = "PUT");

    ExpectedL<std::string> invoke_http_request(std::string method,
                                               View<std::string> headers,
                                               StringView url,
                                               std::string data = {});

    std::string format_url_query(std::string base_url, std::vector<std::string> query_params);

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

        void download_file(Filesystem& fs,
                           const std::string& url,
                           View<std::string> headers,
                           const Path& download_path,
                           const Optional<std::string>& sha512,
                           MessageSink& progress_sink) const;

        // Returns url that was successfully downloaded from
        std::string download_file(Filesystem& fs,
                                  View<std::string> urls,
                                  View<std::string> headers,
                                  const Path& download_path,
                                  const Optional<std::string>& sha512,
                                  MessageSink& progress_sink) const;

        ExpectedL<int> put_file_to_mirror(const Filesystem& fs, const Path& file_to_put, StringView sha512) const;

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
}
