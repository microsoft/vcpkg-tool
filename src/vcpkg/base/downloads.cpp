#include <vcpkg/base/api-stable-format.h>
#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/downloads.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/hash.h>
#include <vcpkg/base/json.h>
#include <vcpkg/base/message_sinks.h>
#include <vcpkg/base/parse.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.h>
#include <vcpkg/base/system.process.h>
#include <vcpkg/base/system.proxy.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.version.h>

namespace vcpkg
{
    static std::string replace_secrets(std::string input, View<std::string> secrets)
    {
        const auto replacement = msg::format(msgSecretBanner);
        for (const auto& secret : secrets)
        {
            Strings::inplace_replace_all(input, secret, replacement);
        }

        return input;
    }

#if defined(_WIN32)
    struct WinHttpHandle
    {
        HINTERNET h;

        WinHttpHandle() : h(0) { }
        explicit WinHttpHandle(HINTERNET h_) : h(h_) { }
        WinHttpHandle(const WinHttpHandle&) = delete;
        WinHttpHandle(WinHttpHandle&& other) : h(other.h) { other.h = 0; }
        WinHttpHandle& operator=(const WinHttpHandle&) = delete;
        WinHttpHandle& operator=(WinHttpHandle&& other)
        {
            auto cpy = std::move(other);
            std::swap(h, cpy.h);
            return *this;
        }

        ~WinHttpHandle()
        {
            if (h)
            {
                WinHttpCloseHandle(h);
            }
        }
    };

    static LocalizedString format_winhttp_last_error_message(StringLiteral api_name, StringView url, DWORD last_error)
    {
        return msg::format_error(
            msgDownloadWinHttpError, msg::system_api = api_name, msg::exit_code = last_error, msg::url = url);
    }

    static LocalizedString format_winhttp_last_error_message(StringLiteral api_name, StringView url)
    {
        return format_winhttp_last_error_message(api_name, url, GetLastError());
    }

    static void maybe_emit_winhttp_progress(const Optional<unsigned long long>& maybe_content_length,
                                            std::chrono::steady_clock::time_point& last_write,
                                            unsigned long long total_downloaded_size,
                                            MessageSink& progress_sink)
    {
        if (const auto content_length = maybe_content_length.get())
        {
            const auto now = std::chrono::steady_clock::now();
            if ((now - last_write) >= std::chrono::milliseconds(100))
            {
                const double percent =
                    (static_cast<double>(total_downloaded_size) / static_cast<double>(*content_length)) * 100;
                progress_sink.print(Color::none, fmt::format("{:.2f}%\n", percent));
                last_write = now;
            }
        }
    }

    struct WinHttpRequest
    {
        static ExpectedL<WinHttpRequest> make(HINTERNET hConnect,
                                              StringView url_path,
                                              StringView sanitized_url,
                                              bool https,
                                              const wchar_t* method = L"GET")
        {
            WinHttpRequest ret;
            ret.m_sanitized_url.assign(sanitized_url.data(), sanitized_url.size());
            // Create an HTTP request handle.
            {
                auto h = WinHttpOpenRequest(hConnect,
                                            method,
                                            Strings::to_utf16(url_path).c_str(),
                                            nullptr,
                                            WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            https ? WINHTTP_FLAG_SECURE : 0);
                if (!h)
                {
                    return format_winhttp_last_error_message("WinHttpOpenRequest", sanitized_url);
                }

                ret.m_hRequest = WinHttpHandle{h};
            }

            // Send a request.
            auto bResults = WinHttpSendRequest(
                ret.m_hRequest.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);

            if (!bResults)
            {
                return format_winhttp_last_error_message("WinHttpSendRequest", sanitized_url);
            }

            // End the request.
            bResults = WinHttpReceiveResponse(ret.m_hRequest.h, NULL);
            if (!bResults)
            {
                return format_winhttp_last_error_message("WinHttpReceiveResponse", sanitized_url);
            }

            return ret;
        }

