#pragma once

#include <vcpkg/base/expected.h>
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

    enum class Sha512MismatchFormat
    {
        UserFriendly,
        GuidWrapped,
    };

    void verify_downloaded_file_hash(const Filesystem& fs,
                                     const std::string& sanitized_url,
                                     const Path& downloaded_path,
                                     const std::string& sha512,
                                     Sha512MismatchFormat mismatch_format = Sha512MismatchFormat::UserFriendly);

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

    enum class Sha512MismatchAction : bool
    {
        Warn,
        Error,
    };

    static constexpr StringLiteral guid_marker_hash_mismatch_start = "7279eda6-681f-46e0-aa5d-679ec14a2fb9";
    static constexpr StringLiteral guid_marker_hash_mismatch_end = "6982135f-5ad4-406f-86e3-f2e19c8966ef";
    // The following guids are used to detect the start and end of the output of the download command
    static constexpr StringLiteral guid_marker_hash_mismatch_general_start = "b360a6a9-fb74-41de-a4c5-a7faf126d565";
    static constexpr StringLiteral guid_marker_hash_mismatch_general_end = "9d36a06a-0efa-470a-9a1e-63a26be67a84";

    // Handles downloading and uploading to a content addressable mirror
    struct DownloadManager
    {
        DownloadManager() = default;
        explicit DownloadManager(const DownloadManagerConfig& config) : m_config(config) { }
        explicit DownloadManager(DownloadManagerConfig&& config) : m_config(std::move(config)) { }

        void download_file(Filesystem& fs,
                           const std::string& url,
                           const Path& download_path,
                           const Optional<std::string>& sha512,
                           Sha512MismatchAction mismatch_action = Sha512MismatchAction::Error,
                           Sha512MismatchFormat mismatch_format = Sha512MismatchFormat::UserFriendly) const
        {
            this->download_file(fs, url, {}, download_path, sha512, mismatch_action, mismatch_format);
        }

        void download_file(Filesystem& fs,
                           const std::string& url,
                           View<std::string> headers,
                           const Path& download_path,
                           const Optional<std::string>& sha512,
                           Sha512MismatchAction mismatch_action = Sha512MismatchAction::Error,
                           Sha512MismatchFormat mismatch_format = Sha512MismatchFormat::UserFriendly) const;

        // Returns url that was successfully downloaded from
        std::string download_file(Filesystem& fs,
                                  View<std::string> urls,
                                  View<std::string> headers,
                                  const Path& download_path,
                                  const Optional<std::string>& sha512,
                                  Sha512MismatchAction mismatch_action = Sha512MismatchAction::Error,
                                  Sha512MismatchFormat mismatch_format = Sha512MismatchFormat::UserFriendly) const;

        ExpectedS<int> put_file_to_mirror(const Filesystem& fs, const Path& file_to_put, StringView sha512) const;

    private:
        DownloadManagerConfig m_config;
    };
}
