#pragma once
#include <string>

namespace vcpkg
{
    template<class T>
    auto to_string(const T& t) -> decltype(t.to_string())
    {
        return t.to_string();
    }

    inline const std::string& to_string(const std::string& str) noexcept { return str; }
}
