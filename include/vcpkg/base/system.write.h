#include <stddef.h>
#include <stdio.h>

namespace vcpkg
{
#if defined(_WIN32)
    enum class StdHandle: unsigned long {
        In = static_cast<unsigned long>(-10),
        Out = static_cast<unsigned long>(-11),
        Err = static_cast<unsigned long>(-12),
    };
#else
    enum class StdHandle: int {
        In = 0,
        Out = 1,
        Err = 2,
    };
#endif

    void write_text_to_std_handle(StringView sv, StdHandle handle);
}
