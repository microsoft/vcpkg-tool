#include <vcpkg/base/curl.h>

namespace vcpkg
{
    struct CurlGlobalInit
    {
        CurlGlobalInit() { curl_global_init(CURL_GLOBAL_DEFAULT); }
        ~CurlGlobalInit() { curl_global_cleanup(); }
    };

    struct CurlHandle
    {
        CurlHandle() : handle(curl_multi_init()) { }
        CurlHandle(const CurlHandle&) = delete;
        CurlHandle(CurlHandle&&) = delete;
        CurlHandle& operator=(const CurlHandle&) = delete;
        ~CurlHandle() { curl_multi_cleanup(handle); }

        CURLM* get() const { return handle; }

    private:
        static CurlGlobalInit global_init;
        CURLM* handle;
    };
    CurlGlobalInit CurlHandle::global_init;

    CURLM* get_global_curl_handle() noexcept
    {
        static CurlHandle g_curl_handle;
        return g_curl_handle.get();
    }
}
