#include <vcpkg/base/api-stable-format.h>
#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/curl.h>
#include <vcpkg/base/downloads.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/hash.h>
#include <vcpkg/base/json.h>
#include <vcpkg/base/lazy.h>
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

#include <set>

using namespace vcpkg;

namespace
{
    constexpr StringLiteral vcpkg_curl_user_agent =
        "vcpkg/" VCPKG_BASE_VERSION_AS_STRING "-" VCPKG_VERSION_AS_STRING " (curl)";

    void set_common_curl_options(CURL* handle, const char* url, curl_slist* request_headers)
    {
        curl_easy_setopt(handle, CURLOPT_USERAGENT, vcpkg_curl_user_agent.c_str());
        curl_easy_setopt(handle, CURLOPT_URL, url);
        curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 2L); // CURLFOLLOW_OBEYCODE
        curl_easy_setopt(handle, CURLOPT_HTTPHEADER, request_headers);
        curl_easy_setopt(handle, CURLOPT_HEADEROPT, CURLHEADER_SEPARATE); // don't send headers to proxy CONNECT
    }
}

namespace vcpkg
{
    SanitizedUrl::SanitizedUrl(StringView raw_url, View<std::string> secrets)
        : m_sanitized_url(raw_url.data(), raw_url.size())
    {
        replace_secrets(m_sanitized_url, secrets);
    }

#if defined(_WIN32)
    struct FormatMessageHLocalAlloc
    {
        LPWSTR buffer = nullptr;

        ~FormatMessageHLocalAlloc()
        {
            if (buffer)
            {
                LocalFree(buffer);
            }
        }
    };

    static LocalizedString format_winhttp_last_error_message(StringLiteral api_name,
                                                             const SanitizedUrl& sanitized_url,
                                                             DWORD last_error)
    {
        const HMODULE winhttp_module = GetModuleHandleW(L"winhttp.dll");
        FormatMessageHLocalAlloc alloc;
        DWORD tchars_excluding_terminating_null = 0;
        if (winhttp_module)
        {
            tchars_excluding_terminating_null =
                FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_HMODULE,
                               winhttp_module,
                               last_error,
                               0,
                               reinterpret_cast<LPWSTR>(&alloc.buffer),
                               0,
                               nullptr);
        }

        auto result = msg::format(
            msgDownloadWinHttpError, msg::system_api = api_name, msg::exit_code = last_error, msg::url = sanitized_url);
        if (tchars_excluding_terminating_null && alloc.buffer)
        {
            while (tchars_excluding_terminating_null != 0 &&
                   (alloc.buffer[tchars_excluding_terminating_null - 1] == L'\r' ||
                    alloc.buffer[tchars_excluding_terminating_null - 1] == L'\n'))
            {
                --tchars_excluding_terminating_null;
            }

            tchars_excluding_terminating_null = static_cast<DWORD>(
                std::remove(alloc.buffer, alloc.buffer + tchars_excluding_terminating_null, L'\r') - alloc.buffer);
            result.append_raw(' ').append_raw(Strings::to_utf8(alloc.buffer, tchars_excluding_terminating_null));
        }

