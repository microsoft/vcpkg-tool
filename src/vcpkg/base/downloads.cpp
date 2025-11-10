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

#include <set>

using namespace vcpkg;

namespace
{
    void set_common_curl_easy_options(CurlEasyHandle& easy_handle, StringView url, const CurlHeaders& request_headers)
    {
        auto* curl = easy_handle.get();
        curl_easy_setopt(curl, CURLOPT_USERAGENT, vcpkg_curl_user_agent);
        curl_easy_setopt(curl, CURLOPT_URL, url_encode_spaces(url).c_str());
        curl_easy_setopt(curl,
                         CURLOPT_FOLLOWLOCATION,
                         2L); // Follow redirects, change request method based on HTTP response code.
                              // https://curl.se/libcurl/c/CURLOPT_FOLLOWLOCATION.html#CURLFOLLOWOBEYCODE
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, request_headers.get());
        curl_easy_setopt(curl, CURLOPT_HEADEROPT, CURLHEADER_SEPARATE); // don't send headers to proxy CONNECT
    }
}

namespace vcpkg
{
    SanitizedUrl::SanitizedUrl(StringView raw_url, View<std::string> secrets)
        : m_sanitized_url(raw_url.data(), raw_url.size())
    {
        replace_secrets(m_sanitized_url, secrets);
    }

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

    static size_t write_file_callback(void* contents, size_t size, size_t nmemb, void* param)
    {
        if (!param) return 0;
        return static_cast<WriteFilePointer*>(param)->write(contents, size, nmemb);
    }

    static size_t progress_callback(
        void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
    {
        (void)ultotal;
        (void)ulnow;
        auto machine_readable_progress = static_cast<MessageSink*>(clientp);
        if (dltotal && machine_readable_progress)
        {
            double percentage = static_cast<double>(dlnow) / static_cast<double>(dltotal) * 100.0;
            machine_readable_progress->println(LocalizedString::from_raw(fmt::format("{:.2f}%", percentage)));
        }
        return 0;
    }

    static std::vector<int> libcurl_bulk_operation(DiagnosticContext& context,
                                                   View<std::string> urls,
                                                   View<Path> outputs,
                                                   View<std::string> headers)
    {
        if (!outputs.empty() && outputs.size() != urls.size())
        {
            Checks::unreachable(VCPKG_LINE_INFO);
        }

        std::vector<int> return_codes(urls.size(), -1);

        CurlHeaders request_headers(headers);

        std::vector<WriteFilePointer> write_pointers;
        write_pointers.reserve(urls.size());

        std::vector<CurlEasyHandle> easy_handles;
        easy_handles.resize(urls.size());

        CurlMultiHandle multi_handle;
        for (size_t request_index = 0; request_index < urls.size(); ++request_index)
        {
            const auto& url = urls[request_index];
            auto& easy_handle = easy_handles[request_index];
            auto* curl = easy_handle.get();

            set_common_curl_easy_options(easy_handle, url, request_headers);
            if (outputs.empty())
            {
                curl_easy_setopt(curl, CURLOPT_PRIVATE, reinterpret_cast<void*>(static_cast<uintptr_t>(request_index)));
            }
            else
            {
                const auto& output = outputs[request_index];
                std::error_code ec;
                auto& request_write_pointer = write_pointers.emplace_back(output, Append::NO, ec);
                if (ec)
                {
                    context.report_error(format_filesystem_call_error(ec, "fopen", {output}));
                    Checks::unreachable(VCPKG_LINE_INFO);
                }

                curl_easy_setopt(curl, CURLOPT_PRIVATE, static_cast<void*>(&request_write_pointer));
                // note explicit cast to void* necessary to go through ...
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, static_cast<void*>(&request_write_pointer));
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &write_file_callback);
                multi_handle.add_easy_handle(easy_handle);
            }
        }

        int still_running = 0;
        do
        {
            CURLMcode mc = curl_multi_perform(multi_handle.get(), &still_running);
            if (mc != CURLM_OK)
            {
                Debug::println("curl_multi_perform failed:");
                Debug::println(msg::format(msgCurlFailedGeneric, msg::exit_code = static_cast<int>(mc))
                                   .append_raw(fmt::format(" ({}).", curl_multi_strerror(mc))));
                Checks::unreachable(VCPKG_LINE_INFO);
            }

            mc = curl_multi_poll(multi_handle.get(), nullptr, 0, 1000, nullptr);
            if (mc != CURLM_OK)
            {
                Debug::println("curl_multi_poll failed:");
                Debug::println(msg::format(msgCurlFailedGeneric, msg::exit_code = static_cast<int>(mc))
                                   .append_raw(fmt::format(" ({}).", curl_multi_strerror(mc))));
                Checks::unreachable(VCPKG_LINE_INFO);
            }
        } while (still_running);

