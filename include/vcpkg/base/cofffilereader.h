#pragma once

#include <vcpkg/base/fwd/cofffilereader.h>
#include <vcpkg/base/fwd/expected.h>
#include <vcpkg/base/fwd/files.h>
#include <vcpkg/base/fwd/optional.h>

#include <stdint.h>

#include <functional>
#include <set>
#include <string>
#include <vector>

namespace vcpkg
{
    // See https://docs.microsoft.com/en-us/windows/win32/debug/pe-format

    struct CoffFileHeader
    {
        uint16_t machine;
        uint16_t number_of_sections;
        uint32_t date_time_stamp;
        uint32_t pointer_to_symbol_table;
        uint32_t number_of_symbols;
        uint16_t size_of_optional_header;
        uint16_t characteristics;
    };

    struct CoffFileHeaderSignature
    {
        uint16_t machine;
        uint16_t number_of_sections;
    };

    struct CoffFileHeaderAfterSignature
    {
        uint32_t date_time_stamp;
        uint32_t pointer_to_symbol_table;
        uint32_t number_of_symbols;
        uint16_t size_of_optional_header;
        uint16_t characteristics;
    };

    struct CommonPEOptionalHeaders
    {
        uint16_t magic;
        unsigned char major_linker_version;
        unsigned char minor_linker_version;
        uint32_t size_of_code;
        uint32_t size_of_initialized_data;
        uint32_t size_of_uninitialized_data;
        uint32_t address_of_entry_point;
        uint32_t base_of_code;
    };

    enum class DllCharacteristics : uint16_t
    {
        HighEntropyVA = 0x0020,
        DynamicBase = 0x0040,
        ForceIntegrity = 0x0080,
        NxCompat = 0x0100,
        NoIsolation = 0x0200,
        NoSeh = 0x0400,
        NoBind = 0x0800,
        AppContainer = 0x1000,
        WdmDriver = 0x2000,
        GuardCF = 0x4000,
        TSAware = 0x8000
    };

    constexpr bool operator&(DllCharacteristics lhs, DllCharacteristics rhs) noexcept
    {
        return (static_cast<uint16_t>(lhs) & static_cast<uint16_t>(rhs)) != 0;
    }

    struct UniquePEOptionalHeaders
    {
        uint32_t base_of_data;
        uint32_t imagebase;
        uint32_t section_alignment;
        uint32_t file_alignment;
        uint16_t major_operating_system_version;
        uint16_t minor_operating_system_version;
        uint16_t major_image_version;
        uint16_t minor_image_version;
        uint16_t major_subsystem_version;
        uint16_t minor_subsystem_version;
        uint32_t win32_version_value;
        uint32_t size_of_image;
        uint32_t size_of_headers;
        uint32_t checksum;
        uint16_t subsystem;
        DllCharacteristics dll_characteristics;
        uint32_t size_of_stack_reserve;
        uint32_t size_of_stack_commit;
        uint32_t size_of_heap_reserve;
        uint32_t size_of_heap_commit;
        uint32_t loader_flags;
        uint32_t number_of_rva_and_sizes;
    };

    struct UniquePEPlusOptionalHeaders
    {
        uint64_t imagebase;
        uint32_t section_alignment;
        uint32_t file_alignment;
        uint16_t major_operating_system_version;
        uint16_t minor_operating_system_version;
        uint16_t major_image_version;
        uint16_t minor_image_version;
        uint16_t major_subsystem_version;
        uint16_t minor_subsystem_version;
        uint32_t win32_version_value;
        uint32_t size_of_image;
        uint32_t size_of_headers;
        uint32_t checksum;
        uint16_t subsystem;
        DllCharacteristics dll_characteristics;
        uint64_t size_of_stack_reserve;
        uint64_t size_of_stack_commit;
        uint64_t size_of_heap_reserve;
        uint64_t size_of_heap_commit;
        uint32_t loader_flags;
        uint32_t number_of_rva_and_sizes;
    };