        ExpectedL<int> query_status() const
        {
            DWORD status_code;
            DWORD size = sizeof(status_code);

            auto succeeded = WinHttpQueryHeaders(m_hRequest.h,
                                                 WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                                 WINHTTP_HEADER_NAME_BY_INDEX,
                                                 &status_code,
                                                 &size,
                                                 WINHTTP_NO_HEADER_INDEX);
            if (succeeded)
            {
                return status_code;
            }

            return format_winhttp_last_error_message("WinHttpQueryHeaders", m_sanitized_url);
        }

        ExpectedL<Optional<unsigned long long>> query_content_length() const
        {
            static constexpr DWORD buff_characters = 21; // 18446744073709551615
            wchar_t buff[buff_characters];
            DWORD size = sizeof(buff);
            auto succeeded = WinHttpQueryHeaders(m_hRequest.h,
                                                 WINHTTP_QUERY_CONTENT_LENGTH,
                                                 WINHTTP_HEADER_NAME_BY_INDEX,
                                                 buff,
                                                 &size,
                                                 WINHTTP_NO_HEADER_INDEX);
            if (succeeded)
            {
                return Strings::strto<unsigned long long>(Strings::to_utf8(buff, size >> 1));
            }

            const DWORD last_error = GetLastError();
            if (last_error == ERROR_WINHTTP_HEADER_NOT_FOUND)
            {
                return Optional<unsigned long long>{nullopt};
            }

            return format_winhttp_last_error_message("WinHttpQueryHeaders", m_sanitized_url, last_error);
        }

        ExpectedL<Unit> write_response_body(WriteFilePointer& file, MessageSink& progress_sink)
        {
            static constexpr DWORD buff_size = 65535;
            std::unique_ptr<char[]> buff{new char[buff_size]};
            Optional<unsigned long long> maybe_content_length;
            auto last_write = std::chrono::steady_clock::now();

            {
                auto maybe_maybe_content_length = query_content_length();
                if (const auto p = maybe_maybe_content_length.get())
                {
                    maybe_content_length = *p;
                }
                else
                {
                    return std::move(maybe_maybe_content_length).error();
                }
            }

            unsigned long long total_downloaded_size = 0;
            for (;;)
            {
                DWORD this_read;
                if (!WinHttpReadData(m_hRequest.h, buff.get(), buff_size, &this_read))
                {
                    return format_winhttp_last_error_message("WinHttpReadData", m_sanitized_url);
                }

                if (this_read == 0)
                {
                    return Unit{};
                }

                do
                {
                    const auto this_write = static_cast<DWORD>(file.write(buff.get(), 1, this_read));
                    if (this_write == 0)
                    {
                        return format_winhttp_last_error_message("fwrite", m_sanitized_url);
                    }

                    maybe_emit_winhttp_progress(maybe_content_length, last_write, total_downloaded_size, progress_sink);
                    this_read -= this_write;
                    total_downloaded_size += this_write;
                } while (this_read > 0);
            }
        }

        WinHttpHandle m_hRequest;
        std::string m_sanitized_url;
    };

