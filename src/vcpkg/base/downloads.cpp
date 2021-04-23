#include <vcpkg/base/cache.h>
#include <vcpkg/base/downloads.h>
#include <vcpkg/base/hash.h>
#include <vcpkg/base/lockguarded.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.h>
#include <vcpkg/base/system.print.h>
#include <vcpkg/base/system.process.h>
#include <vcpkg/base/system.proxy.h>
#include <vcpkg/base/util.h>

namespace vcpkg::Downloads
{
#if defined(_WIN32)
    struct WinHttpHandleDeleter
    {
        void operator()(HINTERNET h) const { WinHttpCloseHandle(h); }
    };

    struct WinHttpRequest
    {
        static ExpectedS<WinHttpRequest> make(HINTERNET hConnect,
                                              StringView url_path,
                                              bool https,
                                              const wchar_t* method = L"GET")
        {
            WinHttpRequest ret;
            // Create an HTTP request handle.
            auto h = WinHttpOpenRequest(hConnect,
                                        method,
                                        Strings::to_utf16(url_path).c_str(),
                                        nullptr,
                                        WINHTTP_NO_REFERER,
                                        WINHTTP_DEFAULT_ACCEPT_TYPES,
                                        https ? WINHTTP_FLAG_SECURE : 0);
            if (!h) return Strings::concat("WinHttpOpenRequest() failed: ", GetLastError());
            ret.m_hRequest.reset(h);

            // Send a request.
            auto bResults = WinHttpSendRequest(
                ret.m_hRequest.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);

            if (!bResults) return Strings::concat("WinHttpSendRequest() failed: ", GetLastError());

            // End the request.
            bResults = WinHttpReceiveResponse(ret.m_hRequest.get(), NULL);
            if (!bResults) return Strings::concat("WinHttpReceiveResponse() failed: ", GetLastError());

            DWORD dwStatusCode = 0;
            DWORD dwSize = sizeof(dwStatusCode);

            bResults = WinHttpQueryHeaders(ret.m_hRequest.get(),
                                           WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                           WINHTTP_HEADER_NAME_BY_INDEX,
                                           &dwStatusCode,
                                           &dwSize,
                                           WINHTTP_NO_HEADER_INDEX);
            if (!bResults) return Strings::concat("WinHttpQueryHeaders() failed: ", GetLastError());
            if (dwStatusCode < 200 || dwStatusCode >= 300) return Strings::concat("failed: status code ", dwStatusCode);

            return std::move(ret);
        }

        template<class F>
        ExpectedS<int> forall_data(F f)
        {
            std::vector<char> buf;

            size_t total_downloaded_size = 0;
            DWORD dwSize = 0;
            do
            {
                DWORD downloaded_size = 0;
                auto bResults = WinHttpQueryDataAvailable(m_hRequest.get(), &dwSize);
                if (!bResults) return Strings::concat("WinHttpQueryDataAvailable() failed: ", GetLastError());

                if (buf.size() < dwSize) buf.resize(static_cast<size_t>(dwSize) * 2);

                bResults = WinHttpReadData(m_hRequest.get(), (LPVOID)buf.data(), dwSize, &downloaded_size);
                if (!bResults) return Strings::concat("WinHttpReadData() failed: ", GetLastError());
                f(Span<char>(buf.data(), downloaded_size));

                total_downloaded_size += downloaded_size;
            } while (dwSize > 0);
            return 1;
        }

        std::unique_ptr<void, WinHttpHandleDeleter> m_hRequest;
    };