    struct ImageDataDirectory
    {
        uint32_t virtual_address;
        uint32_t size;
    };

    enum class SectionTableFlags : uint32_t
    {
        TypeNoPad = 0x00000008u,
        CntCode = 0x00000020u,
        CntInitializedData = 0x00000040u,
        CntUniniitalizedData = 0x00000080u,
        LinkOther = 0x00000100u,
        LinkInfo = 0x00000200u,
        LinkRemove = 0x00000400u,
        LinkComdat = 0x00001000u,
        GpRel = 0x00008000u,
        MemPurgable = 0x00020000u,
        // Mem16Bit has the same constant as MemPurgable?!
        MemLocked = 0x00040000u,
        MemPreload = 0x00080000u,
        Align1Bytes = 0x00100000u,
        Align2Bytes = 0x00200000u,
        Align4Bytes = 0x00300000u,
        Align8Bytes = 0x00400000u,
        Align16Bytes = 0x00500000u,
        Align32Bytes = 0x00600000u,
        Align64Bytes = 0x00700000u,
        Align128Bytes = 0x00800000u,
        Align256Bytes = 0x00900000u,
        Align512Bytes = 0x00A00000u,
        Align1024Bytes = 0x00B00000u,
        Align2048Bytes = 0x00C00000u,
        Align4096Bytes = 0x00D00000u,
        Align8192Bytes = 0x00E00000u,
        LinkNumberOfRelocationsOverflow = 0x01000000u,
        MemDiscardable = 0x02000000u,
        MemNotCached = 0x04000000u,
        MemNotPaged = 0x08000000u,
        MemShared = 0x10000000u,
        MemExecute = 0x20000000u,
        MemRead = 0x40000000u,
        MemWrite = 0x80000000u
    };

    constexpr bool operator&(SectionTableFlags lhs, SectionTableFlags rhs) noexcept
    {
        return (static_cast<uint32_t>(lhs) & static_cast<uint32_t>(rhs)) != 0;
    }

    struct SectionTableHeader
    {
        unsigned char name[8];
        uint32_t virtual_size;
        uint32_t virtual_address;
        uint32_t size_of_raw_data;
        uint32_t pointer_to_raw_data;
        uint32_t pointer_to_relocations;
        uint32_t pointer_to_line_numbers;
        uint16_t number_of_relocations;
        uint16_t number_of_line_numbers;
        SectionTableFlags characteristics;
    };

    struct ExportDirectoryTable
    {
        uint32_t export_flags;
        uint32_t timestamp;
        uint16_t major_version;
        uint16_t minor_version;
        uint32_t name_rva;
        uint32_t ordinal_base;
        uint32_t address_table_entries;
        uint32_t number_of_name_pointers;
        uint32_t export_address_table_rva;
        uint32_t name_pointer_rva;
        uint32_t ordinal_table_rva;
    };

    struct ImportDirectoryTableEntry
    {
        uint32_t import_lookup_table_rva;
        uint32_t date_time_stamp;
        uint32_t forwarder_chain;
        uint32_t name_rva;
        uint32_t import_address_table_rva;
    };

    struct ImageLoadConfigCodeIntegrity
    {
        uint32_t Flags;
        uint32_t Catalog;
        uint16_t CatalogOffset;
        uint16_t Reserved;
    };

