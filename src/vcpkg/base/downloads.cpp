#include <vcpkg/base/api-stable-format.h>
#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/downloads.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/hash.h>
#include <vcpkg/base/json.h>
#include <vcpkg/base/message_sinks.h>
#include <vcpkg/base/parse.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/stringview.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.h>
#include <vcpkg/base/system.process.h>
#include <vcpkg/base/system.proxy.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.version.h>

using namespace vcpkg;

namespace
{
    constexpr StringLiteral vcpkg_curl_user_agent_header =
        "User-Agent: vcpkg/" VCPKG_BASE_VERSION_AS_STRING "-" VCPKG_VERSION_AS_STRING " (curl)";

    void add_curl_headers(Command& cmd, View<std::string> headers)
    {
        cmd.string_arg("-H").string_arg(vcpkg_curl_user_agent_header);
        for (auto&& header : headers)
        {
            cmd.string_arg("-H").string_arg(header);
        }
    }

    std::string replace_secrets(std::string input, View<std::string> secrets)
    {
        const auto replacement = msg::format(msgSecretBanner);
        for (const auto& secret : secrets)
        {
            Strings::inplace_replace_all(input, secret, replacement);
        }

        return input;
    }
}

namespace vcpkg
{
#if defined(_WIN32)
    static LocalizedString format_winhttp_last_error_message(StringLiteral api_name, StringView url, DWORD last_error)
    {
        return msg::format(
            msgDownloadWinHttpError, msg::system_api = api_name, msg::exit_code = last_error, msg::url = url);
    }

    static LocalizedString format_winhttp_last_error_message(StringLiteral api_name, StringView url)
    {
        return format_winhttp_last_error_message(api_name, url, GetLastError());
    }

    static void maybe_emit_winhttp_progress(MessageSink& machine_readable_progress,
                                            const Optional<unsigned long long>& maybe_content_length,
                                            std::chrono::steady_clock::time_point& last_write,
                                            unsigned long long total_downloaded_size)
    {
        if (const auto content_length = maybe_content_length.get())
        {
            const auto now = std::chrono::steady_clock::now();
            if ((now - last_write) >= std::chrono::milliseconds(100))
            {
                const double percent =
                    (static_cast<double>(total_downloaded_size) / static_cast<double>(*content_length)) * 100;
                machine_readable_progress.println(LocalizedString::from_raw(fmt::format("{:.2f}%", percent)));
                last_write = now;
            }
        }
    }

    struct WinHttpHandle
    {
        WinHttpHandle() = default;
        WinHttpHandle(const WinHttpHandle&) = delete;
        WinHttpHandle& operator=(const WinHttpHandle&) = delete;

        void require_null_handle() const
        {
            if (h)
            {
                Checks::unreachable(VCPKG_LINE_INFO, "WinHTTP handle type confusion");
            }
        }

        void require_created_handle() const
        {
            if (!h)
            {
                Checks::unreachable(VCPKG_LINE_INFO, "WinHTTP handle not created");
            }
        }

        bool Connect(DiagnosticContext& context,
                     StringView sanitized_url,
                     const WinHttpHandle& session,
                     StringView hostname,
                     INTERNET_PORT port)
        {
            require_null_handle();
            session.require_created_handle();
            h = WinHttpConnect(session.h, Strings::to_utf16(hostname).c_str(), port, 0);
            if (h)
            {
                return true;
            }

            context.report_error(format_winhttp_last_error_message("WinHttpConnect", sanitized_url));
            return false;
        }

        bool Open(DiagnosticContext& context,
                  StringView sanitized_url,
                  _In_opt_z_ LPCWSTR pszAgentW,
                  _In_ DWORD dwAccessType,
                  _In_opt_z_ LPCWSTR pszProxyW,
                  _In_opt_z_ LPCWSTR pszProxyBypassW,
                  _In_ DWORD dwFlags)
        {
            require_null_handle();
            h = WinHttpOpen(pszAgentW, dwAccessType, pszProxyW, pszProxyBypassW, dwFlags);
            if (h)
            {
                return true;
            }

            context.report_error(format_winhttp_last_error_message("WinHttpOpen", sanitized_url));
            return false;
        }

