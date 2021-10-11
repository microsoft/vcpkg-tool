#include <vcpkg/base/format.h>

#include <fmt/format.h>

namespace vcpkg
{
    void throw_format_error(const char* msg)
    {
        throw fmt::format_error(msg);
    }
}