        // drain all messages
        int messages_in_queue = 0;
        while (auto* msg = curl_multi_info_read(multi_handle.get(), &messages_in_queue))
        {
            if (msg->msg == CURLMSG_DONE)
            {
                CURL* handle = msg->easy_handle;
                if (msg->data.result == CURLE_OK)
                {
                    size_t idx;
                    void* curlinfo_private;
                    curl_easy_getinfo(handle, CURLINFO_PRIVATE, &curlinfo_private);
                    if (outputs.empty())
                    {
                        idx = reinterpret_cast<uintptr_t>(curlinfo_private);
                    }
                    else
                    {
                        if (!curlinfo_private)
                        {
                            Checks::unreachable(VCPKG_LINE_INFO);
                        }
                        auto request_write_handle = static_cast<WriteFilePointer*>(curlinfo_private);
                        idx = request_write_handle - write_pointers.data();
                    }

                    long response_code;
                    curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &response_code);
                    return_codes[idx] = static_cast<int>(response_code);
                }
                else
                {
                    context.report_error(
                        msg::format(msgCurlFailedGeneric, msg::exit_code = static_cast<int>(msg->data.result))
                            .append_raw(fmt::format(" ({}).", curl_easy_strerror(msg->data.result))));
                }
            }
        }
        return return_codes;
    }

    static std::vector<int> libcurl_bulk_check(DiagnosticContext& context,
                                               View<std::string> urls,
                                               View<std::string> headers)
    {
        return libcurl_bulk_operation(context,
                                      urls,
                                      {}, // no output
                                      headers);
    }

    std::vector<int> url_heads(DiagnosticContext& context, View<std::string> urls, View<std::string> headers)
    {
        return libcurl_bulk_check(context, urls, headers);
    }

    std::vector<int> download_files_no_cache(DiagnosticContext& context,
                                             View<std::pair<std::string, Path>> url_pairs,
                                             View<std::string> headers)
    {
        return libcurl_bulk_operation(context,
                                      Util::fmap(url_pairs, [](auto&& kv) -> std::string { return kv.first; }),
                                      Util::fmap(url_pairs, [](auto&& kv) -> Path { return kv.second; }),
                                      headers);
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

        CurlEasyHandle handle;
        CURL* curl = handle.get();

        std::string post_data = Json::stringify(snapshot);

        std::string headers[]{
            "Accept: application/vnd.github+json",
            ("Authorization: Bearer " + github_token),
            "X-GitHub-Api-Version: 2022-11-28",
            "Content-Type: application/json",
        };

        CurlHeaders request_headers(headers);
        set_common_curl_easy_options(handle, uri, request_headers);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, vcpkg_curl_user_agent);
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, post_data.length());

        CURLcode result = curl_easy_perform(curl);
        long response_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

        if (result != CURLE_OK)
        {
            context.report_error(msg::format(msgCurlFailedGeneric, msg::exit_code = static_cast<int>(result))
                                     .append_raw(fmt::format(" ({}).", curl_easy_strerror(result))));
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
                              View<std::string> headers,
                              const Path& file)
    {
        std::error_code ec;
        ReadFilePointer fileptr(file, ec);
        if (ec)
        {
            context.report_error(format_filesystem_call_error(ec, "fopen", {file}));
            return false;
        }
        auto file_size = fileptr.size(ec);
        if (ec)
        {
            context.report_error(format_filesystem_call_error(ec, "fstat", {file}));
            return false;
        }

        CurlEasyHandle handle;
        CURL* curl = handle.get();

        auto request_headers = raw_url.starts_with("ftp://") ? CurlHeaders() : CurlHeaders(headers);
        auto upload_url = url_encode_spaces(raw_url);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, vcpkg_curl_user_agent);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, request_headers.get());
        curl_easy_setopt(curl, CURLOPT_URL, upload_url.c_str());
        curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
        curl_easy_setopt(curl, CURLOPT_READDATA, static_cast<void*>(&fileptr));
        curl_easy_setopt(curl, CURLOPT_READFUNCTION, &read_file_callback);
        curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, static_cast<curl_off_t>(file_size));

        auto result = curl_easy_perform(curl);
        if (result != CURLE_OK)
        {
            context.report_error(msg::format(msgCurlFailedGeneric, msg::exit_code = static_cast<int>(result))
                                     .append_raw(fmt::format(" ({}).", curl_easy_strerror(result))));
            return false;
        }

        long response_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

        if ((response_code >= 100 && response_code < 200) || response_code >= 300)
        {
            context.report_error(msg::format(msgCurlFailedToPut, msg::url = sanitized_url, msg::value = response_code));
            return false;
        }

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

    enum class DownloadPrognosis
    {
        Success,
        OtherError,
        NetworkErrorProxyMightHelp,
        // Transient error means either: a timeout, an FTP 4xx response code or an HTTP 408, 429, 500, 502, 503 or
        // 504 response code. https://everything.curl.dev/usingcurl/downloads/retry.html#retry
        TransientNetworkError
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

    static DownloadPrognosis perform_download(DiagnosticContext& context,
                                              MessageSink& machine_readable_progress,
                                              StringView raw_url,
                                              const Path& download_path,
                                              View<std::string> headers)
    {
        std::error_code ec;
        WriteFilePointer fileptr(download_path, Append::NO, ec);
        if (ec)
        {
            context.report_error(format_filesystem_call_error(ec, "fopen", {download_path}));
            return DownloadPrognosis::OtherError;
        }

        CurlHeaders request_headers(headers);

        CurlEasyHandle handle;
        CURL* curl = handle.get();
        set_common_curl_easy_options(handle, raw_url, request_headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &write_file_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, static_cast<void*>(&fileptr));
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L); // change from default to enable progress
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, &progress_callback);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, static_cast<void*>(&machine_readable_progress));
        auto curl_code = curl_easy_perform(curl);

        if (curl_code == CURLE_OPERATION_TIMEDOUT)
        {
            context.report_error(msgCurlDownloadTimeout);
            return DownloadPrognosis::TransientNetworkError;
        }

        if (curl_code != CURLE_OK)
        {
            context.report_error(msg::format(msgCurlFailedGeneric, msg::exit_code = static_cast<int>(curl_code))
                                     .append_raw(fmt::format(" ({}).", curl_easy_strerror(curl_code))));
            return DownloadPrognosis::NetworkErrorProxyMightHelp;
        }

        long response_code = -1;
        auto get_info_code = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
        if (get_info_code != CURLE_OK)
        {
            context.report_error(msg::format(msgCurlFailedGeneric, msg::exit_code = static_cast<int>(get_info_code))
                                     .append_raw(fmt::format(" ({}).", curl_easy_strerror(get_info_code))));
            return DownloadPrognosis::NetworkErrorProxyMightHelp;
        }

        if ((response_code >= 200 && response_code < 300) || (raw_url.starts_with("file://") && response_code == 0))
        {
            return DownloadPrognosis::Success;
        }

        if (response_code == 429 || response_code == 408 || response_code == 500 || response_code == 502 ||
            response_code == 503 || response_code == 504)
        {
            context.report_error(
                msg::format(msgCurlFailedHttpResponse, msg::exit_code = static_cast<int>(response_code)));
            return DownloadPrognosis::TransientNetworkError;
        }

        context.report_error(msg::format(msgCurlFailedHttpResponse, msg::exit_code = static_cast<int>(response_code)));
        return DownloadPrognosis::NetworkErrorProxyMightHelp;
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

        // Create directory in advance, otherwise curl will create it in 750 mode on unix style file systems.
        const auto dir = download_path_part_path.parent_path();
        if (!dir.empty())
        {
            fs.create_directories(dir, VCPKG_LINE_INFO);
        }

        // Retry on transient errors:
        // Transient error means either: a timeout, an FTP 4xx response code or an HTTP 408, 429, 500, 502, 503 or
        // 504 response code. https://everything.curl.dev/usingcurl/downloads/retry.html#retry
        using namespace std::chrono_literals;
        static constexpr std::array<std::chrono::seconds, 3> attempt_delays = {0s, 1s, 2s};
        DownloadPrognosis prognosis = DownloadPrognosis::NetworkErrorProxyMightHelp;
        for (size_t attempt_count = 0; attempt_count < attempt_delays.size() + 1; attempt_count++)
        {
            prognosis = perform_download(context, machine_readable_progress, raw_url, download_path_part_path, headers);

            if (DownloadPrognosis::Success == prognosis)
            {
                break;
            }

            if (DownloadPrognosis::TransientNetworkError != prognosis)
            {
                context.report_error(msg::format(msgDownloadNotTransientErrorWontRetry, msg::url = sanitized_url));
                return prognosis;
            }

            context.report_error(msg::format(
                msgDownloadTransientErrorRetry, msg::count = attempt_count + 1, msg::value = attempt_delays.size()));
            std::this_thread::sleep_for(attempt_delays[attempt_count]);
        }

        if (DownloadPrognosis::Success != prognosis)
        {
            context.report_error(msg::format(msgDownloadTransientErrorRetriesExhausted, msg::url = sanitized_url));
            return prognosis;
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
            if (!store_to_asset_cache(
                    wdc, raw_upload_url, sanitized_upload_url, asset_cache_settings.m_write_headers, download_path))
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
        else
        {
            asset_cache_attempt_context.commit();
            authoritative_attempt_context.commit();
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
            return store_to_asset_cache(
                context, raw_upload_url, sanitized_upload_url, asset_cache_settings.m_write_headers, file_to_put);
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