    struct WinHttpSession
    {
        static ExpectedL<WinHttpSession> make(StringView sanitized_url)
        {
            WinHttpSession ret;
            {
                auto h = WinHttpOpen(
                    L"vcpkg/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
                if (!h)
                {
                    return format_winhttp_last_error_message("WinHttpOpen", sanitized_url);
                }

                ret.m_hSession = WinHttpHandle{h};
            }

            // Increase default timeouts to help connections behind proxies
            // WinHttpSetTimeouts(HINTERNET hInternet, int nResolveTimeout, int nConnectTimeout, int nSendTimeout, int
            // nReceiveTimeout);
            WinHttpSetTimeouts(ret.m_hSession.h, 0, 120000, 120000, 120000);

            // If the environment variable HTTPS_PROXY is set
            // use that variable as proxy. This situation might exist when user is in a company network
            // with restricted network/proxy settings
            auto maybe_https_proxy_env = get_environment_variable(EnvironmentVariableHttpsProxy);
            if (auto p_https_proxy = maybe_https_proxy_env.get())
            {
                std::wstring env_proxy_settings = Strings::to_utf16(*p_https_proxy);
                WINHTTP_PROXY_INFO proxy;
                proxy.dwAccessType = WINHTTP_ACCESS_TYPE_NAMED_PROXY;
                proxy.lpszProxy = env_proxy_settings.data();
                proxy.lpszProxyBypass = nullptr;

                WinHttpSetOption(ret.m_hSession.h, WINHTTP_OPTION_PROXY, &proxy, sizeof(proxy));
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
                    WinHttpSetOption(ret.m_hSession.h, WINHTTP_OPTION_PROXY, &proxy, sizeof(proxy));
                }
            }

            // Use Windows 10 defaults on Windows 7
            DWORD secure_protocols(WINHTTP_FLAG_SECURE_PROTOCOL_TLS1 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1 |
                                   WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2);
            WinHttpSetOption(
                ret.m_hSession.h, WINHTTP_OPTION_SECURE_PROTOCOLS, &secure_protocols, sizeof(secure_protocols));

            // Many open source mirrors such as https://download.gnome.org/ will redirect to http mirrors.
            // `curl.exe -L` does follow https -> http redirection.
            // Additionally, vcpkg hash checks the resulting archive.
            DWORD redirect_policy(WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS);
            WinHttpSetOption(
                ret.m_hSession.h, WINHTTP_OPTION_REDIRECT_POLICY, &redirect_policy, sizeof(redirect_policy));

            return ret;
        }

        WinHttpHandle m_hSession;
    };

    struct WinHttpConnection
    {
        static ExpectedL<WinHttpConnection> make(HINTERNET hSession,
                                                 StringView hostname,
                                                 INTERNET_PORT port,
                                                 StringView sanitized_url)
        {
            // Specify an HTTP server.
            auto h = WinHttpConnect(hSession, Strings::to_utf16(hostname).c_str(), port, 0);
            if (!h)
            {
                return format_winhttp_last_error_message("WinHttpConnect", sanitized_url);
            }

            return WinHttpConnection{WinHttpHandle{h}};
        }

        WinHttpHandle m_hConnect;
    };
#endif

    ExpectedL<SplitURIView> split_uri_view(StringView uri)
    {
        auto sep = std::find(uri.begin(), uri.end(), ':');
        if (sep == uri.end()) return msg::format_error(msgInvalidUri, msg::value = uri);

        StringView scheme(uri.begin(), sep);
        if (Strings::starts_with({sep + 1, uri.end()}, "//"))
        {
            auto path_start = std::find(sep + 3, uri.end(), '/');
            return SplitURIView{scheme, StringView{sep + 1, path_start}, {path_start, uri.end()}};
        }
        // no authority
        return SplitURIView{scheme, {}, {sep + 1, uri.end()}};
    }

    static ExpectedL<Unit> try_verify_downloaded_file_hash(const ReadOnlyFilesystem& fs,
                                                           StringView sanitized_url,
                                                           const Path& downloaded_path,
                                                           StringView sha512)
    {
        std::string actual_hash =
            vcpkg::Hash::get_file_hash(fs, downloaded_path, Hash::Algorithm::Sha512).value_or_exit(VCPKG_LINE_INFO);
        if (!Strings::case_insensitive_ascii_equals(sha512, actual_hash))
        {
            return msg::format_error(msgDownloadFailedHashMismatch,
                                     msg::url = sanitized_url,
                                     msg::path = downloaded_path,
                                     msg::expected = sha512,
                                     msg::actual = actual_hash);
        }

        return Unit{};
    }

    void verify_downloaded_file_hash(const ReadOnlyFilesystem& fs,
                                     StringView url,
                                     const Path& downloaded_path,
                                     StringView sha512)
    {
        try_verify_downloaded_file_hash(fs, url, downloaded_path, sha512).value_or_exit(VCPKG_LINE_INFO);
    }

