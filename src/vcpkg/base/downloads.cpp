#include <vcpkg/base/api_stable_format.h>
#include <vcpkg/base/cache.h>
#include <vcpkg/base/downloads.h>
#include <vcpkg/base/hash.h>
#include <vcpkg/base/json.h>
#include <vcpkg/base/parse.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.h>
#include <vcpkg/base/system.process.h>
#include <vcpkg/base/system.proxy.h>
#include <vcpkg/base/util.h>

namespace vcpkg
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

            return ret;
        }

        ExpectedS<int> query_status() const
        {
            DWORD dwStatusCode = 0;
            DWORD dwSize = sizeof(dwStatusCode);

            auto bResults = WinHttpQueryHeaders(m_hRequest.get(),
                                                WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                                WINHTTP_HEADER_NAME_BY_INDEX,
                                                &dwStatusCode,
                                                &dwSize,
                                                WINHTTP_NO_HEADER_INDEX);
            if (!bResults) return Strings::concat("WinHttpQueryHeaders() failed: ", GetLastError());
            return dwStatusCode;
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

            // Increase default timeouts to help connections behind proxies
            // WinHttpSetTimeouts(HINTERNET hInternet, int nResolveTimeout, int nConnectTimeout, int nSendTimeout, int
            // nReceiveTimeout);
            WinHttpSetTimeouts(h, 0, 120000, 120000, 120000);

            // If the environment variable HTTPS_PROXY is set
            // use that variable as proxy. This situation might exist when user is in a company network
            // with restricted network/proxy settings
            auto maybe_https_proxy_env = get_environment_variable("HTTPS_PROXY");
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
                auto ieProxy = get_windows_ie_proxy_server();
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

    ExpectedS<SplitURIView> split_uri_view(StringView uri)
    {
        auto sep = std::find(uri.begin(), uri.end(), ':');
        if (sep == uri.end()) return Strings::concat("Error: unable to parse uri: '", uri, "'");

        StringView scheme(uri.begin(), sep);
        if (Strings::starts_with({sep + 1, uri.end()}, "//"))
        {
            auto path_start = std::find(sep + 3, uri.end(), '/');
            return SplitURIView{scheme, StringView{sep + 1, path_start}, {path_start, uri.end()}};
        }
        // no authority
        return SplitURIView{scheme, {}, {sep + 1, uri.end()}};
    }

    static std::string format_hash_mismatch(StringView url,
                                            const Path& downloaded_path,
                                            StringView expected,
                                            StringView actual)
    {
        return Strings::format("File does not have the expected hash:\n"
                               "             url : [ %s ]\n"
                               "       File path : [ %s ]\n"
                               "   Expected hash : [ %s ]\n"
                               "     Actual hash : [ %s ]\n",
                               url,
                               downloaded_path,
                               expected,
                               actual);
    }

    static Optional<std::string> try_verify_downloaded_file_hash(const Filesystem& fs,
                                                                 StringView sanitized_url,
                                                                 const Path& downloaded_path,
                                                                 StringView sha512)
    {
        std::string actual_hash =
            vcpkg::Hash::get_file_hash(fs, downloaded_path, Hash::Algorithm::Sha512).value_or_exit(VCPKG_LINE_INFO);

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

        if (!Strings::case_insensitive_ascii_equals(sha512, actual_hash))
        {
            return format_hash_mismatch(sanitized_url, downloaded_path, sha512, actual_hash);
        }
        return nullopt;
    }

    void verify_downloaded_file_hash(const Filesystem& fs,
                                     StringView url,
                                     const Path& downloaded_path,
                                     StringView sha512)
    {
        auto maybe_error = try_verify_downloaded_file_hash(fs, url, downloaded_path, sha512);
        if (auto err = maybe_error.get())
        {
            Checks::exit_with_message(VCPKG_LINE_INFO, *err);
        }
    }

    static bool check_downloaded_file_hash(Filesystem& fs,
                                           const Optional<std::string>& hash,
                                           StringView sanitized_url,
                                           const Path& download_part_path,
                                           std::string& errors)
    {
        if (auto p = hash.get())
        {
            auto maybe_error = try_verify_downloaded_file_hash(fs, sanitized_url, download_part_path, *p);
            if (auto err = maybe_error.get())
            {
                Strings::append(errors, *err, '\n');
                return false;
            }
        }
        return true;
    }

    static void url_heads_inner(View<std::string> urls,
                                View<std::string> headers,
                                std::vector<int>* out,
                                View<std::string> secrets)
    {
        static constexpr StringLiteral guid_marker = "8a1db05f-a65d-419b-aa72-037fb4d0672e";

        const size_t start_size = out->size();

        Command cmd;
        cmd.string_arg("curl")
            .string_arg("--head")
            .string_arg("--location")
            .string_arg("-w")
            .string_arg(Strings::concat(guid_marker, " %{http_code}\\n"));
        for (auto&& header : headers)
        {
            cmd.string_arg("-H").string_arg(header);
        }
        for (auto&& url : urls)
        {
            cmd.string_arg(url);
        }

        std::vector<std::string> lines;

        auto res = cmd_execute_and_stream_lines(cmd, [out, &lines](StringView line) {
                       lines.push_back(line.to_string());
                       if (Strings::starts_with(line, guid_marker))
                       {
                           out->push_back(std::strtol(line.data() + guid_marker.size(), nullptr, 10));
                       }
                   }).value_or_exit(VCPKG_LINE_INFO);

        if (res != 0)
        {
            Checks::msg_exit_with_error(VCPKG_LINE_INFO, msgCurlFailedToExecute, msg::exit_code = res);
        }

        if (out->size() != start_size + urls.size())
        {
            auto command_line = replace_secrets(std::move(cmd).extract(), secrets);
            auto actual = replace_secrets(Strings::join("\n", lines), secrets);
            Checks::msg_exit_with_error(VCPKG_LINE_INFO,
                                        msgCurlReportedUnexpectedResults,
                                        msg::command_line = command_line,
                                        msg::actual = actual);
        }
    }
    std::vector<int> url_heads(View<std::string> urls, View<std::string> headers, View<std::string> secrets)
    {
        static constexpr size_t batch_size = 100;

        std::vector<int> ret;

        size_t i = 0;
        for (; i + batch_size <= urls.size(); i += batch_size)
        {
            url_heads_inner({urls.data() + i, batch_size}, headers, &ret, secrets);
        }
        if (i != urls.size()) url_heads_inner({urls.begin() + i, urls.end()}, headers, &ret, secrets);

        return ret;
    }

    std::string replace_secrets(std::string input, View<std::string> secrets)
    {
        static constexpr StringLiteral replacement{"*** SECRET ***"};
        for (const auto& secret : secrets)
        {
            Strings::inplace_replace_all(input, secret, replacement);
        }

        return input;
    }

    static void download_files_inner(Filesystem&,
                                     View<std::pair<std::string, Path>> url_pairs,
                                     View<std::string> headers,
                                     std::vector<int>* out)
    {
        for (auto i : {100, 1000, 10000, 0})
        {
            size_t start_size = out->size();
            static constexpr StringLiteral guid_marker = "5ec47b8e-6776-4d70-b9b3-ac2a57bc0a1c";

            Command cmd;
            cmd.string_arg("curl")
                .string_arg("--create-dirs")
                .string_arg("--location")
                .string_arg("-w")
                .string_arg(Strings::concat(guid_marker, " %{http_code}\\n"));
            for (StringView header : headers)
            {
                cmd.string_arg("-H").string_arg(header);
            }
            for (auto&& url : url_pairs)
            {
                cmd.string_arg(url.first).string_arg("-o").string_arg(url.second);
            }
            auto res =
                cmd_execute_and_stream_lines(cmd, [out](StringView line) {
                    if (Strings::starts_with(line, guid_marker))
                    {
                        out->push_back(static_cast<int>(std::strtol(line.data() + guid_marker.size(), nullptr, 10)));
                    }
                }).value_or_exit(VCPKG_LINE_INFO);

            if (res != 0)
            {
                Checks::msg_exit_with_error(VCPKG_LINE_INFO, msgCurlFailedToExecute, msg::exit_code = res);
            }

            if (start_size + url_pairs.size() > out->size())
            {
                // curl stopped before finishing all downloads; retry after some time
                msg::println_warning(msgUnexpectedErrorDuringBulkDownload);
                std::this_thread::sleep_for(std::chrono::milliseconds(i));
                url_pairs =
                    View<std::pair<std::string, Path>>{url_pairs.begin() + out->size() - start_size, url_pairs.end()};
            }
            else
            {
                break;
            }
        }
    }
    std::vector<int> download_files(Filesystem& fs,
                                    View<std::pair<std::string, Path>> url_pairs,
                                    View<std::string> headers)
    {
        static constexpr size_t batch_size = 50;

        std::vector<int> ret;

        size_t i = 0;
        for (; i + batch_size <= url_pairs.size(); i += batch_size)
        {
            download_files_inner(fs, {url_pairs.data() + i, batch_size}, headers, &ret);
        }
        if (i != url_pairs.size()) download_files_inner(fs, {url_pairs.begin() + i, url_pairs.end()}, headers, &ret);

        Checks::msg_check_exit(VCPKG_LINE_INFO,
                               ret.size() == url_pairs.size(),
                               msgCurlReturnedUnexpectedResponseCodes,
                               msg::actual = ret.size(),
                               msg::expected = url_pairs.size());
        return ret;
    }

    ExpectedS<int> put_file(const Filesystem&, StringView url, View<std::string> headers, const Path& file)
    {
        static constexpr StringLiteral guid_marker = "9a1db05f-a65d-419b-aa72-037fb4d0672e";

        if (Strings::starts_with(url, "ftp://"))
        {
            // HTTP headers are ignored for FTP clients
            Command cmd;
            cmd.string_arg("curl");
            cmd.string_arg(url);
            cmd.string_arg("-T").string_arg(file);
            auto maybe_res = cmd_execute_and_capture_output(cmd);
            if (auto res = maybe_res.get())
            {
                if (res->exit_code == 0)
                {
                    return 0;
                }

                Debug::print(res->output, '\n');
                return Strings::concat(
                    "Error: curl failed to put file to ", url, " with exit code: ", res->exit_code, '\n');
            }

            return Strings::concat("Error: launching curl failed: ", maybe_res.error());
        }

        Command cmd;
        cmd.string_arg("curl").string_arg("-X").string_arg("PUT");
        for (auto&& header : headers)
        {
            cmd.string_arg("-H").string_arg(header);
        }
        cmd.string_arg("-w").string_arg(Strings::concat("\\n", guid_marker, "%{http_code}"));
        cmd.string_arg(url);
        cmd.string_arg("-T").string_arg(file);
        int code = 0;
        auto res = cmd_execute_and_stream_lines(cmd, [&code](StringView line) {
                       if (Strings::starts_with(line, guid_marker))
                       {
                           code = std::strtol(line.data() + guid_marker.size(), nullptr, 10);
                       }
                   }).map_error([](LocalizedString&& ls) { return ls.extract_data(); });
        if (auto pres = res.get())
        {
            if (*pres != 0 || (code >= 100 && code < 200) || code >= 300)
            {
                res = Strings::concat("Error: curl failed to put file to ",
                                      url,
                                      " with exit code '",
                                      *pres,
                                      "' and http code '",
                                      code,
                                      "'\n");
            }
        }

        return res;
    }

#if defined(_WIN32)
    namespace
    {
        /// <summary>
        /// Download a file using WinHTTP -- only supports HTTP and HTTPS
        /// </summary>
        static bool download_winhttp(Filesystem& fs,
                                     const Path& download_path_part_path,
                                     SplitURIView split_uri,
                                     const std::string& url,
                                     const std::vector<std::string>& secrets,
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

            const auto sanitized_url = replace_secrets(url, secrets);
            msg::write_unlocalized_text_to_stdout(Color::none, fmt::format("Downloading {}\n", sanitized_url));
            static auto s = WinHttpSession::make().value_or_exit(VCPKG_LINE_INFO);
            for (size_t trials = 0; trials < 4; ++trials)
            {
                if (trials > 0)
                {
                    // 1s, 2s, 4s
                    msg::write_unlocalized_text_to_stdout(
                        Color::none, fmt::format("Download failed -- retrying after {}ms.", 500 << trials));
                    std::this_thread::sleep_for(std::chrono::milliseconds(500 << trials));
                }
                auto conn = WinHttpConnection::make(s.m_hSession.get(), hostname, port);
                if (!conn)
                {
                    Strings::append(errors, sanitized_url, ": ", conn.error(), '\n');
                    continue;
                }
                auto req = WinHttpRequest::make(
                    conn.get()->m_hConnect.get(), split_uri.path_query_fragment, split_uri.scheme == "https");
                if (!req)
                {
                    Strings::append(errors, sanitized_url, ": ", req.error(), '\n');
                    continue;
                }

                auto maybe_status = req.get()->query_status();
                if (auto status = maybe_status.get())
                {
                    if (*status < 200 || *status >= 300)
                    {
                        Strings::append(errors, sanitized_url, ": failed: status code ", *status, '\n');
                        return false;
                    }
                }
                else
                {
                    Strings::append(errors, sanitized_url, ": ", maybe_status.error(), '\n');
                    continue;
                }

                const auto f = fs.open_for_write(download_path_part_path, VCPKG_LINE_INFO);

                auto forall_data =
                    req.get()->forall_data([&f](Span<char> span) { f.write(span.data(), 1, span.size()); });
                if (!forall_data)
                {
                    Strings::append(errors, sanitized_url, ": ", forall_data.error(), '\n');
                    continue;
                }
                return true;
            }
            return false;
        }
    }
#endif

    static bool try_download_file(vcpkg::Filesystem& fs,
                                  const std::string& url,
                                  View<std::string> headers,
                                  const Path& download_path,
                                  const Optional<std::string>& sha512,
                                  const std::vector<std::string>& secrets,
                                  std::string& errors,
                                  MessageSink& progress_sink)
    {
        auto download_path_part_path = download_path;
        download_path_part_path += ".";
#if defined(_WIN32)
        download_path_part_path += std::to_string(_getpid());
#else
        download_path_part_path += std::to_string(getpid());
#endif
        download_path_part_path += ".part";

#if defined(_WIN32) && 0
        if (headers.size() == 0)
        {
            auto split_uri = details::split_uri_view(url).value_or_exit(VCPKG_LINE_INFO);
            auto authority = split_uri.authority.value_or_exit(VCPKG_LINE_INFO).substr(2);
            if (split_uri.scheme == "https" || split_uri.scheme == "http")
            {
                // This check causes complex URLs (non-default port, embedded basic auth) to be passed down to curl.exe
                if (Strings::find_first_of(authority, ":@") == authority.end())
                {
                    if (download_winhttp(fs, download_path_part_path, split_uri, url, secrets, errors))
                    {
                        if (check_downloaded_file_hash(fs, sha512, url, download_path_part_path, errors))
                        {
                            fs.rename(download_path_part_path, download_path, VCPKG_LINE_INFO);
                            return true;
                        }
                    }
                    return false;
                }
            }
        }
#endif
        Command cmd;
        cmd.string_arg("curl")
            .string_arg("--fail")
            .string_arg("-L")
            .string_arg(url)
            .string_arg("--create-dirs")
            .string_arg("--output")
            .string_arg(download_path_part_path);
        for (auto&& header : headers)
        {
            cmd.string_arg("-H").string_arg(header);
        }

        std::string non_progress_lines;
        const auto maybe_exit_code = cmd_execute_and_stream_lines(
            cmd,
            [&](StringView line) {
                const auto maybe_parsed = try_parse_curl_progress_data(line);
                if (const auto parsed = maybe_parsed.get())
                {
                    progress_sink.print(Color::none, fmt::format("{}%\n", parsed->total_percent));
                }
                else
                {
                    non_progress_lines.append(line.data(), line.size());
                    non_progress_lines.push_back('\n');
                }
            },
            default_working_directory,
            default_environment,
            Encoding::Utf8);

        const auto sanitized_url = replace_secrets(url, secrets);
        if (const auto exit_code = maybe_exit_code.get())
        {
            if (*exit_code != 0)
            {
                Strings::append(errors, sanitized_url, ": ", non_progress_lines, '\n');
                return false;
            }

            if (check_downloaded_file_hash(fs, sha512, sanitized_url, download_path_part_path, errors))
            {
                fs.rename(download_path_part_path, download_path, VCPKG_LINE_INFO);
                return true;
            }
        }
        else
        {
            Strings::append(errors, sanitized_url, ": ", maybe_exit_code.error(), '\n');
        }

        return false;
    }

    static Optional<const std::string&> try_download_file(vcpkg::Filesystem& fs,
                                                          View<std::string> urls,
                                                          View<std::string> headers,
                                                          const Path& download_path,
                                                          const Optional<std::string>& sha512,
                                                          const std::vector<std::string>& secrets,
                                                          std::string& errors,
                                                          MessageSink& progress_sink)
    {
        for (auto&& url : urls)
        {
            if (try_download_file(fs, url, headers, download_path, sha512, secrets, errors, progress_sink)) return url;
        }
        return nullopt;
    }

    View<std::string> azure_blob_headers()
    {
        static std::string s_headers[2] = {"x-ms-version: 2020-04-08", "x-ms-blob-type: BlockBlob"};
        return s_headers;
    }

    void DownloadManager::download_file(Filesystem& fs,
                                        const std::string& url,
                                        View<std::string> headers,
                                        const Path& download_path,
                                        const Optional<std::string>& sha512,
                                        MessageSink& progress_sink) const
    {
        this->download_file(fs, View<std::string>(&url, 1), headers, download_path, sha512, progress_sink);
    }

    std::string DownloadManager::download_file(Filesystem& fs,
                                               View<std::string> urls,
                                               View<std::string> headers,
                                               const Path& download_path,
                                               const Optional<std::string>& sha512,
                                               MessageSink& progress_sink) const
    {
        std::string errors;
        if (urls.size() == 0)
        {
            if (auto hash = sha512.get())
            {
                Strings::append(errors, "Error: No urls specified to download SHA: ", *hash);
            }
            else
            {
                Strings::append(errors, "Error: No urls specified and no hash specified.");
            }
        }
        if (auto hash = sha512.get())
        {
            if (auto read_template = m_config.m_read_url_template.get())
            {
                auto read_url = Strings::replace_all(*read_template, "<SHA>", *hash);
                if (try_download_file(fs,
                                      read_url,
                                      m_config.m_read_headers,
                                      download_path,
                                      sha512,
                                      m_config.m_secrets,
                                      errors,
                                      progress_sink))
                {
                    return read_url;
                }
            }
            else if (auto script = m_config.m_script.get())
            {
                if (urls.size() != 0)
                {
                    const auto download_path_part_path =
                        download_path + Strings::concat(".", get_process_id(), ".part");

                    const auto escaped_url = Command(urls[0]).extract();
                    const auto escaped_sha512 = Command(*hash).extract();
                    const auto escaped_dpath = Command(download_path_part_path).extract();

                    auto cmd = api_stable_format(*script, [&](std::string& out, StringView key) {
                                   if (key == "url")
                                   {
                                       Strings::append(out, escaped_url);
                                   }
                                   else if (key == "sha512")
                                   {
                                       Strings::append(out, escaped_sha512);
                                   }
                                   else if (key == "dst")
                                   {
                                       Strings::append(out, escaped_dpath);
                                   }
                               }).value_or_exit(VCPKG_LINE_INFO);

                    auto maybe_res = flatten(cmd_execute_and_capture_output(Command{}.raw_arg(cmd),
                                                                            default_working_directory,
                                                                            get_clean_environment(),
                                                                            Encoding::Utf8,
                                                                            EchoInDebug::Show),
                                             "<mirror-script>");

                    if (maybe_res)
                    {
                        auto maybe_error =
                            try_verify_downloaded_file_hash(fs, "<mirror-script>", download_path_part_path, *hash);
                        if (auto err = maybe_error.get())
                        {
                            Strings::append(errors, *err);
                        }
                        else
                        {
                            fs.rename(download_path_part_path, download_path, VCPKG_LINE_INFO);
                            return urls[0];
                        }
                    }
                    else
                    {
                        Strings::append(errors, maybe_res.error(), '\n');
                    }
                }
            }
        }

        if (!m_config.m_block_origin)
        {
            if (urls.size() != 0)
            {
                auto maybe_url = try_download_file(
                    fs, urls, headers, download_path, sha512, m_config.m_secrets, errors, progress_sink);
                if (auto url = maybe_url.get())
                {
                    if (auto hash = sha512.get())
                    {
                        auto maybe_push = put_file_to_mirror(fs, download_path, *hash);
                        if (!maybe_push)
                        {
                            msg::println_warning(msgFailedToStoreBackToMirror);
                            msg::write_unlocalized_text_to_stdout(Color::warning, maybe_push.error());
                        }
                    }
                    return *url;
                }
            }
        }
        msg::println_error(msgFailedToDownloadFromMirrorSet);
        msg::println_error(LocalizedString::from_raw(errors));
        Checks::exit_fail(VCPKG_LINE_INFO);
    }

    ExpectedS<int> DownloadManager::put_file_to_mirror(const Filesystem& fs,
                                                       const Path& file_to_put,
                                                       StringView sha512) const
    {
        auto maybe_mirror_url = Strings::replace_all(m_config.m_write_url_template.value_or(""), "<SHA>", sha512);
        if (!maybe_mirror_url.empty())
        {
            return put_file(fs, maybe_mirror_url, m_config.m_write_headers, file_to_put);
        }
        return 0;
    }

    Optional<unsigned long long> try_parse_curl_max5_size(StringView sv)
    {
        // \d+(\.\d{1, 2})?[kMGTP]?
        std::size_t idx = 0;
        while (idx < sv.size() && ParserBase::is_ascii_digit(sv[idx]))
        {
            ++idx;
        }

        if (idx == 0)
        {
            return nullopt;
        }

        unsigned long long accumulator;
        {
            const auto maybe_first_digits = Strings::strto<unsigned long long>(sv.substr(0, idx));
            if (auto p = maybe_first_digits.get())
            {
                accumulator = *p;
            }
            else
            {
                return nullopt;
            }
        }

        unsigned long long after_digits = 0;
        if (idx < sv.size() && sv[idx] == '.')
        {
            ++idx;
            if (idx >= sv.size() || !ParserBase::is_ascii_digit(sv[idx]))
            {
                return nullopt;
            }

            after_digits = (sv[idx] - '0') * 10u;
            ++idx;
            if (idx < sv.size() && ParserBase::is_ascii_digit(sv[idx]))
            {
                after_digits += sv[idx] - '0';
                ++idx;
            }
        }

        if (idx == sv.size())
        {
            return accumulator;
        }

        if (idx + 1 != sv.size())
        {
            return nullopt;
        }

        switch (sv[idx])
        {
            case 'k': return (accumulator << 10) + (after_digits << 10) / 100;
            case 'M': return (accumulator << 20) + (after_digits << 20) / 100;
            case 'G': return (accumulator << 30) + (after_digits << 30) / 100;
            case 'T': return (accumulator << 40) + (after_digits << 40) / 100;
            case 'P': return (accumulator << 50) + (after_digits << 50) / 100;
            default: return nullopt;
        }
    }

    static bool parse_curl_uint_impl(unsigned int& target, const char*& first, const char* const last)
    {
        first = std::find_if_not(first, last, ParserBase::is_whitespace);
        const auto start = first;
        first = std::find_if(first, last, ParserBase::is_whitespace);
        const auto maybe_parsed = Strings::strto<unsigned int>(StringView{start, first});
        if (const auto parsed = maybe_parsed.get())
        {
            target = *parsed;
            return false;
        }

        return true;
    }

    static bool parse_curl_max5_impl(unsigned long long& target, const char*& first, const char* const last)
    {
        first = std::find_if_not(first, last, ParserBase::is_whitespace);
        const auto start = first;
        first = std::find_if(first, last, ParserBase::is_whitespace);
        const auto maybe_parsed = try_parse_curl_max5_size(StringView{start, first});
        if (const auto parsed = maybe_parsed.get())
        {
            target = *parsed;
            return false;
        }

        return true;
    }

    static bool skip_curl_time_impl(const char*& first, const char* const last)
    {
        first = std::find_if_not(first, last, ParserBase::is_whitespace);
        first = std::find_if(first, last, ParserBase::is_whitespace);
        return false;
    }

    Optional<CurlProgressData> try_parse_curl_progress_data(StringView curl_progress_line)
    {
        //  % Total    % Received % Xferd  Average Speed   Time    Time     Time  Current
        //                                 Dload  Upload   Total   Spent    Left  Speed
        // https://github.com/curl/curl/blob/5ccddf64398c1186deb5769dac086d738e150e09/lib/progress.c#L546
        CurlProgressData result;
        auto first = curl_progress_line.begin();
        const auto last = curl_progress_line.end();
        if (parse_curl_uint_impl(result.total_percent, first, last) ||
            parse_curl_max5_impl(result.total_size, first, last) ||
            parse_curl_uint_impl(result.recieved_percent, first, last) ||
            parse_curl_max5_impl(result.recieved_size, first, last) ||
            parse_curl_uint_impl(result.transfer_percent, first, last) ||
            parse_curl_max5_impl(result.transfer_size, first, last) ||
            parse_curl_max5_impl(result.average_download_speed, first, last) ||
            parse_curl_max5_impl(result.average_upload_speed, first, last) || skip_curl_time_impl(first, last) ||
            skip_curl_time_impl(first, last) || skip_curl_time_impl(first, last) ||
            parse_curl_max5_impl(result.current_speed, first, last))
        {
            return nullopt;
        }

        return result;
    }
}
