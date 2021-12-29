#pragma once

#if defined(_MSC_VER) && _MSC_VER < 1911
// [[nodiscard]] is not recognized before VS 2017 version 15.3
#pragma warning(disable : 5030)
#endif

#if defined(_MSC_VER) && _MSC_VER < 1910
// https://docs.microsoft.com/en-us/cpp/error-messages/compiler-warnings/compiler-warning-level-3-c4800?view=vs-2019
#pragma warning(disable : 4800)
#endif

#if defined(__GNUC__) && __GNUC__ < 7
// [[nodiscard]] is not recognized before GCC version 7
#pragma GCC diagnostic ignored "-Wattributes"
#endif

#if defined(_MSC_VER)
#include <sal.h>
#endif

#if defined(_MSC_VER)
#define ASSUME(expr) __assume(expr)
#else
#define ASSUME(expr)
#endif

#define Z_VCPKG_PRAGMA(PRAGMA) _Pragma(#PRAGMA)

#if defined(_MSC_VER)
#define VCPKG_SAL_ANNOTATION(...) __VA_ARGS__
#else
#define VCPKG_SAL_ANNOTATION(...)
#endif

// the static_assert(true, "")s are to avoid the extra ';' warning
#if defined(__clang__)
// check clang first because it may define _MSC_VER
#define VCPKG_MSVC_WARNING(...)
#define VCPKG_GCC_DIAGNOSTIC(...)
#define VCPKG_CLANG_DIAGNOSTIC(DIAGNOSTIC) Z_VCPKG_PRAGMA(clang diagnostic DIAGNOSTIC)
#define VCPKG_UNUSED [[maybe_unused]]
#elif defined(_MSC_VER)
#define VCPKG_MSVC_WARNING(...) Z_VCPKG_PRAGMA(warning(__VA_ARGS__))
#define VCPKG_GCC_DIAGNOSTIC(...)
#define VCPKG_CLANG_DIAGNOSTIC(...)
#define VCPKG_UNUSED [[maybe_unused]]
#else
// gcc
#define VCPKG_MSVC_WARNING(...)
#define VCPKG_GCC_DIAGNOSTIC(DIAGNOSTIC) Z_VCPKG_PRAGMA(gcc diagnostic DIAGNOSTIC)
#define VCPKG_CLANG_DIAGNOSTIC(DIAGNOSTIC)
#define VCPKG_UNUSED __attribute__((unused))
#define VCPKG_SAL_ANNOTATION(...)
#endif
