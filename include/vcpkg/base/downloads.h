#pragma once

#include <vcpkg/base/expected.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/optional.h>
#include <vcpkg/base/view.h>

#include <string>
#include <vector>

namespace vcpkg
{
    namespace details
    {
        struct SplitURIView
        {
            StringView scheme;
            Optional<StringView> authority;
            StringView path_query_fragment;
        };

        // e.g. {"https","//example.org", "/index.html"}
        ExpectedS<SplitURIView> split_uri_view(StringView uri);
    }

    void verify_downloaded_file_hash(const Filesystem& fs,
                                     const std::string& sanitized_url,
                                     const Path& downloaded_path,
                                     const std::string& sha512);

    View<std::string> azure_blob_headers();

    std::vector<int> download_files(Filesystem& fs, View<std::pair<std::string, Path>> url_pairs);
    ExpectedS<int> put_file(const Filesystem&, StringView url, View<std::string> headers, const Path& file);
    std::vector<int> url_heads(View<std::string> urls, View<std::string> headers);
    std::string replace_secrets(std::string input, View<std::string> secrets);

    struct DownloadManagerConfig
    {
        Optional<std::string> m_read_url_template;
        std::vector<std::string> m_read_headers;
        Optional<std::string> m_write_url_template;
        std::vector<std::string> m_write_headers;
        std::vector<std::string> m_secrets;
        bool m_block_origin = false;
    };

    // Handles downloading and uploading to a content addressable mirror
    struct DownloadManager
    {
        DownloadManager() = default;
        explicit DownloadManager(const DownloadManagerConfig& config) : m_config(config) { }
        explicit DownloadManager(DownloadManagerConfig&& config) : m_config(std::move(config)) { }

        void download_file(Filesystem& fs,
                           const std::string& url,
                           const Path& download_path,
                           const Optional<std::string>& sha512) const
        {
            this->download_file(fs, url, {}, download_path, sha512);
        }

        void download_file(Filesystem& fs,
                           const std::string& url,
                           View<std::string> headers,
                           const Path& download_path,
                           const Optional<std::string>& sha512) const;

        // Returns url that was successfully downloaded from
        std::string download_file(Filesystem& fs,
                                  View<std::string> urls,
                                  View<std::string> headers,
                                  const Path& download_path,
                                  const Optional<std::string>& sha512) const;

        ExpectedS<int> put_file_to_mirror(const Filesystem& fs, const Path& file_to_put, StringView sha512) const;

    private:
        DownloadManagerConfig m_config;
    };
}
