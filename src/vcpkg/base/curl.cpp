#include <vcpkg/base/curl.h>

namespace vcpkg
{
    struct CurlHandle
    {
        CurlHandle() : handle(curl_multi_init()) { }
        CurlHandle(const CurlHandle&) = delete;
        CurlHandle& operator=(const CurlHandle&) = delete;
        ~CurlHandle() { curl_multi_cleanup(handle); }

        CURLM* get() const { return handle; }

    private:
        CURLM* handle;
    };

    CURLM* get_global_curl_handle() noexcept
    {
        static CurlHandle g_curl_handle;
        return g_curl_handle.get();
    }
}
