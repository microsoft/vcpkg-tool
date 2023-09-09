#pragma once

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <Shlobj.h>
#include <windows.h>

#else // ^^^^ Windows / Unix vvvv

// 2020-05-19: workaround for a c standard library bug
// ctermid is not behind an `extern "C"` barrier, so it's linked incorrectly.
// This has been reported; remove it after 2023-05-19
#if __APPLE__
extern "C"
{
#endif // __APPLE__

#include <unistd.h>

#if __APPLE__
}
#endif // __APPLE__

#include <sys/types.h>
// glibc defines major and minor in sys/types.h, and should not
#undef major
#undef minor
#endif // ^^^ Unix