        bool OpenRequest(DiagnosticContext& context,
                         const WinHttpHandle& hConnect,
                         StringView sanitized_url,
                         IN LPCWSTR pwszVerb,
                         StringView path_query_fragment,
                         IN LPCWSTR pwszVersion,
                         IN LPCWSTR pwszReferrer OPTIONAL,
                         IN LPCWSTR FAR* ppwszAcceptTypes OPTIONAL,
                         IN DWORD dwFlags)
        {
            require_null_handle();
            h = WinHttpOpenRequest(hConnect.h,
                                   pwszVerb,
                                   Strings::to_utf16(path_query_fragment).c_str(),
                                   pwszVersion,
                                   pwszReferrer,
                                   ppwszAcceptTypes,
                                   dwFlags);
            if (h)
            {
                return true;
            }

            context.report_error(format_winhttp_last_error_message("WinHttpOpenRequest", sanitized_url));
            return false;
        }

        bool SendRequest(DiagnosticContext& context,
                         StringView sanitized_url,
                         _In_reads_opt_(dwHeadersLength) LPCWSTR lpszHeaders,
                         IN DWORD dwHeadersLength,
                         _In_reads_bytes_opt_(dwOptionalLength) LPVOID lpOptional,
                         IN DWORD dwOptionalLength,
                         IN DWORD dwTotalLength,
                         IN DWORD_PTR dwContext) const
        {
            require_created_handle();
            if (WinHttpSendRequest(
                    h, lpszHeaders, dwHeadersLength, lpOptional, dwOptionalLength, dwTotalLength, dwContext))
            {
                return true;
            }

            context.report_error(format_winhttp_last_error_message("WinHttpSendRequest", sanitized_url));
            return false;
        }

        bool ReceiveResponse(DiagnosticContext& context, StringView sanitized_url)
        {
            require_created_handle();
            if (WinHttpReceiveResponse(h, NULL))
            {
                return true;
            }

            context.report_error(format_winhttp_last_error_message("WinHttpReceiveResponse", sanitized_url));
            return false;
        }

        bool SetTimeouts(DiagnosticContext& context,
                         StringView sanitized_url,
                         int nResolveTimeout,
                         int nConnectTimeout,
                         int nSendTimeout,
                         int nReceiveTimeout) const
        {
            require_created_handle();
            if (WinHttpSetTimeouts(h, nResolveTimeout, nConnectTimeout, nSendTimeout, nReceiveTimeout))
            {
                return true;
            }

            context.report_error(format_winhttp_last_error_message("WinHttpSetTimeouts", sanitized_url));
            return false;
        }

        bool SetOption(DiagnosticContext& context,
                       StringView sanitized_url,
                       DWORD dwOption,
                       LPVOID lpBuffer,
                       DWORD dwBufferLength) const
        {
            require_created_handle();
            if (WinHttpSetOption(h, dwOption, lpBuffer, dwBufferLength))
            {
                return true;
            }

            context.report_error(format_winhttp_last_error_message("WinHttpSetOption", sanitized_url));
            return false;
        }

        DWORD QueryHeaders(DiagnosticContext& context,
                           StringView sanitized_url,
                           DWORD dwInfoLevel,
                           LPWSTR pwszName,
                           LPVOID lpBuffer,
                           LPDWORD lpdwBufferLength,
                           LPDWORD lpdwIndex) const
        {
            require_created_handle();
            if (WinHttpQueryHeaders(h, dwInfoLevel, pwszName, lpBuffer, lpdwBufferLength, lpdwIndex))
            {
                return 0;
            }

            DWORD last_error = GetLastError();
            context.report_error(format_winhttp_last_error_message("WinHttpQueryHeaders", sanitized_url, last_error));
            return last_error;
        }

