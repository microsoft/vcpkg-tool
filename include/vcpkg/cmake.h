#pragma once

#include <vcpkg/base/stringview.h>

namespace vcpkg
{
    StringView find_cmake_invocation(StringView content, StringView command);
    StringView extract_cmake_invocation_argument(StringView command, StringView argument);
    std::string replace_cmake_var(StringView text, StringView var, StringView value);
} // namespace vcpkg