    struct WinHttpSession
    {
        static ExpectedS<WinHttpSession> make()
        {
            auto h = WinHttpOpen(
                L"vcpkg/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
            if (!h) return Strings::concat("WinHttpOpen() failed: ", GetLastError());
            WinHttpSession ret;
            ret.m_hSession.reset(h);

            // If the environment variable HTTPS_PROXY is set
            // use that variable as proxy. This situation might exist when user is in a company network
            // with restricted network/proxy settings
            auto maybe_https_proxy_env = System::get_environment_variable("HTTPS_PROXY");
            if (auto p_https_proxy = maybe_https_proxy_env.get())
            {
                std::wstring env_proxy_settings = Strings::to_utf16(*p_https_proxy);
                WINHTTP_PROXY_INFO proxy;
                proxy.dwAccessType = WINHTTP_ACCESS_TYPE_NAMED_PROXY;
                proxy.lpszProxy = env_proxy_settings.data();
                proxy.lpszProxyBypass = nullptr;

                WinHttpSetOption(ret.m_hSession.get(), WINHTTP_OPTION_PROXY, &proxy, sizeof(proxy));
            }
            // IE Proxy fallback, this works on Windows 10
            else
            {
                // We do not use WPAD anymore
                // Directly read IE Proxy setting
                auto ieProxy = System::get_windows_ie_proxy_server();
                if (ieProxy.has_value())
                {
                    WINHTTP_PROXY_INFO proxy;
                    proxy.dwAccessType = WINHTTP_ACCESS_TYPE_NAMED_PROXY;
                    proxy.lpszProxy = ieProxy.get()->server.data();
                    proxy.lpszProxyBypass = ieProxy.get()->bypass.data();
                    WinHttpSetOption(ret.m_hSession.get(), WINHTTP_OPTION_PROXY, &proxy, sizeof(proxy));
                }
            }

            // Use Windows 10 defaults on Windows 7
            DWORD secure_protocols(WINHTTP_FLAG_SECURE_PROTOCOL_TLS1 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1 |
                                   WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2);
            WinHttpSetOption(
                ret.m_hSession.get(), WINHTTP_OPTION_SECURE_PROTOCOLS, &secure_protocols, sizeof(secure_protocols));

            // Many open source mirrors such as https://download.gnome.org/ will redirect to http mirrors.
            // `curl.exe -L` does follow https -> http redirection.
            // Additionally, vcpkg hash checks the resulting archive.
            DWORD redirect_policy(WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS);
            WinHttpSetOption(
                ret.m_hSession.get(), WINHTTP_OPTION_REDIRECT_POLICY, &redirect_policy, sizeof(redirect_policy));

            return ret;
        }

        std::unique_ptr<void, WinHttpHandleDeleter> m_hSession;
    };

    struct WinHttpConnection
    {
        static ExpectedS<WinHttpConnection> make(HINTERNET hSession, StringView hostname, INTERNET_PORT port)
        {
            // Specify an HTTP server.
            auto h = WinHttpConnect(hSession, Strings::to_utf16(hostname).c_str(), port, 0);
            if (!h) return Strings::concat("WinHttpConnect() failed: ", GetLastError());
            WinHttpConnection ret;
            ret.m_hConnect.reset(h);
            return ret;
        }

        std::unique_ptr<void, WinHttpHandleDeleter> m_hConnect;
    };
#endif

    ExpectedS<details::SplitURIView> details::split_uri_view(StringView uri)
    {
        auto sep = std::find(uri.begin(), uri.end(), ':');
        if (sep == uri.end()) return Strings::concat("Error: unable to parse uri: '", uri, "'");

        StringView scheme(uri.begin(), sep);
        if (Strings::starts_with({sep + 1, uri.end()}, "//"))
        {
            auto path_start = std::find(sep + 3, uri.end(), '/');
            return details::SplitURIView{scheme, StringView{sep + 1, path_start}, {path_start, uri.end()}};
        }
        // no authority
        return details::SplitURIView{scheme, {}, {sep + 1, uri.end()}};
    }

    static std::string format_hash_mismatch(StringView url,
                                            const fs::path& path,
                                            StringView expected,
                                            StringView actual)
    {
        return Strings::format("File does not have the expected hash:\n"
                               "             url : [ %s ]\n"
                               "       File path : [ %s ]\n"
                               "   Expected hash : [ %s ]\n"
                               "     Actual hash : [ %s ]\n",
                               url,
                               fs::u8string(path),
                               expected,
                               actual);
    }

    static Optional<std::string> try_verify_downloaded_file_hash(const Files::Filesystem& fs,
                                                                 const std::string& url,
                                                                 const fs::path& path,
                                                                 const std::string& sha512)
    {
        std::string actual_hash = vcpkg::Hash::get_file_hash(VCPKG_LINE_INFO, fs, path, Hash::Algorithm::Sha512);

        // <HACK to handle NuGet.org changing nupkg hashes.>
        // This is the NEW hash for 7zip
        if (actual_hash == "a9dfaaafd15d98a2ac83682867ec5766720acf6e99d40d1a00d480692752603bf3f3742623f0ea85647a92374df"
                           "405f331afd6021c5cf36af43ee8db198129c0")
        {
            // This is the OLD hash for 7zip
            actual_hash = "8c75314102e68d2b2347d592f8e3eb05812e1ebb525decbac472231633753f1d4ca31c8e6881a36144a8da26b257"
                          "1305b3ae3f4e2b85fc4a290aeda63d1a13b8";
        }
        // </HACK>

        if (sha512 != actual_hash)
        {
            return format_hash_mismatch(url, fs::u8string(path), sha512, actual_hash);
        }
        return nullopt;
    }

