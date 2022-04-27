#pragma once

#include <vcpkg/base/pragmas.h>

VCPKG_MSVC_WARNING(push)
// note:
// include\fmt\format.h(1812): warning C4189: 'zero': local variable is initialized but not referenced
//
// C6239 is not a useful warning for external code; it is
//   (<non-zero constant> && <expression>) always evaluates to the result of <expression>.
VCPKG_MSVC_WARNING(disable : 4189)
VCPKG_MSVC_WARNING(disable : 6239)
#include <fmt/format.h>
VCPKG_MSVC_WARNING(pop)