        return result;
    }

    static LocalizedString format_winhttp_last_error_message(StringLiteral api_name, const SanitizedUrl& sanitized_url)
    {
        return format_winhttp_last_error_message(api_name, sanitized_url, GetLastError());
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
                     const WinHttpHandle& session,
                     StringView hostname,
                     INTERNET_PORT port,
                     const SanitizedUrl& sanitized_url)
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
                  const SanitizedUrl& sanitized_url,
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
                         const SanitizedUrl& sanitized_url,
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
                         const SanitizedUrl& sanitized_url,
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

        bool ReceiveResponse(DiagnosticContext& context, const SanitizedUrl& url)
        {
            require_created_handle();
            if (WinHttpReceiveResponse(h, NULL))
            {
                return true;
            }

            context.report_error(format_winhttp_last_error_message("WinHttpReceiveResponse", url));
            return false;
        }

        bool SetTimeouts(DiagnosticContext& context,
                         const SanitizedUrl& sanitized_url,
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
                       const SanitizedUrl& sanitized_url,
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
                           const SanitizedUrl& sanitized_url,
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
                      const SanitizedUrl& sanitized_url,
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
        bool open(DiagnosticContext& context, const SanitizedUrl& sanitized_url)
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
                StringView p_https_proxy_view = *p_https_proxy;
                if (p_https_proxy_view.size() != 0 && p_https_proxy_view.back() == '/')
                {
                    // remove trailing slash
                    p_https_proxy_view = p_https_proxy_view.substr(0, p_https_proxy_view.size() - 1);
                }

                std::wstring env_proxy_settings = Strings::to_utf16(p_https_proxy_view);
                WINHTTP_PROXY_INFO proxy;
                proxy.dwAccessType = WINHTTP_ACCESS_TYPE_NAMED_PROXY;
                proxy.lpszProxy = env_proxy_settings.data();

                // Try to get bypass list from environment variable
                auto maybe_no_proxy_env = get_environment_variable(EnvironmentVariableNoProxy);
                std::wstring env_noproxy_settings;
                if (auto p_no_proxy = maybe_no_proxy_env.get())
                {
                    env_noproxy_settings = Strings::to_utf16(*p_no_proxy);
                    proxy.lpszProxyBypass = env_noproxy_settings.data();
                }
                else
                {
                    proxy.lpszProxyBypass = nullptr;
                }

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
                     const WinHttpSession& hSession,
                     StringView hostname,
                     INTERNET_PORT port,
                     const SanitizedUrl& sanitized_url)
        {
            // Specify an HTTP server.
            return m_hConnect.Connect(context, hSession.m_hSession, hostname, port, sanitized_url);
        }

        WinHttpHandle m_hConnect;
    };

    struct WinHttpRequest
    {
        bool open(DiagnosticContext& context,
                  const WinHttpConnection& hConnect,
                  StringView path_query_fragment,
                  const SanitizedUrl& sanitized_url,
                  bool https,
                  const wchar_t* method = L"GET")
        {
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

        Optional<int> query_status(DiagnosticContext& context, const SanitizedUrl& sanitized_url) const
        {
            DWORD status_code;
            DWORD size = sizeof(status_code);
            DWORD last_error = m_hRequest.QueryHeaders(context,
                                                       sanitized_url,
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

        bool query_content_length(DiagnosticContext& context,
                                  const SanitizedUrl& sanitized_url,
                                  Optional<unsigned long long>& result) const
        {
            static constexpr DWORD buff_characters = 21; // 18446744073709551615
            wchar_t buff[buff_characters];
            DWORD size = sizeof(buff);
            AttemptDiagnosticContext adc{context};
            DWORD last_error = m_hRequest.QueryHeaders(adc,
                                                       sanitized_url,
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
                                               const SanitizedUrl& sanitized_url,
                                               const WriteFilePointer& file)
        {
            static constexpr DWORD buff_size = 65535;
            std::unique_ptr<char[]> buff{new char[buff_size]};
            Optional<unsigned long long> maybe_content_length;
            auto last_write = std::chrono::steady_clock::now();
            if (!query_content_length(context, sanitized_url, maybe_content_length))
            {
                return WinHttpTrialResult::retry;
            }

            unsigned long long total_downloaded_size = 0;
            for (;;)
            {
                DWORD this_read;
                if (!m_hRequest.ReadData(context, sanitized_url, buff.get(), buff_size, &this_read))
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
    };
#endif

    Optional<SplitUrlView> parse_split_url_view(StringView raw_url)
    {
        auto sep = std::find(raw_url.begin(), raw_url.end(), ':');
        if (sep == raw_url.end())
        {
            return nullopt;
        }

        StringView scheme(raw_url.begin(), sep);
        if (Strings::starts_with({sep + 1, raw_url.end()}, "//"))
        {
            auto path_start = std::find(sep + 3, raw_url.end(), '/');
            return SplitUrlView{scheme, StringView{sep + 1, path_start}, StringView{path_start, raw_url.end()}};
        }

        // no authority
        return SplitUrlView{scheme, {}, StringView{sep + 1, raw_url.end()}};
    }

    static bool check_downloaded_file_hash(DiagnosticContext& context,
                                           const ReadOnlyFilesystem& fs,
                                           const SanitizedUrl& sanitized_url,
                                           const Path& downloaded_path,
                                           StringView sha512,
                                           std::string* out_sha512)
    {
        if (!std::all_of(sha512.begin(), sha512.end(), ParserBase::is_hex_digit_lower))
        {
            Checks::unreachable(VCPKG_LINE_INFO);
        }

        auto maybe_actual_hash =
            vcpkg::Hash::get_file_hash_required(context, fs, downloaded_path, Hash::Algorithm::Sha512);
        if (auto actual_hash = maybe_actual_hash.get())
        {
            if (sha512 == *actual_hash)
            {
                if (out_sha512)
                {
                    *out_sha512 = std::move(*actual_hash);
                }

                return true;
            }

            context.report(DiagnosticLine{DiagKind::Error,
                                          downloaded_path,
                                          msg::format(msgDownloadFailedHashMismatch, msg::url = sanitized_url)});
            context.report(DiagnosticLine{DiagKind::Note,
                                          msg::format(msgDownloadFailedHashMismatchExpectedHash, msg::sha = sha512)});
            context.report(DiagnosticLine{
                DiagKind::Note, msg::format(msgDownloadFailedHashMismatchActualHash, msg::sha = *actual_hash)});

            if (out_sha512)
            {
                *out_sha512 = std::move(*actual_hash);
            }
        }

        return false;
    }

    static bool check_downloaded_file_hash(DiagnosticContext& context,
                                           const ReadOnlyFilesystem& fs,
                                           const SanitizedUrl& sanitized_url,
                                           const Path& downloaded_path,
                                           const StringView* maybe_sha512,
                                           std::string* out_sha512)
    {
        if (maybe_sha512)
        {
            return check_downloaded_file_hash(context, fs, sanitized_url, downloaded_path, *maybe_sha512, out_sha512);
        }

        if (out_sha512)
        {
            auto maybe_actual_hash =
                vcpkg::Hash::get_file_hash_required(context, fs, downloaded_path, Hash::Algorithm::Sha512);
            if (auto actual_hash = maybe_actual_hash.get())
            {
                *out_sha512 = std::move(*actual_hash);
            }
        }

        return true;
    }

    struct CurlRequestPrivateData
    {
        size_t request_index = 0;
        std::unique_ptr<WriteFilePointer> file = nullptr;
    };
    static size_t write_file_callback(void* contents, size_t size, size_t nmemb, void* param)
    {
        auto* file = reinterpret_cast<WriteFilePointer*>(param);
        if (!file) return 0;
        return file->write(contents, size, nmemb);
    }

    static std::vector<int> libcurl_bulk_operation(DiagnosticContext& context,
                                                   View<std::string> urls,
                                                   View<Path> outputs,
                                                   View<std::string> headers,
                                                   View<std::string> secrets)
    {
        // TODO: handle secret replacement when error messages are implemented
        (void)secrets;

        if (!outputs.empty() && outputs.size() != urls.size()) return {};

        if (vcpkg::curl_global_init_status() != CURLE_OK) Checks::unreachable(VCPKG_LINE_INFO);

        CURLM* multi_handle = curl_multi_init();
        if (!multi_handle) Checks::unreachable(VCPKG_LINE_INFO);

        std::vector<int> ret(urls.size(), -1);
        std::vector<CurlRequestPrivateData> private_data;
        private_data.reserve(urls.size());

        curl_slist* request_headers = nullptr;
        for (auto&& header : headers)
        {
            request_headers = curl_slist_append(request_headers, header.c_str());
        }

        size_t skipped = 0;
        for (size_t request_index = 0; request_index < urls.size(); ++request_index)
        {
            auto& data = private_data.emplace_back(CurlRequestPrivateData{request_index});
            const auto& url = urls[request_index];

            CURL* curl = curl_easy_init();
            if (!curl)
            {
                context.report_error(
                    msgCurlFailedGeneric, msg::exit_code = -1, msg::error_msg = "curl_easy_init failed");
                ret[request_index] = CURLE_FAILED_INIT;
                skipped++;
                continue;
            }

            set_common_curl_options(curl, url.c_str(), request_headers);
            curl_easy_setopt(curl, CURLOPT_PRIVATE, &data);
            if (outputs.size() > request_index)
            {
                const auto& output = outputs[request_index];

                std::error_code ec;
                data.file.reset(new WriteFilePointer(output, Append::NO, ec));
                if (ec)
                {
                    context.report_error(format_filesystem_call_error(ec, "fopen", {output}));
                }
                else
                {
                    curl_easy_setopt(curl, CURLOPT_WRITEDATA, data.file.get());
                    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &write_file_callback);
                }
            }
            curl_multi_add_handle(multi_handle, curl);
        }

        int still_running = 0;
        do
        {
            CURLMcode mc = curl_multi_perform(multi_handle, &still_running);
            if (mc != CURLM_OK)
            {
                context.report_error(msg::format(msgCurlFailedGeneric,
                                                 msg::exit_code = static_cast<int>(mc),
                                                 msg::error_msg = curl_multi_strerror(mc)));
            }

            mc = curl_multi_poll(multi_handle, nullptr, 0, 1000, nullptr);
            if (mc != CURLM_OK)
            {
                context.report_error(msg::format(msgCurlFailedGeneric,
                                                 msg::exit_code = static_cast<int>(mc),
                                                 msg::error_msg = curl_multi_strerror(mc)));
            }
        } while (still_running);

        int messages_in_queue = 0;
        size_t processed = 0;
        do
        {
            while (auto* msg = curl_multi_info_read(multi_handle, &messages_in_queue))
            {
                if (msg->msg == CURLMSG_DONE)
                {
                    ++processed;
                    CURL* handle = msg->easy_handle;

                    if (msg->data.result == CURLE_OK)
                    {
                        CurlRequestPrivateData* data = nullptr;
                        curl_easy_getinfo(handle, CURLINFO_PRIVATE, &data);
                        if (!data) Checks::unreachable(VCPKG_LINE_INFO);

                        auto idx = data->request_index;
                        long response_code = -1;
                        curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &response_code);
                        ret[idx] = static_cast<int>(response_code);
                    }
                    else
                    {
                        context.report_error(msg::format(msgCurlFailedGeneric,
                                                         msg::exit_code = static_cast<int>(msg->data.result),
                                                         msg::error_msg = curl_easy_strerror(msg->data.result)));
                    }
                    curl_multi_remove_handle(multi_handle, handle);
                    curl_easy_cleanup(handle);
                }
            }
        } while (processed + skipped < urls.size());

        curl_slist_free_all(request_headers);
        curl_multi_cleanup(multi_handle);

        return ret;
    }

    static std::vector<int> libcurl_bulk_check(DiagnosticContext& context,
                                               View<std::string> urls,
                                               View<std::string> headers,
                                               View<std::string> secrets)
    {
        return libcurl_bulk_operation(context, urls, {}, headers, secrets);
    }

    std::vector<int> url_heads(DiagnosticContext& context,
                               View<std::string> urls,
                               View<std::string> headers,
                               View<std::string> secrets)
    {
        return libcurl_bulk_check(context, urls, headers, secrets);
    }

    std::vector<int> download_files_no_cache(DiagnosticContext& context,
                                             View<std::pair<std::string, Path>> url_pairs,
                                             View<std::string> headers,
                                             View<std::string> secrets)
    {
        return libcurl_bulk_operation(context,
                                      Util::fmap(url_pairs, [](auto&& kv) -> std::string { return kv.first; }),
                                      Util::fmap(url_pairs, [](auto&& kv) -> Path { return kv.second; }),
                                      headers,
                                      secrets);
    }

    bool submit_github_dependency_graph_snapshot(DiagnosticContext& context,
                                                 const Optional<std::string>& maybe_github_server_url,
                                                 const std::string& github_token,
                                                 const std::string& github_repository,
                                                 const Json::Object& snapshot)
    {
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

        CURL* curl = curl_easy_init();
        if (!curl)
        {
            context.report_error(
                msg::format(msgCurlFailedGeneric, msg::exit_code = -1, msg::error_msg = "curl_easy_init failed"));
            return false;
        }

        std::string post_data = Json::stringify(snapshot);

        curl_slist* request_headers = nullptr;
        request_headers = curl_slist_append(request_headers, "Accept: application/vnd.github+json");
        request_headers = curl_slist_append(request_headers, ("Authorization: Bearer " + github_token).c_str());
        request_headers = curl_slist_append(request_headers, "X-GitHub-Api-Version: 2022-11-28");
        request_headers = curl_slist_append(request_headers, "Content-Type: application/json");

        set_common_curl_options(curl, uri.c_str(), request_headers);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, vcpkg_curl_user_agent.data());
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, post_data.length());

        CURLcode result = curl_easy_perform(curl);
        long response_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

        curl_slist_free_all(request_headers);
        curl_easy_cleanup(curl);

        if (result != CURLE_OK)
        {
            context.report_error(msg::format(msgCurlFailedGeneric,
                                             msg::exit_code = static_cast<int>(result),
                                             msg::error_msg = curl_easy_strerror(result)));
            return false;
        }

        return response_code >= 200 && response_code < 300;
    }

    static size_t read_file_callback(char* buffer, size_t size, size_t nitems, void* param)
    {
        auto* file = static_cast<ReadFilePointer*>(param);
        return file->read(buffer, size, nitems);
    }

    bool store_to_asset_cache(DiagnosticContext& context,
                              StringView raw_url,
                              const SanitizedUrl& sanitized_url,
                              StringLiteral method,
                              View<std::string> headers,
                              const Path& file)
    {
        (void)method;

        std::error_code ec;
        auto fileptr = std::make_unique<ReadFilePointer>(file, ec);
        if (ec)
        {
            context.report_error(format_filesystem_call_error(ec, "fopen", {file}));
            return false;
        }
        auto file_size = fileptr->size(VCPKG_LINE_INFO);

        CURL* curl = curl_easy_init();
        if (!curl) Checks::unreachable(VCPKG_LINE_INFO);

        curl_slist* request_headers = nullptr;
        if (!raw_url.starts_with("ftp://"))
        {
            for (auto&& header : headers)
                request_headers = curl_slist_append(request_headers, header.c_str());
        }

        auto upload_url = url_encode_spaces(raw_url);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, vcpkg_curl_user_agent.data());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, request_headers);
        curl_easy_setopt(curl, CURLOPT_URL, upload_url.c_str());
        curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
        curl_easy_setopt(curl, CURLOPT_READDATA, fileptr.get());
        curl_easy_setopt(curl, CURLOPT_READFUNCTION, &read_file_callback);
        curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, static_cast<curl_off_t>(file_size));

        auto result = curl_easy_perform(curl);
        if (result != CURLE_OK)
        {
            context.report_error(msg::format(msgCurlFailedGeneric,
                                             msg::exit_code = static_cast<int>(result),
                                             msg::error_msg = curl_easy_strerror(result)));
            curl_easy_cleanup(curl);
            curl_slist_free_all(request_headers);
            return false;
        }

        long response_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

        if ((response_code >= 100 && response_code < 200) || response_code >= 300)
        {
            context.report_error(msg::format(msgCurlFailedToPutHttp,
                                             msg::exit_code = static_cast<int>(result),
                                             msg::error_msg = curl_easy_strerror(result),
                                             msg::url = sanitized_url,
                                             msg::value = response_code));
            curl_easy_cleanup(curl);
            curl_slist_free_all(request_headers);
            return false;
        }

        curl_easy_cleanup(curl);
        curl_slist_free_all(request_headers);
        return true;
    }

    bool azcopy_to_asset_cache(DiagnosticContext& context,
                               StringView raw_url,
                               const SanitizedUrl& sanitized_url,
                               const Path& file)
    {
        auto azcopy_cmd = Command{"azcopy"};
        azcopy_cmd.string_arg("copy");
        azcopy_cmd.string_arg("--from-to").string_arg("LocalBlob");
        azcopy_cmd.string_arg("--log-level").string_arg("NONE");
        azcopy_cmd.string_arg(file);
        azcopy_cmd.string_arg(raw_url.to_string());

        int code = 0;
        auto res = cmd_execute_and_stream_lines(context, azcopy_cmd, [&code](StringView line) {
            static constexpr StringLiteral response_marker = "RESPONSE ";
            if (line.starts_with(response_marker))
            {
                code = std::strtol(line.data() + response_marker.size(), nullptr, 10);
            }
        });

        auto pres = res.get();
        if (!pres)
        {
            return false;
        }

        if (*pres != 0)
        {
            context.report_error(msg::format(
                msgAzcopyFailedToPutBlob, msg::exit_code = *pres, msg::url = sanitized_url, msg::value = code));
            return false;
        }

        return true;
    }

    std::string format_url_query(StringView base_url, View<std::string> query_params)
    {
        if (query_params.empty())
        {
            return base_url.to_string();
        }

        return fmt::format(FMT_COMPILE("{}?{}"), base_url, fmt::join(query_params, "&"));
    }