    struct ImageLoadConfigDirectory32
    {
        uint32_t Size;
        uint32_t TimeDateStamp;
        uint16_t MajorVersion;
        uint16_t MinorVersion;
        uint32_t GlobalFlagsClear;
        uint32_t GlobalFlagsSet;
        uint32_t CriticalSectionDefaultTimeout;
        uint32_t DeCommitFreeBlockThreshold;
        uint32_t DeCommitTotalFreeThreshold;
        uint32_t LockPrefixTable;
        uint32_t MaximumAllocationSize;
        uint32_t VirtualMemoryThreshold;
        uint32_t ProcessHeapFlags;
        uint32_t ProcessAffinityMask;
        uint16_t CSDVersion;
        uint16_t DependentLoadFlags;
        uint32_t EditList;
        uint32_t SecurityCookie;
        uint32_t SEHandlerTable;
        uint32_t SEHandlerCount;
        uint32_t GuardCFCheckFunctionPointer;
        uint32_t GuardCFDispatchFunctionPointer;
        uint32_t GuardCFFunctionTable;
        uint32_t GuardCFFunctionCount;
        uint32_t GuardFlags;
        ImageLoadConfigCodeIntegrity CodeIntegrity;
        uint32_t GuardAddressTakenIatEntryTable;
        uint32_t GuardAddressTakenIatEntryCount;
        uint32_t GuardLongJumpTargetTable;
        uint32_t GuardLongJumpTargetCount;
        uint32_t DynamicValueRelocTable;
        uint32_t CHPEMetadataPointer;
    };

    struct ImageLoadConfigDirectory64
    {
        uint32_t Size;
        uint32_t TimeDateStamp;
        uint16_t MajorVersion;
        uint16_t MinorVersion;
        uint32_t GlobalFlagsClear;
        uint32_t GlobalFlagsSet;
        uint32_t CriticalSectionDefaultTimeout;
        uint64_t DeCommitFreeBlockThreshold;
        uint64_t DeCommitTotalFreeThreshold;
        uint64_t LockPrefixTable;
        uint64_t MaximumAllocationSize;
        uint64_t VirtualMemoryThreshold;
        uint64_t ProcessAffinityMask;
        uint32_t ProcessHeapFlags;
        uint16_t CSDVersion;
        uint16_t DependentLoadFlags;
        uint64_t EditList;
        uint64_t SecurityCookie;
        uint64_t SEHandlerTable;
        uint64_t SEHandlerCount;
        uint64_t GuardCFCheckFunctionPointer;
        uint64_t GuardCFDispatchFunctionPointer;
        uint64_t GuardCFFunctionTable;
        uint64_t GuardCFFunctionCount;
        uint32_t GuardFlags;
        ImageLoadConfigCodeIntegrity CodeIntegrity;
        uint64_t GuardAddressTakenIatEntryTable;
        uint64_t GuardAddressTakenIatEntryCount;
        uint64_t GuardLongJumpTargetTable;
        uint64_t GuardLongJumpTargetCount;
        uint64_t DynamicValueRelocTable;
        uint64_t CHPEMetadataPointer;
    };

    // It is expected that enumerators not in this list may be present.
    enum class MachineType : uint16_t
    {
        UNKNOWN = 0x0,         // The contents of this field are assumed to be applicable to any machine type
        AM33 = 0x1d3,          // Matsushita AM33
        AMD64 = 0x8664,        // x64
        ARM = 0x1c0,           // ARM little endian
        ARM64 = 0xaa64,        // ARM64 little endian
        ARM64EC = 0xa641,      // ARM64 "emulation compatible"
        ARM64X = 0xa64e,       // ARM64X
        ARMNT = 0x1c4,         // ARM Thumb-2 little endian
        EBC = 0xebc,           // EFI byte code
        I386 = 0x14c,          // Intel 386 or later processors and compatible processors
        IA64 = 0x200,          // Intel Itanium processor family
        M32R = 0x9041,         // Mitsubishi M32R little endian
        MIPS16 = 0x266,        // MIPS16
        MIPSFPU = 0x366,       // MIPS with FPU
        MIPSFPU16 = 0x466,     // MIPS16 with FPU
        POWERPC = 0x1f0,       // Power PC little endian
        POWERPCFP = 0x1f1,     // Power PC with floating point support
        R4000 = 0x166,         // MIPS little endian
        RISCV32 = 0x5032,      // RISC-V 32-bit address space
        RISCV64 = 0x5064,      // RISC-V 64-bit address space
        RISCV128 = 0x5128,     // RISC-V 128-bit address space
        SH3 = 0x1a2,           // Hitachi SH3
        SH3DSP = 0x1a3,        // Hitachi SH3 DSP
        SH4 = 0x1a6,           // Hitachi SH4
        SH5 = 0x1a8,           // Hitachi SH5
        THUMB = 0x1c2,         // Thumb
        WCEMIPSV2 = 0x169,     // MIPS little-endian WCE v2
        LLVM_BITCODE = 0x4342, // LLVM bitcode https://www.llvm.org/docs/BitCodeFormat.html#llvm-ir-magic-number
        LOONGARCH32 = 0x6232,  // LoongArch 32-bit processor family
        LOONGARCH64 = 0x6264,  // LoongArch 64-bit processor family
    };

