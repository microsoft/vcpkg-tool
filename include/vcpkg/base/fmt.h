#pragma once

#include <vcpkg/base/fwd/fmt.h>

#include <vcpkg/base/pragmas.h>

VCPKG_MSVC_WARNING(push)
// note:
// C6239 is not a useful warning for external code; it is
//   (<non-zero constant> && <expression>) always evaluates to the result of <expression>.
//
// fmt\base.h(451): warning C6239: (<non-zero constant> && <expression>) always evaluates to the result of <expression>
// Did you intend to use the bitwise-and (`&`) operator? If not, consider removing the redundant '<non-zero constant>'
// and the `&&` operator.
// include\fmt\compile.h(153): warning C4702: unreachable code (expected due to if constexpr)
VCPKG_MSVC_WARNING(disable : 6239 4702)
#include <fmt/compile.h>
#include <fmt/format.h>
#include <fmt/ranges.h>
VCPKG_MSVC_WARNING(pop)

template<class T>
std::string adapt_to_string(const T& val)
{
    std::string result;
    val.to_string(result);
    return result;
}