        bool ReadData(DiagnosticContext& context,
                      StringView sanitized_url,
                      LPVOID buffer,
                      DWORD dwNumberOfBytesToRead,
                      DWORD* numberOfBytesRead)
        {
            require_created_handle();
            if (WinHttpReadData(h, buffer, dwNumberOfBytesToRead, numberOfBytesRead))
            {
                return true;
            }

            context.report_error(format_winhttp_last_error_message("WinHttpReadData", sanitized_url));
            return false;
        }

        ~WinHttpHandle()
        {
            if (h)
            {
                // intentionally ignore failures
                (void)WinHttpCloseHandle(h);
            }
        }

    private:
        HINTERNET h{};
    };

    enum class WinHttpTrialResult
    {
        failed,
        succeeded,
        retry
    };

    struct WinHttpSession
    {
        bool open(DiagnosticContext& context, StringView sanitized_url)
        {
            if (!m_hSession.Open(context,
                                 sanitized_url,
                                 L"vcpkg/1.0",
                                 WINHTTP_ACCESS_TYPE_NO_PROXY,
                                 WINHTTP_NO_PROXY_NAME,
                                 WINHTTP_NO_PROXY_BYPASS,
                                 0))
            {
                return false;
            }

            // Increase default timeouts to help connections behind proxies
            // WinHttpSetTimeouts(HINTERNET hInternet, int nResolveTimeout, int nConnectTimeout, int nSendTimeout, int
            // nReceiveTimeout);
            if (!m_hSession.SetTimeouts(context, sanitized_url, 0, 120000, 120000, 120000))
            {
                return false;
            }

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
                if (!m_hSession.SetOption(context, sanitized_url, WINHTTP_OPTION_PROXY, &proxy, sizeof(proxy)))
                {
                    return false;
                }
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
                    if (!m_hSession.SetOption(context, sanitized_url, WINHTTP_OPTION_PROXY, &proxy, sizeof(proxy)))
                    {
                        return false;
                    }
                }
            }

            // Use Windows 10 defaults on Windows 7
            DWORD secure_protocols(WINHTTP_FLAG_SECURE_PROTOCOL_TLS1 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1 |
                                   WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2);
            if (!m_hSession.SetOption(context,
                                      sanitized_url,
                                      WINHTTP_OPTION_SECURE_PROTOCOLS,
                                      &secure_protocols,
                                      sizeof(secure_protocols)))
            {
                return false;
            }

            // Many open source mirrors such as https://download.gnome.org/ will redirect to http mirrors.
            // `curl.exe -L` does follow https -> http redirection.
            // Additionally, vcpkg hash checks the resulting archive.
            DWORD redirect_policy(WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS);
            if (!m_hSession.SetOption(
                    context, sanitized_url, WINHTTP_OPTION_REDIRECT_POLICY, &redirect_policy, sizeof(redirect_policy)))
            {
                return false;
            }