    static ExpectedL<Unit> check_downloaded_file_hash(const ReadOnlyFilesystem& fs,
                                                      const Optional<std::string>& hash,
                                                      StringView sanitized_url,
                                                      const Path& download_part_path)
    {
        if (auto p = hash.get())
        {
            return try_verify_downloaded_file_hash(fs, sanitized_url, download_part_path, *p);
        }

        Debug::println("Skipping hash check because none was specified.");
        return Unit{};
    }

    static std::vector<int> curl_bulk_operation(View<Command> operation_args,
                                                StringLiteral prefixArgs,
                                                View<std::string> headers,
                                                View<std::string> secrets)
    {
#define GUID_MARKER "5ec47b8e-6776-4d70-b9b3-ac2a57bc0a1c"
        static constexpr StringLiteral guid_marker = GUID_MARKER;
        Command prefix_cmd{"curl"};
        if (!prefixArgs.empty())
        {
            prefix_cmd.raw_arg(prefixArgs);
        }

        prefix_cmd.string_arg("-L").string_arg("-w").string_arg(GUID_MARKER "%{http_code}\\n");
#undef GUID_MARKER

        std::vector<int> ret;
        ret.reserve(operation_args.size());

        for (auto&& header : headers)
        {
            prefix_cmd.string_arg("-H").string_arg(header);
        }

        static constexpr auto initial_timeout_delay_ms = 100;
        auto timeout_delay_ms = initial_timeout_delay_ms;
        static constexpr auto maximum_timeout_delay_ms = 100000;

        while (ret.size() != operation_args.size())
        {
            // there's an edge case that we aren't handling here where not even one operation fits with the configured
            // headers but this seems unlikely

            // form a maximum length command line of operations:
            auto batch_cmd = prefix_cmd;
            size_t last_try_op = ret.size();
            while (last_try_op != operation_args.size() && batch_cmd.try_append(operation_args[last_try_op]))
            {
                ++last_try_op;
            }

            // actually run curl
            auto this_batch_result = cmd_execute_and_capture_output(batch_cmd).value_or_exit(VCPKG_LINE_INFO);
            if (this_batch_result.exit_code != 0)
            {
                Checks::msg_exit_with_error(VCPKG_LINE_INFO,
                                            msgCommandFailedCode,
                                            msg::command_line =
                                                replace_secrets(std::move(batch_cmd).extract(), secrets),
                                            msg::exit_code = this_batch_result.exit_code);
            }

            // extract HTTP response codes
            for (auto&& line : Strings::split(this_batch_result.output, '\n'))
            {
                if (Strings::starts_with(line, guid_marker))
                {
                    ret.push_back(static_cast<int>(std::strtol(line.data() + guid_marker.size(), nullptr, 10)));
                }
            }

            // check if we got a partial response, and, if so, issue a timed delay
            if (ret.size() == last_try_op)
            {
                timeout_delay_ms = initial_timeout_delay_ms;
            }
            else
            {
                // curl stopped before finishing all operations; retry after some time
                if (timeout_delay_ms >= maximum_timeout_delay_ms)
                {
                    Checks::msg_exit_with_error(VCPKG_LINE_INFO,
                                                msgCurlTimeout,
                                                msg::command_line =
                                                    replace_secrets(std::move(batch_cmd).extract(), secrets));
                }

                msg::println_warning(msgCurlResponseTruncatedRetrying, msg::value = timeout_delay_ms);
                std::this_thread::sleep_for(std::chrono::milliseconds(timeout_delay_ms));
                timeout_delay_ms *= 10;
            }
        }

        return ret;
    }

    std::vector<int> url_heads(View<std::string> urls, View<std::string> headers, View<std::string> secrets)
    {
        return curl_bulk_operation(
            Util::fmap(urls, [](const std::string& url) { return Command{}.string_arg(url_encode_spaces(url)); }),
            "--head",
            headers,
            secrets);
    }

