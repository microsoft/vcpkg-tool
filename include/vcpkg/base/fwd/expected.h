#pragma once

#include <string>
#include <system_error>

namespace vcpkg
{
    template<class T, class S>
    struct ExpectedT;

    template<class T>
    using Expected = ExpectedT<T, std::error_code>;

    template<class T>
    using ExpectedS = ExpectedT<T, std::string>;
}
