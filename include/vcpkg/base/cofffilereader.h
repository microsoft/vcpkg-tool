#pragma once

#include <vcpkg/base/files.h>
#include <vcpkg/base/machinetype.h>

#include <vector>

namespace vcpkg::CoffFileReader
{
    struct DllInfo
    {
        MachineType machine_type;
    };

    struct LibInfo
    {
        std::vector<MachineType> machine_types;
    };

#if defined(_WIN32)
    DllInfo read_dll(const path& dll);

    LibInfo read_lib(const path& lib);
#endif
}
