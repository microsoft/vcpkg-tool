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

namespace vcpkg
{
    CURLcode get_curl_global_init_status() noexcept;
    void curl_set_system_ssl_root_certs(CURL* curl);

    struct CurlEasyHandle
    {
        CurlEasyHandle();
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
