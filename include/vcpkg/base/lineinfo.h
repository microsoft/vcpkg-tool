#pragma once

#include <vcpkg/base/fwd/format.h>

#include <string>

namespace vcpkg
{
    struct LineInfo
    {
        int line_number;
        const char* file_name;

        std::string to_string() const;
    };
}

#define VCPKG_LINE_INFO                                                                                                \
    vcpkg::LineInfo { __LINE__, __FILE__ }

VCPKG_FORMAT_WITH_TO_STRING(vcpkg::LineInfo);
