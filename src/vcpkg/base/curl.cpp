#include <vcpkg/base/curl.h>

namespace vcpkg
{
    struct CurlGlobalInit
    {
        CurlGlobalInit() { curl_global_init(CURL_GLOBAL_DEFAULT); }
        ~CurlGlobalInit() { curl_global_cleanup(); }
    };

    static CurlGlobalInit& perform_global_init()
    {
        static CurlGlobalInit g_curl_global_init;
        return g_curl_global_init;
    }

    struct CurlHandle
    {
        CurlHandle()
        {
            perform_global_init();
            handle = get_global_curl_handle();
        }
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