            return true;
        }

        WinHttpHandle m_hSession;
    };

    struct WinHttpConnection
    {
        bool connect(DiagnosticContext& context,
                     StringView sanitized_url,
                     const WinHttpSession& hSession,
                     StringView hostname,
                     INTERNET_PORT port)
        {
            // Specify an HTTP server.
            return m_hConnect.Connect(context, sanitized_url, hSession.m_hSession, hostname, port);
        }

        WinHttpHandle m_hConnect;
    };

    struct WinHttpRequest
    {
        bool open(DiagnosticContext& context,
                  const WinHttpConnection& hConnect,
                  StringView path_query_fragment,
                  StringView sanitized_url,
                  bool https,
                  const wchar_t* method = L"GET")
        {
            m_sanitized_url.assign(sanitized_url.data(), sanitized_url.size());
            if (!m_hRequest.OpenRequest(context,
                                        hConnect.m_hConnect,
                                        sanitized_url,
                                        method,
                                        path_query_fragment,
                                        nullptr,
                                        WINHTTP_NO_REFERER,
                                        WINHTTP_DEFAULT_ACCEPT_TYPES,
                                        https ? WINHTTP_FLAG_SECURE : 0))
            {
                return false;
            }

            // Send a request.
            if (!m_hRequest.SendRequest(
                    context, sanitized_url, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
            {
                return false;
            }

            // End the request.
            if (!m_hRequest.ReceiveResponse(context, sanitized_url))
            {
                return false;
            }

            return true;
        }

        Optional<int> query_status(DiagnosticContext& context) const
        {
            DWORD status_code;
            DWORD size = sizeof(status_code);
            DWORD last_error = m_hRequest.QueryHeaders(context,
                                                       m_sanitized_url,
                                                       WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                                       WINHTTP_HEADER_NAME_BY_INDEX,
                                                       &status_code,
                                                       &size,
                                                       WINHTTP_NO_HEADER_INDEX);
            if (last_error)
            {
                return nullopt;
            }

            return status_code;
        }

        bool query_content_length(DiagnosticContext& context, Optional<unsigned long long>& result) const
        {
            static constexpr DWORD buff_characters = 21; // 18446744073709551615
            wchar_t buff[buff_characters];
            DWORD size = sizeof(buff);
            AttemptDiagnosticContext adc{context};
            DWORD last_error = m_hRequest.QueryHeaders(adc,
                                                       m_sanitized_url,
                                                       WINHTTP_QUERY_CONTENT_LENGTH,
                                                       WINHTTP_HEADER_NAME_BY_INDEX,
                                                       buff,
                                                       &size,
                                                       WINHTTP_NO_HEADER_INDEX);
            if (!last_error)
            {
                adc.commit();
                result = Strings::strto<unsigned long long>(Strings::to_utf8(buff, size >> 1));
                return true;
            }

            if (last_error == ERROR_WINHTTP_HEADER_NOT_FOUND)
            {
                adc.handle();
                return true;
            }

            adc.commit();
            return false;
        }

        WinHttpTrialResult write_response_body(DiagnosticContext& context,
                                               MessageSink& machine_readable_progress,
                                               const WriteFilePointer& file)
        {
            static constexpr DWORD buff_size = 65535;
            std::unique_ptr<char[]> buff{new char[buff_size]};
            Optional<unsigned long long> maybe_content_length;
            auto last_write = std::chrono::steady_clock::now();
            if (!query_content_length(context, maybe_content_length))
            {
                return WinHttpTrialResult::retry;
            }

            unsigned long long total_downloaded_size = 0;
            for (;;)
            {
                DWORD this_read;
                if (!m_hRequest.ReadData(context, m_sanitized_url, buff.get(), buff_size, &this_read))
                {
                    return WinHttpTrialResult::retry;
                }

                if (this_read == 0)
                {
                    return WinHttpTrialResult::succeeded;
                }

                do
                {
                    const auto this_write = static_cast<DWORD>(file.write(buff.get(), 1, this_read));
                    if (this_write == 0)
                    {
                        context.report_error(format_filesystem_call_error(
                            std::error_code{errno, std::generic_category()}, "fwrite", {file.path()}));
                        return WinHttpTrialResult::failed;
                    }

                    maybe_emit_winhttp_progress(
                        machine_readable_progress, maybe_content_length, last_write, total_downloaded_size);
                    this_read -= this_write;
                    total_downloaded_size += this_write;
                } while (this_read > 0);
            }
        }

        WinHttpHandle m_hRequest;
        std::string m_sanitized_url;
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

    static std::vector<ExpectedL<int>> curl_bulk_operation(View<Command> operation_args,
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

        prefix_cmd.string_arg("--retry").string_arg("3").string_arg("-L").string_arg("-w").string_arg(
            GUID_MARKER "%{http_code} %{exitcode} %{errormsg}\\n");
#undef GUID_MARKER

        std::vector<ExpectedL<int>> ret;
        ret.reserve(operation_args.size());
        add_curl_headers(prefix_cmd, headers);
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
            std::vector<std::string> debug_lines;
            auto maybe_this_batch_exit_code = cmd_execute_and_stream_lines(batch_cmd, [&](StringView line) {
                debug_lines.emplace_back(line.data(), line.size());
                parse_curl_status_line(ret, guid_marker, line);
            });

            if (auto this_batch_exit_code = maybe_this_batch_exit_code.get())
            {
                if (!ret.empty())
                {
                    if (auto last_http_code = ret.back().get())
                    {
                        if (*last_http_code == 0 && *this_batch_exit_code)
                        {
                            // old version of curl, we only have the result code for the last operation
                            ret.back() = msg::format(msgCurlFailedGeneric, msg::exit_code = *this_batch_exit_code);
                        }
                    }
                }

                if (ret.size() != last_try_op)
                {
                    // curl didn't process everything we asked of it; this usually means curl crashed
                    auto full_failure =
                        msg::format_error(msgCurlFailedToReturnExpectedNumberOfExitCodes,
                                          msg::exit_code = *this_batch_exit_code,
                                          msg::command_line = replace_secrets(std::move(batch_cmd).extract(), secrets));
                    for (const auto& debug_line : debug_lines)
                    {
                        full_failure.append_raw('\n');
                        full_failure.append_raw(debug_line);
                    }

                    ret.emplace_back(std::move(full_failure));
                    return ret;
                }
            }
            else
            {
                // couldn't even launch curl, record this as the last fatal error and give up
                ret.emplace_back(std::move(maybe_this_batch_exit_code).error());
                return ret;
            }
        }

        return ret;
    }

    bool AssetCachingSettings::asset_cache_configured() const noexcept
    {
        return m_read_url_template.has_value() || m_script.has_value();
    }

    std::vector<ExpectedL<int>> url_heads(View<std::string> urls, View<std::string> headers, View<std::string> secrets)
    {
        return curl_bulk_operation(
            Util::fmap(urls, [](const std::string& url) { return Command{}.string_arg(url_encode_spaces(url)); }),
            "--head",
            headers,
            secrets);
    }

    std::vector<ExpectedL<int>> download_files(View<std::pair<std::string, Path>> url_pairs,
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

    bool submit_github_dependency_graph_snapshot(const Optional<std::string>& maybe_github_server_url,
                                                 const std::string& github_token,
                                                 const std::string& github_repository,
                                                 const Json::Object& snapshot)
    {
        static constexpr StringLiteral guid_marker = "fcfad8a3-bb68-4a54-ad00-dab1ff671ed2";

        std::string uri;
        if (auto github_server_url = maybe_github_server_url.get())
        {
            uri = *github_server_url;
            uri.append("/api/v3");
        }
        else
        {
            uri = "https://api.github.com";
        }

        fmt::format_to(
            std::back_inserter(uri), "/repos/{}/dependency-graph/snapshots", url_encode_spaces(github_repository));

        auto cmd = Command{"curl"};
        cmd.string_arg("-w").string_arg("\\n" + guid_marker.to_string() + "%{http_code}");
        cmd.string_arg("-X").string_arg("POST");
        {
            std::string headers[] = {
                "Accept: application/vnd.github+json",
                "Authorization: Bearer " + github_token,
                "X-GitHub-Api-Version: 2022-11-28",
            };
            add_curl_headers(cmd, headers);
        }

        cmd.string_arg(uri);
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
        add_curl_headers(http_cmd, headers);
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
        msg::println(msgAssetCacheSuccesfullyStored,
                     msg::path = file.filename(),
                     msg::url = replace_secrets(url.to_string(), secrets));
        return 0;
    }

    std::string format_url_query(StringView base_url, View<std::string> query_params)
    {
        if (query_params.empty())
        {
            return base_url.to_string();
        }

        return fmt::format(FMT_COMPILE("{}?{}"), base_url, fmt::join(query_params, "&"));
    }

    ExpectedL<std::string> invoke_http_request(StringView method,
                                               View<std::string> headers,
                                               StringView url,
                                               StringView data)
    {
        auto cmd = Command{"curl"}.string_arg("-s").string_arg("-L");
        add_curl_headers(cmd, headers);

        cmd.string_arg("-X").string_arg(method);

        if (!data.empty())
        {
            cmd.string_arg("--data-raw").string_arg(data);
        }

        cmd.string_arg(url_encode_spaces(url));

        return flatten_out(cmd_execute_and_capture_output(cmd), "curl");
    }

#if defined(_WIN32)
    static WinHttpTrialResult download_winhttp_trial(DiagnosticContext& context,
                                                     MessageSink& machine_readable_progress,
                                                     const Filesystem& fs,
                                                     const WinHttpSession& s,
                                                     const Path& download_path_part_path,
                                                     SplitURIView split_uri,
                                                     StringView hostname,
                                                     INTERNET_PORT port,
                                                     StringView sanitized_url)
    {
        WinHttpConnection conn;
        if (!conn.connect(context, sanitized_url, s, hostname, port))
        {
            return WinHttpTrialResult::retry;
        }

        WinHttpRequest req;
        if (!req.open(context, conn, split_uri.path_query_fragment, sanitized_url, split_uri.scheme == "https"))
        {
            return WinHttpTrialResult::retry;
        }

        auto maybe_status = req.query_status(context);
        const auto status = maybe_status.get();
        if (!status)
        {
            return WinHttpTrialResult::retry;
        }

        if (*status < 200 || *status >= 300)
        {
            context.report_error(msgDownloadFailedStatusCode, msg::url = sanitized_url, msg::value = *status);
            return WinHttpTrialResult::failed;
        }

        return req.write_response_body(
            context, machine_readable_progress, fs.open_for_write(download_path_part_path, VCPKG_LINE_INFO));
    }

    /// <summary>
    /// Download a file using WinHTTP -- only supports HTTP and HTTPS
    /// </summary>
    static bool download_winhttp(DiagnosticContext& context,
                                 MessageSink& machine_readable_progress,
                                 const Filesystem& fs,
                                 const Path& download_path_part_path,
                                 SplitURIView split_uri,
                                 const std::string& url,
                                 const std::vector<std::string>& secrets)
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
        WinHttpSession s;
        if (!s.open(context, sanitized_url))
        {
            return false;
        }

        AttemptDiagnosticContext adc{context};
        switch (download_winhttp_trial(context,
                                       machine_readable_progress,
                                       fs,
                                       s,
                                       download_path_part_path,
                                       split_uri,
                                       hostname,
                                       port,
                                       sanitized_url))
        {
            case WinHttpTrialResult::succeeded: adc.commit(); return true;
            case WinHttpTrialResult::failed: adc.commit(); return false;
            case WinHttpTrialResult::retry: break;
        }

        for (size_t trials = 1; trials < 4; ++trials)
        {
            // 1s, 2s, 4s
            const auto trialMs = 500 << trials;
            adc.handle();
            adc.statusln(msg::format_warning(msgDownloadFailedRetrying, msg::value = trialMs));
            std::this_thread::sleep_for(std::chrono::milliseconds(trialMs));
            switch (download_winhttp_trial(context,
                                           machine_readable_progress,
                                           fs,
                                           s,
                                           download_path_part_path,
                                           split_uri,
                                           hostname,
                                           port,
                                           sanitized_url))
            {
                case WinHttpTrialResult::succeeded: adc.commit(); return true;
                case WinHttpTrialResult::failed: adc.commit(); return false;
                case WinHttpTrialResult::retry: break;
            }
        }

        adc.commit();
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
        auto maybe_https_proxy_env = get_environment_variable(EnvironmentVariableHttpsProxy);
        bool needs_proxy_auth = false;
        if (auto proxy_url = maybe_https_proxy_env.get())
        {
            needs_proxy_auth = proxy_url->find('@') != std::string::npos;
        }
        if (headers.size() == 0 && !needs_proxy_auth)
        {
            auto split_uri = split_uri_view(url).value_or_exit(VCPKG_LINE_INFO);
            if (split_uri.scheme == "https" || split_uri.scheme == "http")
            {
                auto maybe_authority = split_uri.authority.get();
                if (!maybe_authority)
                {
                    Checks::msg_exit_with_error(VCPKG_LINE_INFO, msgInvalidUri, msg::value = url);
                }

                auto authority = maybe_authority->substr(2);
                // This check causes complex URLs (non-default port, embedded basic auth) to be passed down to curl.exe
                if (Strings::find_first_of(authority, ":@") == authority.end())
                {
                    BufferedDiagnosticContext bdc{stdout_sink};
                    if (download_winhttp(bdc, progress_sink, fs, download_path_part_path, split_uri, url, secrets))
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

                    errors.push_back(LocalizedString::from_raw(std::move(bdc).to_string()));
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
        add_curl_headers(cmd, headers);
        std::string non_progress_content;
        auto maybe_exit_code = cmd_execute_and_stream_lines(cmd, [&](StringView line) {
            const auto maybe_parsed = try_parse_curl_progress_data(line);
            if (const auto parsed = maybe_parsed.get())
            {
                progress_sink.println(Color::none,
                                      LocalizedString::from_raw(fmt::format("{}%", parsed->total_percent)));
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

    void parse_curl_status_line(std::vector<ExpectedL<int>>& http_codes, StringLiteral prefix, StringView this_line)
    {
        if (!Strings::starts_with(this_line, prefix))
        {
            return;
        }

        auto first = this_line.begin();
        const auto last = this_line.end();
        first += prefix.size();
        const auto first_http_code = first;

        int http_code;
        for (;; ++first)
        {
            if (first == last)
            {
                // this output is broken, even if we don't know %{exit_code} or ${errormsg}, the spaces in front
                // of them should still be printed.
                return;
            }

            if (!ParserBase::is_ascii_digit(*first))
            {
                http_code = Strings::strto<int>(StringView{first_http_code, first}).value_or_exit(VCPKG_LINE_INFO);
                break;
            }
        }

        if (*first != ' ' || ++first == last)
        {
            // didn't see the space after the http_code
            return;
        }

        if (*first == ' ')
        {
            // old curl that doesn't understand %{exit_code}, this is the space after it
            http_codes.emplace_back(http_code);
            return;
        }

        if (!ParserBase::is_ascii_digit(*first))
        {
            // not exit_code
            return;
        }

        const auto first_exit_code = first;
        for (;;)
        {
            if (++first == last)
            {
                // didn't see the space after %{exit_code}
                return;
            }

            if (*first == ' ')
            {
                // the space after exit_code, everything after this space is the error message if any
                auto exit_code = Strings::strto<int>(StringView{first_exit_code, first}).value_or_exit(VCPKG_LINE_INFO);
                if (exit_code == 0)
                {
                    // success!
                    http_codes.emplace_back(http_code);
                    return;
                }

                // note that this gets the space out of the output :)
                http_codes.emplace_back(
                    msg::format(msgCurlFailedGeneric, msg::exit_code = exit_code).append_raw(StringView{first, last}));
                return;
            }

            if (!ParserBase::is_ascii_digit(*first))
            {
                // non numeric exit_code?
                return;
            }
        }
    }

    void download_file(const AssetCachingSettings& settings,
                       const Filesystem& fs,
                       const std::string& url,
                       View<std::string> headers,
                       const Path& download_path,
                       const Optional<std::string>& sha512,
                       MessageSink& progress_sink)
    {
        download_file(settings, fs, View<std::string>(&url, 1), headers, download_path, sha512, progress_sink);
    }

    std::string download_file(const AssetCachingSettings& download_settings,
                              const Filesystem& fs,
                              View<std::string> urls,
                              View<std::string> headers,
                              const Path& download_path,
                              const Optional<std::string>& sha512,
                              MessageSink& progress_sink)
    {
        std::vector<LocalizedString> errors;
        bool block_origin_enabled = download_settings.m_block_origin;

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
            if (auto read_template = download_settings.m_read_url_template.get())
            {
                auto read_url = Strings::replace_all(*read_template, "<SHA>", *hash);
                if (try_download_file(fs,
                                      read_url,
                                      download_settings.m_read_headers,
                                      download_path,
                                      sha512,
                                      download_settings.m_secrets,
                                      errors,
                                      progress_sink))
                {
                    msg::println(msgAssetCacheHit,
                                 msg::path = download_path.filename(),
                                 msg::url = replace_secrets(read_url, download_settings.m_secrets));
                    return read_url;
                }
                else if (block_origin_enabled)
                {
                    msg::println(msgAssetCacheMissBlockOrigin, msg::url = urls[0]);
                }
                else
                {
                    msg::println(msgAssetCacheMiss, msg::url = urls[0]);
                }
            }
            else if (auto script = download_settings.m_script.get())
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
                            msg::println(msgDownloadSuccesful, msg::path = download_path.filename());
                            return urls[0];
                        }
                        msg::println_error(maybe_success.error());
                    }
                    else
                    {
                        msg::println_error(maybe_res.error());
                    }
                }
            }
        }

        if (block_origin_enabled)
        {
            msg::println(msgAssetCacheMissBlockOrigin, msg::url = urls[0]);
        }
        else
        {
            if (urls.size() != 0)
            {
                msg::println(msgDownloadingUrl, msg::url = download_path.filename());
                auto maybe_url = try_download_file(
                    fs, urls, headers, download_path, sha512, download_settings.m_secrets, errors, progress_sink);
                if (auto url = maybe_url.get())
                {
                    msg::println(msgDownloadSuccesful, msg::path = download_path.filename());

                    if (auto hash = sha512.get())
                    {
                        auto maybe_push = put_file_to_mirror(download_settings, fs, download_path, *hash);
                        if (!maybe_push)
                        {
                            msg::println_warning(
                                msgFailedToStoreBackToMirror,
                                msg::path = download_path.filename(),
                                msg::url = replace_secrets(download_path.c_str(), download_settings.m_secrets));
                            msg::println(maybe_push.error());
                        }
                    }

                    return *url;
                }
                else
                {
                    msg::println(msgDownloadFailedProxySettings,
                                 msg::path = download_path.filename(),
                                 msg::url = "https://github.com/microsoft/vcpkg-tool/pull/77");
                }
            }
        }

        for (LocalizedString& error : errors)
        {
            msg::println(error);
        }

        Checks::exit_fail(VCPKG_LINE_INFO);
    }

    ExpectedL<int> put_file_to_mirror(const AssetCachingSettings& download_settings,
                                      const ReadOnlyFilesystem& fs,
                                      const Path& file_to_put,
                                      StringView sha512)
    {
        auto maybe_mirror_url =
            Strings::replace_all(download_settings.m_write_url_template.value_or(""), "<SHA>", sha512);
        if (!maybe_mirror_url.empty())
        {
            return put_file(
                fs, maybe_mirror_url, download_settings.m_secrets, download_settings.m_write_headers, file_to_put);
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
            parse_curl_uint_impl(result.received_percent, first, last) ||
            parse_curl_max5_impl(result.received_size, first, last) ||
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
