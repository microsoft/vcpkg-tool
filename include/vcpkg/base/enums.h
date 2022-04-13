#pragma once

#include <vcpkg/base/lineinfo.h>
#include <vcpkg/base/stringview.h>

namespace vcpkg::Util
{
    std::string nullvalue_to_string(const ZStringView enum_name);

    [[noreturn]] void nullvalue_used(const LineInfo& line_info, const ZStringView enum_name);
}
