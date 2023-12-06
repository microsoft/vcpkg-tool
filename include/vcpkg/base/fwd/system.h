#pragma once

namespace vcpkg
{
    enum class CPUArchitecture
    {
        X86,
        X64,
        ARM,
        ARM64,
        ARM64EC,
        S390X,
        PPC64LE,
        RISCV32,
        RISCV64,
        LOONGARCH32,
        LOONGARCH64,
        MIPS64,
    };
}