    std::vector<int> download_files(View<std::pair<std::string, Path>> url_pairs,
                                    View<std::string> headers,
                                    View<std::string> secrets)
    {
        return curl_bulk_operation(Util::fmap(url_pairs,
                                              [](const std::pair<std::string, Path>& url_pair) {
                                                  return Command{}
                                                      .string_arg(url_encode_spaces(url_pair.first))
                                                      .string_arg("-o")
                                                      .string_arg(url_pair.second);
                                              }),
                                   "--create-dirs",
                                   headers,
                                   secrets);
    }

    bool send_snapshot_to_api(const std::string& github_token,
                              const std::string& github_repository,
                              const Json::Object& snapshot)
    {
        static constexpr StringLiteral guid_marker = "fcfad8a3-bb68-4a54-ad00-dab1ff671ed2";

        auto cmd = Command{"curl"};
        cmd.string_arg("-w").string_arg("\\n" + guid_marker.to_string() + "%{http_code}");
        cmd.string_arg("-X").string_arg("POST");
        cmd.string_arg("-H").string_arg("Accept: application/vnd.github+json");

        std::string res = "Authorization: Bearer " + github_token;
        cmd.string_arg("-H").string_arg(res);
        cmd.string_arg("-H").string_arg("X-GitHub-Api-Version: 2022-11-28");
        cmd.string_arg(Strings::concat(
            "https://api.github.com/repos/", url_encode_spaces(github_repository), "/dependency-graph/snapshots"));
        cmd.string_arg("-d").string_arg("@-");

        RedirectedProcessLaunchSettings settings;
        settings.stdin_content = Json::stringify(snapshot);
        int code = 0;
        auto result = cmd_execute_and_stream_lines(cmd, settings, [&code](StringView line) {
            if (Strings::starts_with(line, guid_marker))
            {
                code = std::strtol(line.data() + guid_marker.size(), nullptr, 10);
            }
            else
            {
                Debug::println(line);
            }
        });

        auto r = result.get();
        if (r && *r == 0 && code >= 200 && code < 300)
        {
            return true;
        }
        return false;
    }

    ExpectedL<int> put_file(const ReadOnlyFilesystem&,
                            StringView url,
                            const std::vector<std::string>& secrets,
                            View<std::string> headers,
                            const Path& file,
                            StringView method)
    {
        static constexpr StringLiteral guid_marker = "9a1db05f-a65d-419b-aa72-037fb4d0672e";

        if (Strings::starts_with(url, "ftp://"))
        {
            // HTTP headers are ignored for FTP clients
            auto ftp_cmd = Command{"curl"};
            ftp_cmd.string_arg(url_encode_spaces(url));
            ftp_cmd.string_arg("-T").string_arg(file);
            auto maybe_res = cmd_execute_and_capture_output(ftp_cmd);
            if (auto res = maybe_res.get())
            {
                if (res->exit_code == 0)
                {
                    return 0;
                }

                Debug::print(res->output, '\n');
                return msg::format_error(msgCurlFailedToPut,
                                         msg::exit_code = res->exit_code,
                                         msg::url = replace_secrets(url.to_string(), secrets));
            }

            return std::move(maybe_res).error();
        }

        auto http_cmd = Command{"curl"}.string_arg("-X").string_arg(method);
        for (auto&& header : headers)
        {
            http_cmd.string_arg("-H").string_arg(header);
        }

        http_cmd.string_arg("-w").string_arg("\\n" + guid_marker.to_string() + "%{http_code}");
        http_cmd.string_arg(url);
        http_cmd.string_arg("-T").string_arg(file);
        int code = 0;
        auto res = cmd_execute_and_stream_lines(http_cmd, [&code](StringView line) {
            if (Strings::starts_with(line, guid_marker))
            {
                code = std::strtol(line.data() + guid_marker.size(), nullptr, 10);
            }
        });

        if (auto pres = res.get())
        {
            if (*pres != 0 || (code >= 100 && code < 200) || code >= 300)
            {
                return msg::format_error(
                    msgCurlFailedToPutHttp, msg::exit_code = *pres, msg::url = url, msg::value = code);
            }
        }

        return res;
    }

