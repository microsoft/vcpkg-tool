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

    CURLcode curl_global_init_status() noexcept
    {
        static CurlGlobalInit g_curl_global_init;
        return g_curl_global_init.get_init_status();
    }
}
