#include <vcpkg/base/curl.h>

namespace vcpkg
{
    struct CurlGlobalInit
    {
        CurlGlobalInit() : init_status(curl_global_init(CURL_GLOBAL_DEFAULT)) { }
        ~CurlGlobalInit() { curl_global_cleanup(); }

        CurlGlobalInit(const CurlGlobalInit&) = delete;
        CurlGlobalInit(CurlGlobalInit&&) = delete;
        CurlGlobalInit& operator=(const CurlGlobalInit&) = delete;
        CurlGlobalInit& operator=(CurlGlobalInit&&) = delete;

        CURLcode get_init_status() const { return init_status; }

    private:
        CURLcode init_status;
    };

    static CURLcode curl_init_status()
    {
        static CurlGlobalInit g_curl_global_init;
        return g_curl_global_init.get_init_status();
    }

    struct CurlHandle
    {
        CurlHandle() : handle(curl_multi_init()) { }
        ~CurlHandle() { curl_multi_cleanup(handle); }

        CurlHandle(const CurlHandle&) = delete;
        CurlHandle(CurlHandle&&) = delete;
        CurlHandle& operator=(const CurlHandle&) = delete;
        CurlHandle& operator=(CurlHandle&&) = delete;

        CURLM* get() const { return handle; }

    private:
        CURLM* handle;
    };

    CURLM* get_global_curl_handle() noexcept
    {
        if (curl_init_status() == CURLE_OK)
        {
            static CurlHandle g_curl_handle;
            return g_curl_handle.get();
        }
        return nullptr;
    }
}
