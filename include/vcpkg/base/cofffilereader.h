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

    DllInfo read_dll(const ReadFilePointer& fs);

    LibInfo read_lib(const ReadFilePointer& fs);
}