    std::string format_url_query(StringView base_url, View<std::string> query_params)
    {
        auto url = base_url.to_string();
        if (query_params.empty())
        {
            return url;
        }

        return url + "?" + Strings::join("&", query_params);
    }

    ExpectedL<std::string> invoke_http_request(StringView method,
                                               View<std::string> headers,
                                               StringView url,
                                               StringView data)
    {
        auto cmd = Command{"curl"}.string_arg("-s").string_arg("-L");
        cmd.string_arg("-H").string_arg(
            fmt::format("User-Agent: vcpkg/{}-{} (curl)", VCPKG_BASE_VERSION_AS_STRING, VCPKG_VERSION_AS_STRING));

        for (auto&& header : headers)
        {
            cmd.string_arg("-H").string_arg(header);
        }

        cmd.string_arg("-X").string_arg(method);

        if (!data.empty())
        {
            cmd.string_arg("--data-raw").string_arg(data);
        }

        cmd.string_arg(url_encode_spaces(url));

        return flatten_out(cmd_execute_and_capture_output(cmd), "curl");
    }

#if defined(_WIN32)
    enum class WinHttpTrialResult
    {
        failed,
        succeeded,
        retry
    };

    static WinHttpTrialResult download_winhttp_trial(const Filesystem& fs,
                                                     WinHttpSession& s,
                                                     const Path& download_path_part_path,
                                                     SplitURIView split_uri,
                                                     StringView hostname,
                                                     INTERNET_PORT port,
                                                     StringView sanitized_url,
                                                     std::vector<LocalizedString>& errors,
                                                     MessageSink& progress_sink)
    {
        auto maybe_conn = WinHttpConnection::make(s.m_hSession.h, hostname, port, sanitized_url);
        const auto conn = maybe_conn.get();
        if (!conn)
        {
            errors.push_back(std::move(maybe_conn).error());
            return WinHttpTrialResult::retry;
        }

        auto maybe_req = WinHttpRequest::make(
            conn->m_hConnect.h, split_uri.path_query_fragment, sanitized_url, split_uri.scheme == "https");
        const auto req = maybe_req.get();
        if (!req)
        {
            errors.push_back(std::move(maybe_req).error());
            return WinHttpTrialResult::retry;
        }

        auto maybe_status = req->query_status();
        const auto status = maybe_status.get();
        if (!status)
        {
            errors.push_back(std::move(maybe_status).error());
            return WinHttpTrialResult::retry;
        }

        if (*status < 200 || *status >= 300)
        {
            errors.push_back(
                msg::format_error(msgDownloadFailedStatusCode, msg::url = sanitized_url, msg::value = *status));
            return WinHttpTrialResult::failed;
        }

        auto f = fs.open_for_write(download_path_part_path, VCPKG_LINE_INFO);
        auto maybe_write = req->write_response_body(f, progress_sink);
        const auto write = maybe_write.get();
        if (!write)
        {
            errors.push_back(std::move(maybe_write).error());
            return WinHttpTrialResult::retry;
        }

        return WinHttpTrialResult::succeeded;
    }