    void verify_downloaded_file_hash(const Files::Filesystem& fs,
                                     const std::string& url,
                                     const fs::path& path,
                                     const std::string& sha512)
    {
        auto maybe_error = try_verify_downloaded_file_hash(fs, url, path, sha512);
        if (auto err = maybe_error.get())
        {
            Checks::exit_with_message(VCPKG_LINE_INFO, *err);
        }
    }

    static void url_heads_inner(View<std::string> urls, std::vector<int>* out)
    {
        static constexpr StringLiteral guid_marker = "8a1db05f-a65d-419b-aa72-037fb4d0672e";

        System::Command cmd;
        cmd.string_arg("curl")
            .string_arg("--head")
            .string_arg("--location")
            .string_arg("-w")
            .string_arg(Strings::concat(guid_marker, " %{http_code}\\n"));
        for (auto&& url : urls)
        {
            cmd.string_arg(url);
        }
        auto res = System::cmd_execute_and_stream_lines(cmd, [out](StringView line) {
            if (Strings::starts_with(line, guid_marker))
            {
                out->push_back(std::strtol(line.data() + guid_marker.size(), nullptr, 10));
            }
        });
        Checks::check_exit(VCPKG_LINE_INFO, res == 0, "curl failed to execute with exit code: %d", res);
    }
    std::vector<int> url_heads(View<std::string> urls)
    {
        static constexpr size_t batch_size = 100;

        std::vector<int> ret;

        size_t i = 0;
        for (; i + batch_size <= urls.size(); i += batch_size)
        {
            url_heads_inner({urls.data() + i, batch_size}, &ret);
        }
        if (i != urls.size()) url_heads_inner({urls.begin() + i, urls.end()}, &ret);

        return ret;
    }

    static void download_files_inner(Files::Filesystem&,
                                     View<std::pair<std::string, fs::path>> url_pairs,
                                     std::vector<int>* out)
    {
        static constexpr StringLiteral guid_marker = "8a1db05f-a65d-419b-aa72-037fb4d0672e";

        System::Command cmd;
        cmd.string_arg("curl")
            .string_arg("--location")
            .string_arg("-w")
            .string_arg(Strings::concat(guid_marker, " %{http_code}\\n"));
        for (auto&& url : url_pairs)
        {
            cmd.string_arg(url.first).string_arg("-o").path_arg(url.second);
        }
        auto res = System::cmd_execute_and_stream_lines(cmd, [out](StringView line) {
            if (Strings::starts_with(line, guid_marker))
            {
                out->push_back(std::strtol(line.data() + guid_marker.size(), nullptr, 10));
            }
        });
        Checks::check_exit(VCPKG_LINE_INFO, res == 0, "curl failed to execute with exit code: %d", res);
    }
    std::vector<int> download_files(Files::Filesystem& fs, View<std::pair<std::string, fs::path>> url_pairs)
    {
        static constexpr size_t batch_size = 50;

        std::vector<int> ret;

        size_t i = 0;
        for (; i + batch_size <= url_pairs.size(); i += batch_size)
        {
            download_files_inner(fs, {url_pairs.data() + i, batch_size}, &ret);
        }
        if (i != url_pairs.size()) download_files_inner(fs, {url_pairs.begin() + i, url_pairs.end()}, &ret);

        Checks::check_exit(VCPKG_LINE_INFO, ret.size() == url_pairs.size());
        return ret;
    }