#if defined(_WIN32)
    static WinHttpTrialResult download_winhttp_trial(DiagnosticContext& context,
                                                     MessageSink& machine_readable_progress,
                                                     const Filesystem& fs,
                                                     const WinHttpSession& s,
                                                     const Path& download_path_part_path,
                                                     SplitUrlView split_uri_view,
                                                     StringView hostname,
                                                     INTERNET_PORT port,
                                                     const SanitizedUrl& sanitized_url)
    {
        WinHttpConnection conn;
        if (!conn.connect(context, s, hostname, port, sanitized_url))
        {
            return WinHttpTrialResult::retry;
        }

        WinHttpRequest req;
        if (!req.open(
                context, conn, split_uri_view.path_query_fragment, sanitized_url, split_uri_view.scheme == "https"))
        {
            return WinHttpTrialResult::retry;
        }

        auto maybe_status = req.query_status(context, sanitized_url);
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

        return req.write_response_body(context,
                                       machine_readable_progress,
                                       sanitized_url,
                                       fs.open_for_write(download_path_part_path, VCPKG_LINE_INFO));
    }

    /// <summary>
    /// Download a file using WinHTTP -- only supports HTTP and HTTPS
    /// </summary>
    static bool download_winhttp(DiagnosticContext& context,
                                 MessageSink& machine_readable_progress,
                                 const Filesystem& fs,
                                 const Path& download_path_part_path,
                                 SplitUrlView split_url_view,
                                 const SanitizedUrl& sanitized_url)
    {
        // `download_winhttp` does not support user or port syntax in authorities
        auto hostname = split_url_view.authority.value_or_exit(VCPKG_LINE_INFO).substr(2);
        INTERNET_PORT port;
        if (split_url_view.scheme == "https")
        {
            port = INTERNET_DEFAULT_HTTPS_PORT;
        }
        else if (split_url_view.scheme == "http")
        {
            port = INTERNET_DEFAULT_HTTP_PORT;
        }
        else
        {
            Checks::unreachable(VCPKG_LINE_INFO);
        }

        // Make sure the directories are present, otherwise fopen_s fails
        const auto dir = download_path_part_path.parent_path();
        if (!dir.empty())
        {
            fs.create_directories(dir, VCPKG_LINE_INFO);
        }

        WinHttpSession s;
        if (!s.open(context, sanitized_url))
        {
            return false;
        }

        AttemptDiagnosticContext adc{context};
        switch (download_winhttp_trial(adc,
                                       machine_readable_progress,
                                       fs,
                                       s,
                                       download_path_part_path,
                                       split_url_view,
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
            context.statusln(
                DiagnosticLine(DiagKind::Warning,
                               msg::format(msgDownloadFailedRetrying, msg::value = trialMs, msg::url = sanitized_url))
                    .to_message_line());
            std::this_thread::sleep_for(std::chrono::milliseconds(trialMs));
            switch (download_winhttp_trial(adc,
                                           machine_readable_progress,
                                           fs,
                                           s,
                                           download_path_part_path,
                                           split_url_view,
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

    enum class DownloadPrognosis
    {
        Success,
        OtherError,
        NetworkErrorProxyMightHelp
    };

    static bool check_combine_download_prognosis(DownloadPrognosis& target, DownloadPrognosis individual_call)
    {
        switch (individual_call)
        {
            case DownloadPrognosis::Success: return true;
            case DownloadPrognosis::OtherError:
                if (target == DownloadPrognosis::Success)
                {
                    target = DownloadPrognosis::OtherError;
                }

                return false;
            case DownloadPrognosis::NetworkErrorProxyMightHelp:
                if (target == DownloadPrognosis::Success || target == DownloadPrognosis::OtherError)
                {
                    target = DownloadPrognosis::NetworkErrorProxyMightHelp;
                }

                return false;
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }
    }

    static void maybe_report_proxy_might_help(DiagnosticContext& context, DownloadPrognosis prognosis)
    {
        if (prognosis == DownloadPrognosis::NetworkErrorProxyMightHelp)
        {
            context.report(DiagnosticLine{DiagKind::Note, msg::format(msgDownloadFailedProxySettings)});
        }
    }

    static DownloadPrognosis try_download_file(DiagnosticContext& context,
                                               MessageSink& machine_readable_progress,
                                               const Filesystem& fs,
                                               StringView raw_url,
                                               const SanitizedUrl& sanitized_url,
                                               View<std::string> headers,
                                               const Path& download_path,
                                               const StringView* maybe_sha512,
                                               std::string* out_sha512)
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
            auto maybe_split_uri_view = parse_split_url_view(raw_url);
            auto split_uri_view = maybe_split_uri_view.get();
            if (!split_uri_view)
            {
                context.report_error(msgInvalidUri, msg::value = sanitized_url);
                return DownloadPrognosis::OtherError;
            }

            if (split_uri_view->scheme == "https" || split_uri_view->scheme == "http")
            {
                auto maybe_authority = split_uri_view->authority.get();
                if (!maybe_authority)
                {
                    context.report_error(msg::format(msgInvalidUri, msg::value = sanitized_url));
                    return DownloadPrognosis::OtherError;
                }

                auto authority = StringView{*maybe_authority}.substr(2);
                // This check causes complex URLs (non-default port, embedded basic auth) to be passed down to
                // curl.exe
                if (Strings::find_first_of(authority, ":@") == authority.end())
                {
                    if (!download_winhttp(context,
                                          machine_readable_progress,
                                          fs,
                                          download_path_part_path,
                                          *split_uri_view,
                                          sanitized_url))
                    {
                        return DownloadPrognosis::NetworkErrorProxyMightHelp;
                    }

                    if (!check_downloaded_file_hash(
                            context, fs, sanitized_url, download_path_part_path, maybe_sha512, out_sha512))
                    {
                        return DownloadPrognosis::OtherError;
                    }

                    fs.rename(download_path_part_path, download_path, VCPKG_LINE_INFO);
                    return DownloadPrognosis::Success;
                }
            }
        }
#endif
        // Create directory in advance, otherwise curl will create it in 750 mode on unix style file systems.
        const auto dir = download_path_part_path.parent_path();
        if (!dir.empty())
        {
            fs.create_directories(dir, VCPKG_LINE_INFO);
        }

        std::error_code ec;
        auto fileptr = std::make_unique<WriteFilePointer>(download_path_part_path, Append::NO, ec);
        if (ec)
        {
            context.report_error(format_filesystem_call_error(ec, "fopen", {download_path_part_path}));
            return DownloadPrognosis::OtherError;
        }

        curl_slist* request_headers = nullptr;
        for (auto&& header : headers)
            request_headers = curl_slist_append(request_headers, header.c_str());

        auto curl = curl_easy_init();
        if (!curl) Checks::unreachable(VCPKG_LINE_INFO);

        set_common_curl_options(curl, url_encode_spaces(raw_url).c_str(), request_headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &write_file_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, fileptr.get());
        // TODO: Add progress
        (void)machine_readable_progress;

        // Retry on transient errors:
        // Transient error means either: a timeout, an FTP 4xx response code or an HTTP 408, 429, 500, 502, 503 or 504
        // response code.
        bool curl_success = false;
        bool should_retry = true;
        size_t retries_count = 0;

        using namespace std::chrono_literals;
        static constexpr std::array<std::chrono::seconds, 3> retry_delay = {0s, 1s, 2s};
        do
        {
            // blocking transfer
            should_retry = false;
            std::this_thread::sleep_for(retry_delay[retries_count++]);
            auto curl_code = curl_easy_perform(curl);
            if (curl_code == CURLE_OK)
            {
                long response_code = -1;
                if (CURLE_OK == curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code))
                {
                    if ((response_code >= 200 && response_code < 300) || response_code == 0)
                    {
                        curl_success = true;
                        break;
                    }
                    else if (response_code == 429 || response_code == 408 || response_code == 500 ||
                             response_code == 502 || response_code == 503 || response_code == 504)
                    {
                        should_retry = true;
                        context.report_error(msg::format(msgCurlFailedHttpResponseWithRetry,
                                                         msg::exit_code = static_cast<int>(curl_code),
                                                         msg::count = retries_count,
                                                         msg::value = retry_delay.size()));
                    }
                    else
                    {
                        context.report_error(
                            msg::format(msgCurlFailedHttpResponse, msg::exit_code = static_cast<int>(curl_code)));
                    }
                }
            }
            else
            {
                if (curl_code == CURLE_OPERATION_TIMEDOUT)
                {
                    should_retry = true;
                    context.report_error(msg::format(msgCurlFailedGenericWithRetry,
                                                     msg::exit_code = static_cast<int>(curl_code),
                                                     msg::error_msg = curl_easy_strerror(curl_code),
                                                     msg::count = retries_count,
                                                     msg::value = retry_delay.size()));
                }
                else
                {
                    context.report_error(msg::format(msgCurlFailedGeneric,
                                                     msg::exit_code = static_cast<int>(curl_code),
                                                     msg::error_msg = curl_easy_strerror(curl_code)));
                }
            }
        } while (should_retry && retries_count < retry_delay.size());

        curl_easy_cleanup(curl);
        curl_slist_free_all(request_headers);
        fileptr.reset();

        if (!curl_success)
        {
            return DownloadPrognosis::NetworkErrorProxyMightHelp;
        }

        if (!check_downloaded_file_hash(context, fs, sanitized_url, download_path_part_path, maybe_sha512, out_sha512))
        {
            return DownloadPrognosis::OtherError;
        }

        fs.rename(download_path_part_path, download_path, VCPKG_LINE_INFO);
        return DownloadPrognosis::Success;
    }

    View<std::string> azure_blob_headers()
    {
        static std::string s_headers[2] = {"x-ms-version: 2020-04-08", "x-ms-blob-type: BlockBlob"};
        return s_headers;
    }

    static DownloadPrognosis download_file_azurl_asset_cache(DiagnosticContext& context,
                                                             MessageSink& machine_readable_progress,
                                                             const AssetCachingSettings& asset_cache_settings,
                                                             const Filesystem& fs,
                                                             const Path& download_path,
                                                             StringView display_path,
                                                             const StringView* maybe_sha512,
                                                             std::string* out_sha512)
    {
        auto read_template = asset_cache_settings.m_read_url_template.get();
        if (!read_template || !maybe_sha512)
        {
            // can't use http asset caches when none are configured or we don't have a SHA
            return DownloadPrognosis::OtherError;
        }

        auto raw_read_url = Strings::replace_all(*read_template, "<SHA>", *maybe_sha512);
        SanitizedUrl sanitized_read_url{raw_read_url, asset_cache_settings.m_secrets};
        context.statusln(msg::format(msgAssetCacheConsult, msg::path = display_path, msg::url = sanitized_read_url));
        return try_download_file(context,
                                 machine_readable_progress,
                                 fs,
                                 raw_read_url,
                                 sanitized_read_url,
                                 asset_cache_settings.m_read_headers,
                                 download_path,
                                 maybe_sha512,
                                 out_sha512);
    }

    static void report_script_while_command_line(DiagnosticContext& context, const std::string& raw_command)
    {
        context.report(DiagnosticLine{
            DiagKind::Note,
            msg::format(msgWhileRunningAssetCacheScriptCommandLine).append_raw(": ").append_raw(raw_command)});
    }

    static void report_script_failed_to_make_file(DiagnosticContext& context,
                                                  const std::string& raw_command,
                                                  const Path& download_path_part_path)
    {
        context.report(DiagnosticLine{
            DiagKind::Error, download_path_part_path, msg::format(msgAssetCacheScriptFailedToWriteFile)});
        context.report(DiagnosticLine{
            DiagKind::Note, msg::format(msgAssetCacheScriptCommandLine).append_raw(": ").append_raw(raw_command)});
    }

    static void report_asset_cache_authoritative_urls(DiagnosticContext& context,
                                                      DiagKind first_message_kind,
                                                      msg::MessageT<msg::url_t> first_message,
                                                      const std::vector<SanitizedUrl>& sanitized_urls)
    {
        auto first_sanitized_url = sanitized_urls.begin();
        const auto last_sanitized_url = sanitized_urls.end();
        if (first_sanitized_url != last_sanitized_url)
        {
            context.report(
                DiagnosticLine{first_message_kind, msg::format(first_message, msg::url = *first_sanitized_url)});
            while (++first_sanitized_url != last_sanitized_url)
            {
                context.report(
                    DiagnosticLine{DiagKind::Note, msg::format(msgDownloadOrUrl, msg::url = *first_sanitized_url)});
            }
        }
    }

    static DownloadPrognosis download_file_script_asset_cache(DiagnosticContext& context,
                                                              const AssetCachingSettings& asset_cache_settings,
                                                              const Filesystem& fs,
                                                              View<std::string> raw_urls,
                                                              const std::vector<SanitizedUrl>& sanitized_urls,
                                                              const Path& download_path,
                                                              StringView display_path,
                                                              const StringView* maybe_sha512,
                                                              std::string* out_sha512)
    {
        using Hash::HashPrognosis;
        auto script = asset_cache_settings.m_script.get();
        if (!script)
        {
            return DownloadPrognosis::OtherError;
        }

        if (raw_urls.empty() && !maybe_sha512)
        {
            Checks::unreachable(VCPKG_LINE_INFO);
        }

        context.statusln(msg::format(msgAssetCacheConsultScript, msg::path = display_path));
        const auto download_path_part_path =
            fmt::format("{}.{}.part", fs.absolute(download_path, VCPKG_LINE_INFO), get_process_id());
        Lazy<std::string> escaped_url;
        const auto escaped_dpath = Command(download_path_part_path).extract();
        auto maybe_raw_command = api_stable_format(context, *script, [&](std::string& out, StringView key) {
            if (key == "url")
            {
                if (raw_urls.empty())
                {
                    if (!maybe_sha512)
                    {
                        Checks::unreachable(VCPKG_LINE_INFO);
                    }

                    context.report_error(
                        msg::format(msgAssetCacheScriptNeedsUrl, msg::value = *script, msg::sha = *maybe_sha512));
                    return false;
                }

                Strings::append(out, escaped_url.get_lazy([&] { return Command(raw_urls[0]).extract(); }));
                return true;
            }

            if (key == "sha512")
            {
                if (maybe_sha512)
                {
                    out.append(maybe_sha512->data(), maybe_sha512->size());
                    return true;
                }

                context.report_error(
                    msg::format(msgAssetCacheScriptNeedsSha, msg::value = *script, msg::url = sanitized_urls[0]));
                return false;
            }

            if (key == "dst")
            {
                Strings::append(out, escaped_dpath);
                return true;
            }

            context.report_error(msg::format(msgAssetCacheScriptBadVariable, msg::value = *script, msg::list = key));
            context.report(
                DiagnosticLine{DiagKind::Note, msg::format(msgAssetCacheScriptBadVariableHint, msg::list = key)});
            return false;
        });

        auto raw_command = maybe_raw_command.get();
        if (!raw_command)
        {
            return DownloadPrognosis::OtherError;
        }

        Command cmd;
        cmd.raw_arg(*raw_command);
        RedirectedProcessLaunchSettings settings;
        settings.environment = get_clean_environment();
        auto maybe_res = cmd_execute_and_stream_lines(
            context, cmd, settings, [&](StringView line) { context.statusln(LocalizedString::from_raw(line)); });
        auto res = maybe_res.get();
        if (!res)
        {
            report_script_while_command_line(context, *raw_command);
            return DownloadPrognosis::OtherError;
        }

        if (*res != 0)
        {
            context.report_error(msg::format(msgAssetCacheScriptFailed, msg::exit_code = *res));
            context.report(DiagnosticLine{
                DiagKind::Note, msg::format(msgAssetCacheScriptCommandLine).append_raw(": ").append_raw(*raw_command)});
            return DownloadPrognosis::OtherError;
        }

        if (maybe_sha512)
        {
            auto hash_result = Hash::get_file_hash(context, fs, download_path_part_path, Hash::Algorithm::Sha512);
            switch (hash_result.prognosis)
            {
                case HashPrognosis::Success:
                    if (Strings::case_insensitive_ascii_equals(*maybe_sha512, hash_result.hash))
                    {
                        if (out_sha512)
                        {
                            *out_sha512 = std::move(hash_result.hash);
                        }

                        fs.rename(download_path_part_path, download_path, VCPKG_LINE_INFO);
                        return DownloadPrognosis::Success;
                    }

                    context.report(DiagnosticLine{DiagKind::Error,
                                                  download_path_part_path,
                                                  msg::format(msgAssetCacheScriptFailedToWriteCorrectHash)});
                    context.report(DiagnosticLine{
                        DiagKind::Note,
                        msg::format(msgAssetCacheScriptCommandLine).append_raw(": ").append_raw(*raw_command)});
                    context.report(DiagnosticLine{
                        DiagKind::Note,
                        msg::format(msgDownloadFailedHashMismatchExpectedHash, msg::sha = *maybe_sha512)});
                    context.report(DiagnosticLine{
                        DiagKind::Note,
                        msg::format(msgDownloadFailedHashMismatchActualHash, msg::sha = hash_result.hash)});
                    if (out_sha512)
                    {
                        *out_sha512 = std::move(hash_result.hash);
                    }

                    return DownloadPrognosis::OtherError;
                case HashPrognosis::FileNotFound:
                    report_script_failed_to_make_file(context, *raw_command, download_path_part_path);
                    return DownloadPrognosis::OtherError;
                case HashPrognosis::OtherError:
                    report_script_while_command_line(context, *raw_command);
                    return DownloadPrognosis::OtherError;
                default: Checks::unreachable(VCPKG_LINE_INFO);
            }
        }

        if (fs.exists(download_path_part_path, VCPKG_LINE_INFO))
        {
            fs.rename(download_path_part_path, download_path, VCPKG_LINE_INFO);
            return DownloadPrognosis::Success;
        }

        report_script_failed_to_make_file(context, *raw_command, download_path_part_path);
        return DownloadPrognosis::OtherError;
    }

    static DownloadPrognosis download_file_asset_cache(DiagnosticContext& context,
                                                       MessageSink& machine_readable_progress,
                                                       const AssetCachingSettings& asset_cache_settings,
                                                       const Filesystem& fs,
                                                       View<std::string> raw_urls,
                                                       const std::vector<SanitizedUrl>& sanitized_urls,
                                                       const Path& download_path,
                                                       StringView display_path,
                                                       const StringView* maybe_sha512,
                                                       std::string* out_sha512)
    {
        switch (download_file_azurl_asset_cache(context,
                                                machine_readable_progress,
                                                asset_cache_settings,
                                                fs,
                                                download_path,
                                                display_path,
                                                maybe_sha512,
                                                out_sha512))
        {
            case DownloadPrognosis::Success: return DownloadPrognosis::Success;
            case DownloadPrognosis::OtherError:
                return download_file_script_asset_cache(context,
                                                        asset_cache_settings,
                                                        fs,
                                                        raw_urls,
                                                        sanitized_urls,
                                                        download_path,
                                                        display_path,
                                                        maybe_sha512,
                                                        out_sha512);
            case DownloadPrognosis::NetworkErrorProxyMightHelp: return DownloadPrognosis::NetworkErrorProxyMightHelp;
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }
    }
    static void report_download_success_and_maybe_upload(DiagnosticContext& context,
                                                         const Path& download_path,
                                                         StringView display_path,
                                                         const AssetCachingSettings& asset_cache_settings,
                                                         const StringView* maybe_sha512)
    {
        auto url_template = asset_cache_settings.m_write_url_template.get();
        if (maybe_sha512 && url_template && !url_template->empty())
        {
            auto raw_upload_url = Strings::replace_all(*url_template, "<SHA>", *maybe_sha512);
            SanitizedUrl sanitized_upload_url{raw_upload_url, asset_cache_settings.m_secrets};
            context.statusln(
                msg::format(msgDownloadSuccesfulUploading, msg::path = display_path, msg::url = sanitized_upload_url));
            WarningDiagnosticContext wdc{context};
            if (!store_to_asset_cache(wdc,
                                      raw_upload_url,
                                      sanitized_upload_url,
                                      "PUT",
                                      asset_cache_settings.m_write_headers,
                                      download_path))
            {
                context.report(DiagnosticLine{DiagKind::Warning,
                                              msg::format(msgFailedToStoreBackToMirror,
                                                          msg::path = display_path,
                                                          msg::url = sanitized_upload_url)});
            }
        }
        else
        {
            context.statusln(msg::format(msgDownloadSuccesful, msg::path = display_path));
        }
    }

    bool download_file_asset_cached(DiagnosticContext& context,
                                    MessageSink& machine_readable_progress,
                                    const AssetCachingSettings& asset_cache_settings,
                                    const Filesystem& fs,
                                    const std::string& url,
                                    View<std::string> headers,
                                    const Path& download_path,
                                    StringView display_path,
                                    const Optional<std::string>& maybe_sha512)
    {
        return download_file_asset_cached(context,
                                          machine_readable_progress,
                                          asset_cache_settings,
                                          fs,
                                          View<std::string>(&url, 1),
                                          headers,
                                          download_path,
                                          display_path,
                                          maybe_sha512);
    }

    static bool download_file_asset_cached_sanitized_sha(DiagnosticContext& context,
                                                         MessageSink& machine_readable_progress,
                                                         const AssetCachingSettings& asset_cache_settings,
                                                         const Filesystem& fs,
                                                         View<std::string> raw_urls,
                                                         View<std::string> headers,
                                                         const Path& download_path,
                                                         StringView display_path,
                                                         const StringView* maybe_sha512,
                                                         std::string* out_sha512)
    {
        // Design goals:
        // * We want it to be clear when asset cache(s) are used. This means not printing the authoritative URL in a
        //   'downloading' message when we aren't looking at it.
        // * We don't want to say that something is an error / failure unless it actually is. This means asset cache
        //   failures followed by authoritative success must print only success. This also means that we can't print
        //   asset cache errors immediately, since they might be 'eaten' by a subsequent authoritative success.
        // * We want to print something before 'going to sleep' for network access ever, so if the machine where that
        //   network access is is being slow or whatever the user understands.
        // * We want to print errors and warnings as close to when they happen as possible notwithstanding other goals.
        // * We want to print the proxy warning if and only if a failure looks like it might be something a proxy could
        //   fix. For example, successful network access with the wrong SHA is not proxy-fixable.
        // * If we are printing the proxy message, we want to take some effort to only print it once, and put it on the
        //   *last* HTTP failure we print. This avoids a ton of console spew and makes it likely to be near the end of
        //   failure output and thus not scrolled off the top of the console buffer.
        // * We consider hash check failure the same as a network I/O failure, and let other sources 'fix' the problem.
        //
        // See examples of console output in asset-caching.ps1

        // Note: no secrets for the input URLs
        std::vector<SanitizedUrl> sanitized_urls =
            Util::fmap(raw_urls, [&](const std::string& url) { return SanitizedUrl{url, {}}; });
        const auto last_sanitized_url = sanitized_urls.end();
        bool can_read_asset_cache = false;
        if (asset_cache_settings.m_read_url_template.has_value() && maybe_sha512)
        {
            // url asset cache reads need a hash
            can_read_asset_cache = true;
        }

        if (asset_cache_settings.m_script.has_value() && (maybe_sha512 || !raw_urls.empty()))
        {
            // script asset cache reads need either a hash or a URL
            can_read_asset_cache = true;
        }

        if (raw_urls.empty())
        {
            // try to fetch from asset cache only without a known URL
            if (maybe_sha512)
            {
                if (can_read_asset_cache)
                {
                    context.statusln(
                        msg::format(msgDownloadingAssetShaToFile, msg::sha = *maybe_sha512, msg::path = display_path));
                }
                else
                {
                    context.report_error(msg::format(
                        msgDownloadingAssetShaWithoutAssetCache, msg::sha = *maybe_sha512, msg::path = display_path));
                    return false;
                }
            }
            else
            {
                context.report_error(msgNoUrlsAndNoHashSpecified);
                return false;
            }
        }

        if (asset_cache_settings.m_block_origin && !can_read_asset_cache)
        {
            // this will emit msgAssetCacheMissBlockOrigin below, this message just ensures the filename is mentioned in
            // the output at all
            context.statusln(msg::format(msgDownloadingFile, msg::path = display_path));
        }

        DownloadPrognosis asset_cache_prognosis = DownloadPrognosis::Success;
        // the asset cache downloads might fail, but that's OK if we can download the file from an authoritative source
        AttemptDiagnosticContext asset_cache_attempt_context{context};
        if (check_combine_download_prognosis(asset_cache_prognosis,
                                             download_file_asset_cache(asset_cache_attempt_context,
                                                                       machine_readable_progress,
                                                                       asset_cache_settings,
                                                                       fs,
                                                                       raw_urls,
                                                                       sanitized_urls,
                                                                       download_path,
                                                                       display_path,
                                                                       maybe_sha512,
                                                                       out_sha512)))
        {
            asset_cache_attempt_context.commit();
            if (raw_urls.empty())
            {
                context.statusln(msg::format(msgAssetCacheHit));
                return true;
            }

            auto first_sanitized_url = sanitized_urls.begin();
            LocalizedString overall_url;
            overall_url.append_raw(first_sanitized_url->to_string());
            while (++first_sanitized_url != last_sanitized_url)
            {
                overall_url.append_raw(", ").append(msgDownloadOrUrl, msg::url = *first_sanitized_url);
            }

            context.statusln(msg::format(msgAssetCacheHitUrl, msg::url = overall_url));
            return true;
        }

        if (raw_urls.empty())
        {
            asset_cache_attempt_context.commit();
            if (!maybe_sha512)
            {
                Checks::unreachable(VCPKG_LINE_INFO);
            }

            context.report_error(msg::format(msgAssetCacheMissNoUrls, msg::sha = *maybe_sha512));
            maybe_report_proxy_might_help(context, asset_cache_prognosis);
            return false;
        }

        if (asset_cache_settings.m_block_origin)
        {
            asset_cache_attempt_context.commit();
            report_asset_cache_authoritative_urls(
                context, DiagKind::Error, msgAssetCacheMissBlockOrigin, sanitized_urls);
            maybe_report_proxy_might_help(context, asset_cache_prognosis);
            return false;
        }

        auto first_raw_url = raw_urls.begin();
        const auto last_raw_url = raw_urls.end();
        auto first_sanitized_url = sanitized_urls.begin();
        AttemptDiagnosticContext authoritative_attempt_context{context};
        DownloadPrognosis authoritative_prognosis = DownloadPrognosis::Success;
        if (can_read_asset_cache)
        {
            context.statusln(msg::format(msgAssetCacheMiss, msg::url = *first_sanitized_url));
        }
        else if (raw_urls.size() == 1)
        {
            context.statusln(
                msg::format(msgDownloadingUrlToFile, msg::url = *first_sanitized_url, msg::path = display_path));
        }
        else
        {
            context.statusln(msg::format(
                msgDownloadingFileFirstAuthoritativeSource, msg::path = display_path, msg::url = *first_sanitized_url));
        }

        if (check_combine_download_prognosis(authoritative_prognosis,
                                             try_download_file(authoritative_attempt_context,
                                                               machine_readable_progress,
                                                               fs,
                                                               *first_raw_url,
                                                               *first_sanitized_url,
                                                               headers,
                                                               download_path,
                                                               maybe_sha512,
                                                               out_sha512)))
        {
            asset_cache_attempt_context.handle();
            authoritative_attempt_context.handle();
            report_download_success_and_maybe_upload(
                context, download_path, display_path, asset_cache_settings, maybe_sha512);
            return true;
        }

        while (++first_sanitized_url, ++first_raw_url != last_raw_url)
        {
            context.statusln(msg::format(msgDownloadTryingAuthoritativeSource, msg::url = *first_sanitized_url));
            if (check_combine_download_prognosis(authoritative_prognosis,
                                                 try_download_file(authoritative_attempt_context,
                                                                   machine_readable_progress,
                                                                   fs,
                                                                   *first_raw_url,
                                                                   *first_sanitized_url,
                                                                   headers,
                                                                   download_path,
                                                                   maybe_sha512,
                                                                   out_sha512)))
            {
                asset_cache_attempt_context.handle();
                authoritative_attempt_context.handle();
                report_download_success_and_maybe_upload(
                    context, download_path, display_path, asset_cache_settings, maybe_sha512);
                return true;
            }
        }

        if (asset_cache_prognosis == DownloadPrognosis::NetworkErrorProxyMightHelp &&
            authoritative_prognosis != DownloadPrognosis::NetworkErrorProxyMightHelp)
        {
            // reorder the proxy warning up to the asset cache prognosis if that's where it comes from
            asset_cache_attempt_context.commit();
            maybe_report_proxy_might_help(context, asset_cache_prognosis);
            authoritative_attempt_context.commit();
            return false;
        }

        check_combine_download_prognosis(authoritative_prognosis, asset_cache_prognosis);
        asset_cache_attempt_context.commit();
        authoritative_attempt_context.commit();
        maybe_report_proxy_might_help(context, authoritative_prognosis);
        return false;
    }

    bool download_file_asset_cached(DiagnosticContext& context,
                                    MessageSink& machine_readable_progress,
                                    const AssetCachingSettings& asset_cache_settings,
                                    const Filesystem& fs,
                                    View<std::string> raw_urls,
                                    View<std::string> headers,
                                    const Path& download_path,
                                    StringView display_path,
                                    const Optional<std::string>& maybe_sha512_mixed_case)
    {
        if (auto sha512_mixed_case = maybe_sha512_mixed_case.get())
        {
            static constexpr StringLiteral all_zero_sha =
                "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
                "00000000000000000000000000";
            if (*sha512_mixed_case == all_zero_sha)
            {
                std::string actual_sha512;
                if (download_file_asset_cached_sanitized_sha(context,
                                                             machine_readable_progress,
                                                             asset_cache_settings,
                                                             fs,
                                                             raw_urls,
                                                             headers,
                                                             download_path,
                                                             display_path,
                                                             nullptr,
                                                             &actual_sha512))
                {
                    context.report_error(msg::format(msgDownloadFailedHashMismatchZero, msg::sha = actual_sha512));
                }

                return false;
            }

            auto sha512 = Strings::ascii_to_lowercase(*sha512_mixed_case);
            StringView sha512sv = sha512;
            return download_file_asset_cached_sanitized_sha(context,
                                                            machine_readable_progress,
                                                            asset_cache_settings,
                                                            fs,
                                                            raw_urls,
                                                            headers,
                                                            download_path,
                                                            display_path,
                                                            &sha512sv,
                                                            nullptr);
        }

        return download_file_asset_cached_sanitized_sha(context,
                                                        machine_readable_progress,
                                                        asset_cache_settings,
                                                        fs,
                                                        raw_urls,
                                                        headers,
                                                        download_path,
                                                        display_path,
                                                        nullptr,
                                                        nullptr);
    }

    bool store_to_asset_cache(DiagnosticContext& context,
                              const AssetCachingSettings& asset_cache_settings,
                              const Path& file_to_put,
                              StringView sha512)
    {
        if (auto url_template = asset_cache_settings.m_write_url_template.get())
        {
            if (url_template->empty())
            {
                return true;
            }

            auto raw_upload_url = Strings::replace_all(*url_template, "<SHA>", sha512);
            SanitizedUrl sanitized_upload_url{raw_upload_url, asset_cache_settings.m_secrets};
            return store_to_asset_cache(context,
                                        raw_upload_url,
                                        sanitized_upload_url,
                                        "PUT",
                                        asset_cache_settings.m_write_headers,
                                        file_to_put);
        }

        return true;
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