    /// <summary>
    /// Download a file using WinHTTP -- only supports HTTP and HTTPS
    /// </summary>
    static bool download_winhttp(const Filesystem& fs,
                                 const Path& download_path_part_path,
                                 SplitURIView split_uri,
                                 const std::string& url,
                                 const std::vector<std::string>& secrets,
                                 std::vector<LocalizedString>& errors,
                                 MessageSink& progress_sink)
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
        msg::println(msgDownloadingUrl, msg::url = sanitized_url);
        static auto s = WinHttpSession::make(sanitized_url).value_or_exit(VCPKG_LINE_INFO);
        for (size_t trials = 0; trials < 4; ++trials)
        {
            if (trials > 0)
            {
                // 1s, 2s, 4s
                const auto trialMs = 500 << trials;
                msg::println_warning(msgDownloadFailedRetrying, msg::value = trialMs);
                std::this_thread::sleep_for(std::chrono::milliseconds(trialMs));
            }

            switch (download_winhttp_trial(
                fs, s, download_path_part_path, split_uri, hostname, port, sanitized_url, errors, progress_sink))
            {
                case WinHttpTrialResult::failed: return false;
                case WinHttpTrialResult::succeeded: return true;
                case WinHttpTrialResult::retry: break;
            }
        }

        return false;
    }
