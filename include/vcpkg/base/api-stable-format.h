#pragma once

#include <vcpkg/base/diagnostics.h>
#include <vcpkg/base/optional.h>
#include <vcpkg/base/stringview.h>

#include <string>

namespace vcpkg
{
    namespace details
    {
        template<class F>
        bool api_stable_format_cb(void* f, std::string& s, StringView sv)
        {
            return (*(F*)(f))(s, sv);
        }

        Optional<std::string> api_stable_format_impl(DiagnosticContext& context,
                                                     StringView fmtstr,
                                                     bool (*cb)(void*, std::string&, StringView),
                                                     void* data);
    }

    // This function exists in order to provide an API-stable formatting function similar to `std::format()` that does
    // not depend on the feature set of fmt or the C++ standard library and thus can be contractual for user interfaces.
    template<class F>
    Optional<std::string> api_stable_format(DiagnosticContext& context, StringView fmtstr, F&& handler)
    {
        return details::api_stable_format_impl(context, fmtstr, &details::api_stable_format_cb<F>, &handler);
    }
}
