#pragma once

#include <vcpkg/base/files.h>
#include <vcpkg/base/optional.h>
#include <vcpkg/base/view.h>

#include <string>
#include <vector>

namespace vcpkg::Downloads
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

    void verify_downloaded_file_hash(const Files::Filesystem& fs,
                                     const std::string& sanitized_url,
                                     const fs::path& path,
                                     const std::string& sha512);

    View<std::string> azure_blob_headers();

    std::vector<int> download_files(Files::Filesystem& fs, View<std::pair<std::string, fs::path>> url_pairs);
    ExpectedS<int> put_file(const Files::Filesystem&, StringView url, View<std::string> headers, const fs::path& file);
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
    class DownloadManager
    {
        DownloadManagerConfig m_config;

    public:
        DownloadManager() = default;
        explicit DownloadManager(const DownloadManagerConfig& config) : m_config(config) { }
        explicit DownloadManager(DownloadManagerConfig&& config) : m_config(std::move(config)) { }

        void download_file(Files::Filesystem& fs,
                           const std::string& url,
                           const fs::path& download_path,
                           const std::string& sha512) const
        {
            this->download_file(fs, url, {}, download_path, sha512);
        }

        void download_file(Files::Filesystem& fs,
                           const std::string& url,
                           View<std::string> headers,
                           const fs::path& download_path,
                           const std::string& sha512) const;

        // Returns url that was successfully downloaded from
        std::string download_file(Files::Filesystem& fs,
                                  View<std::string> urls,
                                  View<std::string> headers,
                                  const fs::path& download_path,
                                  const std::string& sha512) const;

        ExpectedS<int> put_file_to_mirror(const Files::Filesystem& fs,
                                          const fs::path& path,
                                          const std::string& sha512) const;
    };
}
