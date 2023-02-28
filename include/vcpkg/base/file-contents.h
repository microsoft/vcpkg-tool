#pragma once
#include <vcpkg/base/fwd/file-contents.h>

#include <string>

namespace vcpkg
{
    struct FileContents
    {
        std::string content;
        std::string origin;
    };
}
