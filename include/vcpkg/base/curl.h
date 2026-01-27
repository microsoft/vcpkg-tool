#pragma once

#include <vcpkg/base/fwd/span.h>

#include <vcpkg/base/pragmas.h>

#include <vcpkg/commands.version.h>

VCPKG_MSVC_WARNING(push)
// note: disable warning triggered by curl headers
// ws2tcpip.h(968): warning C6101: Returning uninitialized memory '*Mtu':  A successful path through the function does
// not set the named _Out_ parameter.
VCPKG_MSVC_WARNING(disable : 6101)
#include <curl/curl.h>
#include <curl/multi.h>
VCPKG_MSVC_WARNING(pop)

#ifdef VCPKG_LIBCURL_DLSYM
void vcpkg_curl_global_init(long flags);

// these are filled in by the call to vcpkg_curl_global_init
extern decltype(&curl_easy_cleanup) vcpkg_curl_easy_cleanup;
extern decltype(&curl_easy_getinfo) vcpkg_curl_easy_getinfo;
extern decltype(&curl_easy_init) vcpkg_curl_easy_init;
extern decltype(&curl_easy_perform) vcpkg_curl_easy_perform;
extern decltype(&curl_easy_setopt) vcpkg_curl_easy_setopt;
extern decltype(&curl_easy_strerror) vcpkg_curl_easy_strerror;

extern decltype(&curl_multi_add_handle) vcpkg_curl_multi_add_handle;
extern decltype(&curl_multi_cleanup) vcpkg_curl_multi_cleanup;
extern decltype(&curl_multi_info_read) vcpkg_curl_multi_info_read;
extern decltype(&curl_multi_init) vcpkg_curl_multi_init;
extern decltype(&curl_multi_remove_handle) vcpkg_curl_multi_remove_handle;
extern decltype(&curl_multi_strerror) vcpkg_curl_multi_strerror;
extern decltype(&curl_multi_perform) vcpkg_curl_multi_perform;
extern decltype(&curl_multi_wait) vcpkg_curl_multi_poll; // or _wait, if _poll is not present

extern decltype(&curl_slist_append) vcpkg_curl_slist_append;
extern decltype(&curl_slist_free_all) vcpkg_curl_slist_free_all;

extern decltype(&curl_version) vcpkg_curl_version;
#else // ^^^ VCPKG_LIBCURL_DLSYM / !VCPKG_LIBCURL_DLSYM vvv
#define vcpkg_curl_global_init(flags) curl_global_init(flags)

#define vcpkg_curl_easy_cleanup(handle) curl_easy_cleanup(handle)
#define vcpkg_curl_easy_getinfo(handle, info, data) curl_easy_getinfo(handle, info, data)
#define vcpkg_curl_easy_init() curl_easy_init()
#define vcpkg_curl_easy_perform(handle) curl_easy_perform(handle)
#define vcpkg_curl_easy_setopt(handle, option, parameter) curl_easy_setopt(handle, option, parameter)
#define vcpkg_curl_easy_strerror(code) curl_easy_strerror(code)

#define vcpkg_curl_multi_add_handle(multi_handle, easy_handle) curl_multi_add_handle(multi_handle, easy_handle)
#define vcpkg_curl_multi_cleanup(multi_handle) curl_multi_cleanup(multi_handle)
#define vcpkg_curl_multi_info_read(multi_handle, messages_in_queue) \
    curl_multi_info_read(multi_handle, messages_in_queue)
#define vcpkg_curl_multi_init() curl_multi_init()
#define vcpkg_curl_multi_remove_handle(multi_handle, easy_handle) \
    curl_multi_remove_handle(multi_handle, easy_handle)
#define vcpkg_curl_multi_strerror(code) curl_multi_strerror(code)
#define vcpkg_curl_multi_poll(multi_handle, extra_fds, extra_nfds, timeout_ms, numfds) \
    curl_multi_poll(multi_handle, extra_fds, extra_nfds, timeout_ms, numfds)
#define vcpkg_curl_multi_perform(multi_handle, running_handles) \
    curl_multi_perform(multi_handle, running_handles)

#define vcpkg_curl_slist_append(list, string) curl_slist_append(list, string)
#define vcpkg_curl_slist_free_all(list) curl_slist_free_all(list)

#define vcpkg_curl_version() curl_version()
#endif // ^^^ !VCPKG_LIBCURL_DLSYM

namespace vcpkg
{
    struct CurlEasyHandle
    {
        CurlEasyHandle() = default;
        CurlEasyHandle(CurlEasyHandle&& other) noexcept;
        CurlEasyHandle& operator=(CurlEasyHandle&& other) noexcept;
        ~CurlEasyHandle();

        CURL* get();

    private:
        CURL* m_ptr = nullptr;
    };

    struct CurlMultiHandle
    {
        CurlMultiHandle();
        CurlMultiHandle(CurlMultiHandle&& other) noexcept;
        CurlMultiHandle& operator=(CurlMultiHandle&& other) noexcept;
        ~CurlMultiHandle();

        // Adds an easy handle to the multi handle but doesn't take ownership of it.
        // Makes sure that the easy handle is removed from the multi handle on cleanup.
        void add_easy_handle(CurlEasyHandle& easy_handle);

        CURLM* get();

    private:
        CURLM* m_ptr = nullptr;
        std::vector<CURL*> m_easy_handles;
    };

    struct CurlHeaders
    {
        CurlHeaders() = default;
        CurlHeaders(View<std::string> headers);
        CurlHeaders(CurlHeaders&& other) noexcept;
        CurlHeaders& operator=(CurlHeaders&& other) noexcept;
        ~CurlHeaders();

        curl_slist* get() const;

    private:
        curl_slist* m_headers = nullptr;
    };

    constexpr char vcpkg_curl_user_agent[] =
        "vcpkg/" VCPKG_BASE_VERSION_AS_STRING "-" VCPKG_VERSION_AS_STRING " (curl)";
}
