#pragma once

#include <vcpkg/base/fwd/files.h>
#include <vcpkg/base/fwd/stringview.h>

#include <string>

namespace vcpkg
{
    bool validate_device_id(StringView uuid);

    std::string get_device_id(const vcpkg::Filesystem& fs);
}