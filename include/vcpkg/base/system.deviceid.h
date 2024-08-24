#pragma once

#include <vcpkg/base/fwd/files.h>
#include <vcpkg/base/fwd/stringview.h>

#include <string>

namespace vcpkg
{
    bool validate_device_id(StringView uuid);

#if defined(_WIN32)
    std::string get_device_id();
#else
    std::string get_device_id(const vcpkg::Filesystem& fs);
#endif
}