    int put_file(const Files::Filesystem&, StringView url, const fs::path& file)
    {
        static constexpr StringLiteral guid_marker = "9a1db05f-a65d-419b-aa72-037fb4d0672e";

        if (Strings::starts_with(url, "ftp://"))
        {
            System::Command cmd;
            cmd.string_arg("curl");
            cmd.string_arg(url);
            cmd.string_arg("-T").path_arg(file);
            auto res = System::cmd_execute_and_capture_output(cmd);
            if (res.exit_code != 0)
            {
                Debug::print(res.output, '\n');
                System::print2(System::Color::warning, "curl failed to execute with exit code: ", res.exit_code, '\n');
            }
            return res.exit_code;
        }
        System::Command cmd;
        cmd.string_arg("curl").string_arg("-X").string_arg("PUT");
        cmd.string_arg("-w").string_arg(Strings::concat("\\n", guid_marker, "%{http_code}"));
        cmd.string_arg(url);
        cmd.string_arg("-T").path_arg(file);
        cmd.string_arg("-H").string_arg("x-ms-version: 2020-04-08");
        cmd.string_arg("-H").string_arg("x-ms-blob-type: BlockBlob");
        int code = 0;
        auto res = System::cmd_execute_and_stream_lines(cmd, [&code](StringView line) {
            if (Strings::starts_with(line, guid_marker))
            {
                code = std::strtol(line.data() + guid_marker.size(), nullptr, 10);
            }
        });
        if (res != 0)
        {
            System::print2(System::Color::warning, "curl failed to execute with exit code: ", res, '\n');
        }
        return code;
    }

    void download_file(Files::Filesystem& fs,
                       const std::string& url,
                       const fs::path& download_path,
                       const std::string& sha512)
    {
        download_file(fs, {&url, 1}, download_path, sha512);
    }

#if defined(_WIN32)
    namespace
    {
        struct WriteFlushFile
        {
            WriteFlushFile(const fs::path& p)
            {
                auto err = _wfopen_s(&f, p.c_str(), L"wb");
                Checks::check_exit(VCPKG_LINE_INFO,
                                   !err,
                                   "Failed to open file %s. Error code was %s",
                                   fs::u8string(p),
                                   std::to_string(err));
                ASSUME(f != nullptr);
            }
            ~WriteFlushFile()
            {
                if (f)
                {
                    fflush(f);
                    fclose(f);
                }
            }
            FILE* f = nullptr;
        };

        /// <summary>
        /// Download a file using WinHTTP -- only supports HTTP and HTTPS
        /// </summary>
        static bool download_winhttp(Files::Filesystem& fs,
                                     const fs::path& download_path_part_path,
                                     details::SplitURIView split_uri,
                                     const std::string& url,
                                     std::string& errors)
        {
            // `download_winhttp` does not support user or port syntax in authorities
            auto hostname = split_uri.authority.value_or_exit(VCPKG_LINE_INFO).substr(2);
            INTERNET_PORT port;
            if (split_uri.scheme == "https")
            {
                port = INTERNET_DEFAULT_HTTPS_PORT;
            }
            else if (split_uri.scheme == "http")
            {
                port = INTERNET_DEFAULT_HTTP_PORT;
            }
            else
            {
                Checks::unreachable(VCPKG_LINE_INFO);
            }

            // Make sure the directories are present, otherwise fopen_s fails
            const auto dir = download_path_part_path.parent_path();
            fs.create_directories(dir, VCPKG_LINE_INFO);

            WriteFlushFile f(download_path_part_path);

            Debug::print("Downloading ", url, "\n");
            static auto s = WinHttpSession::make().value_or_exit(VCPKG_LINE_INFO);
            auto conn = WinHttpConnection::make(s.m_hSession.get(), hostname, port);
            if (!conn)
            {
                Strings::append(errors, url, ": ", conn.error(), '\n');
                return false;
            }
            auto req = WinHttpRequest::make(
                conn.get()->m_hConnect.get(), split_uri.path_query_fragment, split_uri.scheme == "https");
            if (!req)
            {
                Strings::append(errors, url, ": ", req.error(), '\n');
                return false;
            }
            auto forall_data =
                req.get()->forall_data([&f](Span<char> span) { fwrite(span.data(), 1, span.size(), f.f); });
            if (!forall_data)
            {
                Strings::append(errors, url, ": ", forall_data.error(), '\n');
                return false;
            }
            return true;
        }
    }
#endif

