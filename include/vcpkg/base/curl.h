
#ifdef _MSC_VER
#pragma warning(push)           // Save current warning state
#pragma warning(disable : 6101) // Disable specific warning (e.g., warning 4996)
#endif

#include <curl/curl.h>
#include <curl/multi.h>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace vcpkg
{
    CURLM* get_global_curl_handle() noexcept;
}
