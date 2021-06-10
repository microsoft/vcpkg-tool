#pragma once

#include <vcpkg/base/files.h>
#include <vcpkg/base/optional.h>
#include <vcpkg/base/view.h>

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
                                     const std::string& url,
                                     const fs::path& path,
                                     const std::string& sha512);

    // Returns url that was successfully downloaded from
    std::string download_file(Files::Filesystem& fs,
                              View<std::string> urls,
                              View<std::string> headers,
                              const fs::path& download_path,
                              const std::string& sha512);

    void download_file(Files::Filesystem& fs,
                       const std::string& url,
                       View<std::string> headers,
                       const fs::path& download_path,
                       const std::string& sha512);

    View<std::string> azure_blob_headers();

    std::vector<int> download_files(Files::Filesystem& fs, View<std::pair<std::string, fs::path>> url_pairs);
    int put_file(const Files::Filesystem&, StringView url, View<std::string> headers, const fs::path& file);
    std::vector<int> url_heads(View<std::string> urls, View<std::string> headers);

    struct DownloadManagerConfig
    {
        Optional<std::string> m_read_url_template;
        std::vector<std::string> m_read_headers;
        Optional<std::string> m_write_url_template;
        std::vector<std::string> m_write_headers;
        bool m_block_origin = false;
    };

    // Handles downloading and uploading to a content addressable mirror
    struct DownloadManager : private DownloadManagerConfig
    {
        DownloadManager() = default;
        explicit DownloadManager(Optional<std::string> read_url_template,
                                 std::vector<std::string> read_headers,
                                 Optional<std::string> write_url_template,
                                 std::vector<std::string> write_headers,
                                 bool block_origin);

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

        std::string download_file(Files::Filesystem& fs,
                                  View<std::string> urls,
                                  View<std::string> headers,
                                  const fs::path& download_path,
                                  const std::string& sha512) const;

        int put_file_to_mirror(const Files::Filesystem& fs, const fs::path& path, const std::string& sha512) const;

        const DownloadManagerConfig& internal_get_config() const { return *this; }

        bool block_origin() const { return m_block_origin; }
    };
}
