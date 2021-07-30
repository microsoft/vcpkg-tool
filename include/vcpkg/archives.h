#pragma once

#include <vcpkg/fwd/vcpkgpaths.h>

#include <vcpkg/base/files.h>

namespace vcpkg::Archives
{
    void extract_archive(const VcpkgPaths& paths, const Path& archive, const Path& to_path);
}