    enum class PEType
    {
        Unset,
        PE32,
        PE32Plus
    };

    enum class LoadConfigType
    {
        UnsetOrOld,
        PE32,
        PE32Plus
    };

    struct DllMetadata
    {
        CoffFileHeader coff_header;
        PEType pe_type;
        CommonPEOptionalHeaders common_optional_headers;
        union
        {
            UniquePEOptionalHeaders pe_headers;
            UniquePEPlusOptionalHeaders pe_plus_headers;
        };

        LoadConfigType load_config_type;
        union
        {
            ImageLoadConfigDirectory32 image_config_directory32;
            ImageLoadConfigDirectory64 image_config_directory64;
        };

        std::vector<ImageDataDirectory> data_directories;
        std::vector<SectionTableHeader> section_headers;

        const ImageDataDirectory* try_get_image_data_directory(size_t directory_index) const noexcept;
        MachineType get_machine_type() const noexcept;
        bool is_arm64_ec() const noexcept;
    };

    struct ArchiveMemberHeader
    {
        char name[16];
        char date[12];
        char user_id[6];
        char group_id[6];
        char mode[8];
        char size[10];
        char end_of_header[2];

        uint64_t decoded_size() const;
    };

    struct ImportHeader
    {
        uint16_t sig1; // must be IMAGE_FILE_MACHINE_UNKNOWN
        uint16_t sig2; // must be 0xFFFF
        uint16_t version;
        uint16_t machine;
        uint32_t date_time_stamp;
        uint32_t size_of_data;
        uint16_t ordinal_hint;
        uint16_t type_and_name_type;
        // 2 bits: type
        // 3 bits: name type
        // 11 bits: reserved and must be 0
    };

    constexpr uint32_t ImportHeaderSignature = 0xFFFF0000u;
    constexpr uint32_t LlvmBitcodeSignature = 0xDEC04342u;

    struct ImportHeaderAfterSignature
    {
        uint16_t version;
        uint16_t machine;
        uint32_t date_time_stamp;
        uint32_t size_of_data;
        uint16_t ordinal_hint;
        uint16_t type_and_name_type;
        // 2 bits: type
        // 3 bits: name type
        // 11 bits: reserved and must be 0
    };

    struct LibInformation
    {
        std::vector<MachineType> machine_types; // used as set because n is tiny
        std::set<std::string, std::less<>> linker_directives;
    };

    std::vector<std::string> tokenize_command_line(StringView cmd_line);
    ExpectedL<Optional<DllMetadata>> try_read_dll_metadata(ReadFilePointer& f);
    ExpectedL<DllMetadata> try_read_dll_metadata_required(ReadFilePointer& f);
    ExpectedL<bool> try_read_if_dll_has_exports(const DllMetadata& dll, ReadFilePointer& f);
    ExpectedL<std::vector<std::string>> try_read_dll_imported_dll_names(const DllMetadata& dll, ReadFilePointer& f);
    ExpectedL<LibInformation> read_lib_information(ReadFilePointer& f);
}
