#pragma once

#include <vcpkg/base/expected.h>
#include <vcpkg/base/stringview.h>

#include <string>

namespace vcpkg
{
    namespace details
    {
        template<class F>
        void api_stable_format_cb(void* f, std::string& s, StringView sv)
        {
            (*(F*)(f))(s, sv);
        }

        ExpectedL<std::string> api_stable_format_impl(StringView fmtstr,
                                                      void (*cb)(void*, std::string&, StringView),
                                                      void* data);
    }

    // This function exists in order to provide an API-stable formatting function similar to `std::format()` that does
    // not depend on the feature set of fmt or the C++ standard library and thus can be contractual for user interfaces.
    template<class F>
    ExpectedL<std::string> api_stable_format(StringView fmtstr, F&& handler)
    {
        return details::api_stable_format_impl(fmtstr, &details::api_stable_format_cb<F>, &handler);
    }
}
