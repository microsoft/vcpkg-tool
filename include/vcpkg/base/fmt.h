#pragma once

#include <vcpkg/base/fwd/fmt.h>

#include <vcpkg/base/pragmas.h>

VCPKG_MSVC_WARNING(push)
// note:
// base.h(1711): warning C6294: Ill-defined for-loop. Loop body not executed.
// Arises in template code only when supplied a nontype template parameter 0 which makes the loop body empty.
// format.h(1058): warning C6240: (<expression> && <non-zero constant>) always evaluates to the result of <expression>
// Arises from <non-zero constant> being a macro
// format.h(1686): warning C6326: Potential comparison of a constant with another constant.
// Also arises from a macro
VCPKG_MSVC_WARNING(disable : 6240 6294 6326)
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