    std::string download_file(vcpkg::Files::Filesystem& fs,
                              View<std::string> urls,
                              const fs::path& download_path,
                              const std::string& sha512)
    {
        Checks::check_exit(VCPKG_LINE_INFO, urls.size() > 0);

        auto download_path_part_path = download_path;
#if defined(_WIN32)
        download_path_part_path += fs::u8path(Strings::concat(".", _getpid(), ".part"));
#else
        download_path_part_path += fs::u8path(Strings::concat(".", getpid(), ".part"));
#endif

        std::string errors;
        for (const std::string& url : urls)
        {
#if defined(_WIN32)
            auto split_uri = details::split_uri_view(url).value_or_exit(VCPKG_LINE_INFO);
            auto authority = split_uri.authority.value_or_exit(VCPKG_LINE_INFO).substr(2);
            if (split_uri.scheme == "https" || split_uri.scheme == "http")
            {
                // This check causes complex URLs (non-default port, embedded basic auth) to be passed down to curl.exe
                if (Strings::find_first_of(authority, ":@") == authority.end())
                {
                    if (download_winhttp(fs, download_path_part_path, split_uri, url, errors))
                    {
                        auto maybe_error = try_verify_downloaded_file_hash(fs, url, download_path_part_path, sha512);
                        if (auto err = maybe_error.get())
                        {
                            Strings::append(errors, *err);
                        }
                        else
                        {
                            fs.rename(download_path_part_path, download_path, VCPKG_LINE_INFO);
                            return url;
                        }
                    }
                    continue;
                }
            }
#endif
            System::Command cmd;
            cmd.string_arg("curl")
                .string_arg("--fail")
                .string_arg("-L")
                .string_arg(url)
                .string_arg("--create-dirs")
                .string_arg("--output")
                .path_arg(download_path_part_path);
            const auto out = System::cmd_execute_and_capture_output(cmd);
            if (out.exit_code != 0)
            {
                Strings::append(errors, url, ": ", out.output, '\n');
                continue;
            }

            auto maybe_error = try_verify_downloaded_file_hash(fs, url, download_path_part_path, sha512);
            if (auto err = maybe_error.get())
            {
                Strings::append(errors, *err);
            }
            else
            {
                fs.rename(download_path_part_path, download_path, VCPKG_LINE_INFO);
                return url;
            }
        }
        Checks::exit_with_message(VCPKG_LINE_INFO, Strings::concat("Failed to download from mirror set:\n", errors));
    }

    DownloadManager::DownloadManager(Optional<std::string> read_url_template,
                                     Optional<std::string> write_url_template,
                                     bool block_origin)
        : m_block_origin(block_origin)
        , m_read_url_template(std::move(read_url_template))
        , m_write_url_template(std::move(write_url_template))
    {
    }

    void DownloadManager::download_file(Files::Filesystem& fs,
                                        const std::string& url,
                                        const fs::path& download_path,
                                        const std::string& sha512) const
    {
        this->download_file(fs, View<std::string>(&url, 1), download_path, sha512);
    }

    std::string DownloadManager::download_file(Files::Filesystem& fs,
                                               View<std::string> urls,
                                               const fs::path& download_path,
                                               const std::string& sha512) const
    {
        auto maybe_mirror_url = Strings::replace_all(m_read_url_template.value_or(""), "<SHA>", sha512);

        std::vector<std::string> all_urls;
        if (!maybe_mirror_url.empty()) all_urls.push_back(maybe_mirror_url);

        if (!m_block_origin)
        {
            Util::Vectors::append(&all_urls, urls);
        }

        if (all_urls.empty())
        {
            Checks::exit_with_message(VCPKG_LINE_INFO, "Error: No urls specified to download SHA: %s", sha512);
        }

        auto fetched_url = Downloads::download_file(fs, all_urls, download_path, sha512);
        if (fetched_url != maybe_mirror_url)
        {
            put_file_to_mirror(fs, download_path, sha512);
        }
        return fetched_url;
    }

    int DownloadManager::put_file_to_mirror(const Files::Filesystem& fs,
                                            const fs::path& path,
                                            const std::string& sha512) const
    {
        int code = 0;
        auto maybe_mirror_url = Strings::replace_all(m_write_url_template.value_or(""), "<SHA>", sha512);
        if (!maybe_mirror_url.empty())
        {
            code = Downloads::put_file(fs, maybe_mirror_url, path);
            Debug::print("Putting to cache: ", maybe_mirror_url, ": ", code, "\n");
        }
        return code;
    }
}