#endif

    static bool try_download_file(const Filesystem& fs,
                                  const std::string& url,
                                  View<std::string> headers,
                                  const Path& download_path,
                                  const Optional<std::string>& sha512,
                                  const std::vector<std::string>& secrets,
                                  std::vector<LocalizedString>& errors,
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

#if defined(_WIN32)
        if (headers.size() == 0)
        {
            auto split_uri = split_uri_view(url).value_or_exit(VCPKG_LINE_INFO);
            auto authority = split_uri.authority.value_or_exit(VCPKG_LINE_INFO).substr(2);
            if (split_uri.scheme == "https" || split_uri.scheme == "http")
            {
                // This check causes complex URLs (non-default port, embedded basic auth) to be passed down to curl.exe
                if (Strings::find_first_of(authority, ":@") == authority.end())
                {
                    if (download_winhttp(fs, download_path_part_path, split_uri, url, secrets, errors, progress_sink))
                    {
                        auto maybe_hash_check = check_downloaded_file_hash(fs, sha512, url, download_path_part_path);
                        if (maybe_hash_check.has_value())
                        {
                            fs.rename(download_path_part_path, download_path, VCPKG_LINE_INFO);
                            return true;
                        }
                        else
                        {
                            errors.push_back(std::move(maybe_hash_check).error());
                        }
                    }
                    return false;
                }
            }
        }
#endif
        auto cmd = Command{"curl"}
                       .string_arg("--fail")
                       .string_arg("-L")
                       .string_arg(url_encode_spaces(url))
                       .string_arg("--create-dirs")
                       .string_arg("--output")
                       .string_arg(download_path_part_path);
        for (auto&& header : headers)
        {
            cmd.string_arg("-H").string_arg(header);
        }

        std::string non_progress_content;
        auto maybe_exit_code = cmd_execute_and_stream_lines(cmd, [&](StringView line) {
            const auto maybe_parsed = try_parse_curl_progress_data(line);
            if (const auto parsed = maybe_parsed.get())
            {
                progress_sink.print(Color::none, fmt::format("{}%\n", parsed->total_percent));
            }
            else
            {
                non_progress_content.append(line.data(), line.size());
                non_progress_content.push_back('\n');
            }
        });

        const auto sanitized_url = replace_secrets(url, secrets);
        if (const auto exit_code = maybe_exit_code.get())
        {
            if (*exit_code != 0)
            {
                errors.push_back(
                    msg::format_error(msgDownloadFailedCurl, msg::url = sanitized_url, msg::exit_code = *exit_code)
                        .append_raw('\n')
                        .append_raw(non_progress_content));
                return false;
            }

            auto maybe_hash_check = check_downloaded_file_hash(fs, sha512, sanitized_url, download_path_part_path);
            if (maybe_hash_check.has_value())
            {
                fs.rename(download_path_part_path, download_path, VCPKG_LINE_INFO);
                return true;
            }
            else
            {
                errors.push_back(std::move(maybe_hash_check).error());
            }
        }
        else
        {
            errors.push_back(std::move(maybe_exit_code).error());
        }

        return false;
    }

    static Optional<const std::string&> try_download_file(const Filesystem& fs,
                                                          View<std::string> urls,
                                                          View<std::string> headers,
                                                          const Path& download_path,
                                                          const Optional<std::string>& sha512,
                                                          const std::vector<std::string>& secrets,
                                                          std::vector<LocalizedString>& errors,
                                                          MessageSink& progress_sink)
    {
        for (auto&& url : urls)
        {
            if (try_download_file(fs, url, headers, download_path, sha512, secrets, errors, progress_sink))
            {
                return url;
            }
        }

        return nullopt;
    }

    View<std::string> azure_blob_headers()
    {
        static std::string s_headers[2] = {"x-ms-version: 2020-04-08", "x-ms-blob-type: BlockBlob"};
        return s_headers;
    }

    void DownloadManager::download_file(const Filesystem& fs,
                                        const std::string& url,
                                        View<std::string> headers,
                                        const Path& download_path,
                                        const Optional<std::string>& sha512,
                                        MessageSink& progress_sink) const
    {
        this->download_file(fs, View<std::string>(&url, 1), headers, download_path, sha512, progress_sink);
    }

    std::string DownloadManager::download_file(const Filesystem& fs,
                                               View<std::string> urls,
                                               View<std::string> headers,
                                               const Path& download_path,
                                               const Optional<std::string>& sha512,
                                               MessageSink& progress_sink) const
    {
        std::vector<LocalizedString> errors;
        if (urls.size() == 0)
        {
            if (auto hash = sha512.get())
            {
                errors.push_back(msg::format_error(msgNoUrlsAndHashSpecified, msg::sha = *hash));
            }
            else
            {
                errors.push_back(msg::format_error(msgNoUrlsAndNoHashSpecified));
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
                    const auto download_path_part_path = download_path + fmt::format(".{}.part", get_process_id());
                    const auto escaped_url = Command(urls[0]).extract();
                    const auto escaped_sha512 = Command(*hash).extract();
                    const auto escaped_dpath = Command(download_path_part_path).extract();
                    Command cmd;
                    cmd.raw_arg(api_stable_format(*script, [&](std::string& out, StringView key) {
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
                                }).value_or_exit(VCPKG_LINE_INFO));

                    RedirectedProcessLaunchSettings settings;
                    settings.environment = get_clean_environment();
                    settings.echo_in_debug = EchoInDebug::Show;
                    auto maybe_res = flatten(cmd_execute_and_capture_output(cmd, settings), "<mirror-script>");
                    if (maybe_res)
                    {
                        auto maybe_success =
                            try_verify_downloaded_file_hash(fs, "<mirror-script>", download_path_part_path, *hash);
                        if (maybe_success)
                        {
                            fs.rename(download_path_part_path, download_path, VCPKG_LINE_INFO);
                            return urls[0];
                        }

                        errors.push_back(std::move(maybe_success).error());
                    }
                    else
                    {
                        errors.push_back(std::move(maybe_res).error());
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
                            msg::println(maybe_push.error());
                        }
                    }

                    return *url;
                }
            }
        }
        msg::println_error(msgFailedToDownloadFromMirrorSet);
        for (LocalizedString& error : errors)
        {
            msg::println(error);
        }

        Checks::exit_fail(VCPKG_LINE_INFO);
    }

    ExpectedL<int> DownloadManager::put_file_to_mirror(const ReadOnlyFilesystem& fs,
                                                       const Path& file_to_put,
                                                       StringView sha512) const
    {
        auto maybe_mirror_url = Strings::replace_all(m_config.m_write_url_template.value_or(""), "<SHA>", sha512);
        if (!maybe_mirror_url.empty())
        {
            return put_file(fs, maybe_mirror_url, m_config.m_secrets, m_config.m_write_headers, file_to_put);
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
        // Curl's maintainer Daniel Stenberg clarified that this output is semi-contractual
        // here: https://twitter.com/bagder/status/1600615752725307400
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

    std::string url_encode_spaces(StringView url) { return Strings::replace_all(url, StringLiteral{" "}, "%20"); }
}
