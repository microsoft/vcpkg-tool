#pragma once

#include <vcpkg/base/fwd/fmt.h>

#include <vcpkg/base/pragmas.h>

VCPKG_MSVC_WARNING(push)
// note:
// C6239 is not a useful warning for external code; it is
//   (<non-zero constant> && <expression>) always evaluates to the result of <expression>.
//
// include\fmt\format.h(1812): warning C4189: 'zero': local variable is initialized but not referenced
VCPKG_MSVC_WARNING(disable : 6239 4189)
#include <fmt/format.h>
VCPKG_MSVC_WARNING(pop)
