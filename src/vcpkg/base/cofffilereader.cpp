#include <vcpkg/base/checks.h>
#include <vcpkg/base/cofffilereader.h>
#include <vcpkg/base/optional.h>
#include <vcpkg/base/stringview.h>
#include <vcpkg/base/system.debug.h>

#include <stdio.h>

// See https://docs.microsoft.com/en-us/windows/win32/debug/pe-format

namespace vcpkg
{
    static void read_and_verify_pe_signature(const ReadFilePointer& fs)
    {
        static constexpr long OFFSET_TO_PE_SIGNATURE_OFFSET = 0x3c;

        static constexpr StringLiteral PE_SIGNATURE = "PE\0\0";

        Checks::check_exit(VCPKG_LINE_INFO, fs.seek(OFFSET_TO_PE_SIGNATURE_OFFSET, SEEK_SET) == 0);
        uint32_t offset_to_pe_signature;
        Checks::check_exit(VCPKG_LINE_INFO, fs.read(&offset_to_pe_signature, sizeof(uint32_t), 1) == 1);
        Checks::check_exit(VCPKG_LINE_INFO, fs.seek(offset_to_pe_signature, SEEK_SET) == 0);

        char signature[PE_SIGNATURE.size()];
        Checks::check_exit(VCPKG_LINE_INFO,
                           fs.read(signature, sizeof(char), PE_SIGNATURE.size()) == PE_SIGNATURE.size());
        Checks::msg_check_exit(
            VCPKG_LINE_INFO, PE_SIGNATURE == StringView{signature, PE_SIGNATURE.size()}, msgIncorrectPESignature);
    }

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

    static_assert(sizeof(CoffFileHeader) == 20, "The CoffFileHeader struct must match its on-disk representation");

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

    static_assert(sizeof(CommonPEOptionalHeaders) == 24,
                  "The CommonPEOptionalHeaders struct must match its on-disk representation");

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
        uint16_t dll_characteristics;
        uint32_t size_of_stack_reserve;
        uint32_t size_of_stack_commit;
        uint32_t size_of_heap_reserve;
        uint32_t size_of_heap_commit;
        uint32_t loader_flags;
        uint32_t number_of_rva_and_sizes;
    };

    static_assert(sizeof(UniquePEOptionalHeaders) == 72,
                  "The UniquePEOptionalHeaders struct must match its on-disk representation.");

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
        uint16_t dll_characteristics;
        uint64_t size_of_stack_reserve;
        uint64_t size_of_stack_commit;
        uint64_t size_of_heap_reserve;
        uint64_t size_of_heap_commit;
        uint32_t loader_flags;
        uint32_t number_of_rva_and_sizes;
    };

    static_assert(sizeof(UniquePEPlusOptionalHeaders) == 88,
                  "The UniquePEPlusOptionalHeaders struct must match its on-disk representation.");

    struct ImageDataDirectory
    {
        uint32_t virtual_address;
        uint32_t size;
    };

    static_assert(sizeof(ImageDataDirectory) == 8,
                  "The ImageDataDirectory struct must match its on-disk representation.");

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
        uint32_t characteristics;
    };

    static_assert(sizeof(SectionTableHeader) == 40,
                  "The SectionTableHeader struct must match its on-disk representation.");

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

    struct LoadedDll
    {
        CoffFileHeader coff_header;
        CommonPEOptionalHeaders common_optional_headers;
        union
        {
            UniquePEOptionalHeaders pe_headers;
            UniquePEPlusOptionalHeaders pe_plus_headers;
        };

        std::vector<ImageDataDirectory> data_directories;
        std::vector<SectionTableHeader> section_headers;

        explicit LoadedDll(const ReadFilePointer& fs)
        {
            read_and_verify_pe_signature(fs);
            Checks::check_exit(VCPKG_LINE_INFO, fs.read(&coff_header, sizeof(coff_header), 1) == 1);
            read_optional_header(fs);
            read_section_headers(fs);
        }

        bool check_load_config_dll_arm64ec(const ReadFilePointer& fs) const
        {
            if (data_directories.size() < 11)
            {
                Debug::print("No load config data directory");
                return false;
            }

            const auto load_config_data_directory = data_directories[10];
            auto remaining_size = seek_to_rva(fs, load_config_data_directory.virtual_address);
            Checks::check_exit(VCPKG_LINE_INFO,
                               remaining_size >= load_config_data_directory.size,
                               "Load config data directory crosses a section boundary.");
            remaining_size = load_config_data_directory.size;
            if (common_optional_headers.magic == 0x10b)
            {
                if (remaining_size < sizeof(ImageLoadConfigDirectory32))
                {
                    Debug::print("Older LOAD_CONFIG");
                    return false;
                }
                ImageLoadConfigDirectory32 entry;
                Checks::check_exit(VCPKG_LINE_INFO, fs.read(&entry, sizeof(entry), 1) == 1);
                return entry.CHPEMetadataPointer != 0;
            }

            if (common_optional_headers.magic == 0x20b)
            {
                if (remaining_size < sizeof(ImageLoadConfigDirectory64))
                {
                    Debug::print("Older LOAD_CONFIG");
                    return false;
                }
                ImageLoadConfigDirectory64 entry;
                Checks::check_exit(VCPKG_LINE_INFO, fs.read(&entry, sizeof(entry), 1) == 1);
                return entry.CHPEMetadataPointer != 0;
            }

            Checks::unreachable(VCPKG_LINE_INFO);
        }

        std::vector<std::string> read_imported_dlls(const ReadFilePointer& fs) const
        {
            std::vector<std::string> results;
            if (data_directories.size() < 2)
            {
                Debug::print("No import directory");
                return results;
            }

            const auto import_data_directory = data_directories[1];
            auto remaining_size = seek_to_rva(fs, import_data_directory.virtual_address);
            Checks::check_exit(VCPKG_LINE_INFO,
                               remaining_size >= import_data_directory.size,
                               "Import data directory crosses a section boundary.");
            remaining_size = import_data_directory.size;
            if (remaining_size < sizeof(ImportDirectoryTableEntry))
            {
                Debug::print("No import directory table entries");
                return results;
            }

            // -1 for the all-zeroes ImportDirectoryTableEntry
            const auto maximum_directory_entries = (remaining_size / sizeof(ImportDirectoryTableEntry)) - 1;
            std::vector<uint32_t> name_rvas; // collect all the RVAs first because loading each name seeks
            static constexpr ImportDirectoryTableEntry all_zeroes{};
            for (size_t idx = 0; idx < maximum_directory_entries; ++idx)
            {
                ImportDirectoryTableEntry entry;
                Checks::check_exit(VCPKG_LINE_INFO, fs.read(&entry, sizeof(entry), 1) == 1);
                if (::memcmp(&entry, &all_zeroes, sizeof(ImportDirectoryTableEntry)) == 0)
                {
                    // found the special "null terminator"
                    break;
                }

                name_rvas.push_back(entry.name_rva);
            }

            for (const auto name_rva : name_rvas)
            {
                results.push_back(read_ntbs_from_rva(fs, name_rva));
            }

            return results;
        }

    private:
        void read_optional_header(const ReadFilePointer& fs)
        {
            Checks::check_exit(VCPKG_LINE_INFO,
                               coff_header.size_of_optional_header >=
                                   (sizeof(CommonPEOptionalHeaders) + sizeof(UniquePEOptionalHeaders)));
            std::vector<unsigned char> optional_header(coff_header.size_of_optional_header);
            Checks::check_exit(VCPKG_LINE_INFO,
                               fs.read(optional_header.data(), 1, optional_header.size()) == optional_header.size());
            ::memcpy(&common_optional_headers, optional_header.data(), sizeof(common_optional_headers));
            size_t offset_to_data_directories;
            if (common_optional_headers.magic == 0x10b)
            {
                memcpy(&pe_headers, optional_header.data() + sizeof(CommonPEOptionalHeaders), sizeof(pe_headers));
                offset_to_data_directories = 96;
            }
            else if (common_optional_headers.magic == 0x20b)
            {
                memcpy(&pe_plus_headers,
                       optional_header.data() + sizeof(CommonPEOptionalHeaders),
                       sizeof(pe_plus_headers));
                offset_to_data_directories = 112;
            }
            else
            {
                Checks::exit_with_message(VCPKG_LINE_INFO,
                                          "Image did not have a PE or PE+ magic number in its optional header.");
            }

            size_t number_of_data_directories =
                (optional_header.size() - offset_to_data_directories) / sizeof(ImageDataDirectory);
            data_directories.resize(number_of_data_directories);
            memcpy(data_directories.data(),
                   optional_header.data() + offset_to_data_directories,
                   data_directories.size() * sizeof(ImageDataDirectory));
        }

        void read_section_headers(const ReadFilePointer& fs)
        {
            section_headers.resize(coff_header.number_of_sections);
            Checks::check_exit(VCPKG_LINE_INFO,
                               fs.read(section_headers.data(), sizeof(SectionTableHeader), section_headers.size()) ==
                                   section_headers.size());
        }

        // seeks the file `fs` to the location in the file denoted by `rva`;
        // returns the remaining size of data in the section
        size_t seek_to_rva(const ReadFilePointer& fs, uint32_t rva) const noexcept
        {
            // The PE spec says that the sections have to be sorted by virtual_address and
            // contiguous, but this does not assume that for paranoia reasons.
            for (const auto& section : section_headers)
            {
                const auto section_size = std::max(section.size_of_raw_data, section.virtual_size);
                if (rva < section.virtual_address || rva >= section.virtual_address + section_size)
                {
                    continue;
                }

                const auto start_offset_within_section = rva - section.virtual_address;
                const auto file_pointer = start_offset_within_section + section.pointer_to_raw_data;
                Checks::check_exit(VCPKG_LINE_INFO, fs.seek(file_pointer, SEEK_SET) == 0);
                return section.size_of_raw_data - start_offset_within_section;
            }

            Checks::exit_with_message(VCPKG_LINE_INFO, "Could not find RVA %08X", rva);
        }

        std::string read_ntbs_from_rva(const ReadFilePointer& fs, uint32_t rva) const
        {
            // Note that maximum_size handles the case that size_of_raw_data < virtual_size, where the loader
            // inserts the null(s).
            const auto maximum_size = seek_to_rva(fs, rva);
            std::string result;
            for (;;)
            {
                if (result.size() == maximum_size)
                {
                    return result;
                }

                int ch = fs.getc();
                if (ch == EOF || ch == '\0')
                {
                    return result;
                }

                result.push_back(static_cast<char>(ch));
            }
        }
    };

    MachineType read_dll_machine_type(const ReadFilePointer& fs)
    {
        LoadedDll dll(fs);
        return to_machine_type(dll.coff_header.machine);
    }

    std::vector<std::string> read_dll_imported_dll_names(const ReadFilePointer& fs)
    {
        LoadedDll dll(fs);
        return dll.read_imported_dlls(fs);
    }

    bool is_arm64ec_dll(const ReadFilePointer& fs)
    {
        LoadedDll dll(fs);
        return dll.check_load_config_dll_arm64ec(fs);
    }

    struct ArchiveMemberHeader
    {
        char name[16];
        char date[12];
        char user_id[6];
        char group_id[6];
        char mode[8];
        char size[10];
        char end_of_header[2];

        void check_end_of_header() const
        {
            // The name[0] == '\0' check is for freeglut, see GitHub issue #223
            Checks::msg_check_exit(VCPKG_LINE_INFO,
                                   name[0] == '\0' || (end_of_header[0] == '`' && end_of_header[1] == '\n'),
                                   msgIncorrectLibHeaderEnd);
        }

        uint64_t decoded_size() const
        {
            char size_plus_null[11];
            memcpy(size_plus_null, size, 10);
            size_plus_null[10] = '\0';
            uint64_t value = strtoull(size_plus_null, nullptr, 10);
            if (value & 1u)
            {
                ++value; // align to short
            }

            return value;
        }
    };

    static_assert(sizeof(ArchiveMemberHeader) == 60,
                  "The ArchiveMemberHeader struct must match its on-disk representation");

    static MachineType read_import_machine_type_after_sig1(const ReadFilePointer& fs)
    {
        struct ImportHeaderPrefixAfterSig1
        {
            uint16_t sig2;
            uint16_t version;
            uint16_t machine;
        } tmp;

        Checks::check_exit(VCPKG_LINE_INFO, fs.read(&tmp, sizeof(tmp), 1) == 1);
        if (tmp.sig2 == 0xFFFF)
        {
            return to_machine_type(tmp.machine);
        }

        // This can happen, for example, if this is a .drectve member
        return MachineType::UNKNOWN;
    }

    static void read_and_verify_archive_file_signature(const ReadFilePointer& fs)
    {
        static constexpr StringLiteral FILE_START = "!<arch>\n";
        Checks::check_exit(VCPKG_LINE_INFO, fs.seek(0, SEEK_SET) == 0);

        char file_start[FILE_START.size()];
        Checks::check_exit(VCPKG_LINE_INFO, fs.read(&file_start, FILE_START.size(), 1) == 1);

        if (FILE_START != StringView{file_start, FILE_START.size()})
        {
            msg::println_error(msgIncorrectArchiveFileSignature);
            Checks::exit_fail(VCPKG_LINE_INFO);
        }
    }

    static void read_and_skip_first_linker_member(const vcpkg::ReadFilePointer& fs)
    {
        ArchiveMemberHeader first_linker_member_header;
        Checks::check_exit(VCPKG_LINE_INFO,
                           fs.read(&first_linker_member_header, sizeof(first_linker_member_header), 1) == 1);
        if (memcmp(first_linker_member_header.name, "/ ", 2) != 0)
        {
            Debug::println("Could not find proper first linker member");
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        Checks::check_exit(VCPKG_LINE_INFO, fs.seek(first_linker_member_header.decoded_size(), SEEK_CUR) == 0);
    }

    static std::vector<uint32_t> read_second_linker_member_offsets(const vcpkg::ReadFilePointer& fs)
    {
        ArchiveMemberHeader second_linker_member_header;
        Checks::check_exit(VCPKG_LINE_INFO,
                           fs.read(&second_linker_member_header, sizeof(second_linker_member_header), 1) == 1);
        if (memcmp(second_linker_member_header.name, "/ ", 2) != 0)
        {
            Debug::println("Could not find proper second linker member");
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        const auto second_size = second_linker_member_header.decoded_size();
        // The first 4 bytes contains the number of archive members
        uint32_t archive_member_count;

        if (second_size < sizeof(archive_member_count))
        {
            Debug::println("Second linker member was too small to contain a single uint32_t");
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        Checks::check_exit(VCPKG_LINE_INFO, fs.read(&archive_member_count, sizeof(archive_member_count), 1) == 1);
        const auto maximum_possible_archive_members = (second_size / sizeof(uint32_t)) - 1;
        if (archive_member_count > maximum_possible_archive_members)
        {
            Debug::println("Second linker member was too small to contain the expected number of archive members");
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        std::vector<uint32_t> offsets(archive_member_count);
        Checks::check_exit(VCPKG_LINE_INFO,
                           fs.read(offsets.data(), sizeof(uint32_t), archive_member_count) == archive_member_count);

        // Ignore offsets that point to offset 0. See vcpkg github #223 #288 #292
        offsets.erase(std::remove(offsets.begin(), offsets.end(), 0u), offsets.end());
        // Sort the offsets, because it is possible for them to be unsorted. See vcpkg github #292
        std::sort(offsets.begin(), offsets.end());
        uint64_t leftover = second_size - sizeof(uint32_t) - (archive_member_count * sizeof(uint32_t));
        Checks::check_exit(VCPKG_LINE_INFO, fs.seek(leftover, SEEK_CUR) == 0);
        return offsets;
    }

    static std::vector<MachineType> read_machine_types_from_archive_members(const vcpkg::ReadFilePointer& fs,
                                                                            const std::vector<uint32_t>& member_offsets)
    {
        std::vector<MachineType> machine_types; // used as a set because n is tiny
        // Next we have the obj and pseudo-object files
        for (unsigned int offset : member_offsets)
        {
            // Skip the header, no need to read it
            Checks::check_exit(VCPKG_LINE_INFO, fs.seek(offset + sizeof(ArchiveMemberHeader), SEEK_SET) == 0);
            uint16_t machine_type_raw;
            Checks::check_exit(VCPKG_LINE_INFO, fs.read(&machine_type_raw, sizeof(machine_type_raw), 1) == 1);

            auto result_machine_type = to_machine_type(machine_type_raw);
            if (result_machine_type == MachineType::UNKNOWN)
            {
                result_machine_type = read_import_machine_type_after_sig1(fs);
            }

            if (result_machine_type == MachineType::UNKNOWN ||
                std::find(machine_types.begin(), machine_types.end(), result_machine_type) != machine_types.end())
            {
                continue;
            }

            machine_types.push_back(result_machine_type);
        }

        std::sort(machine_types.begin(), machine_types.end());
        return machine_types;
    }

    std::vector<MachineType> read_lib_machine_types(const ReadFilePointer& fs)
    {
        read_and_verify_archive_file_signature(fs);
        read_and_skip_first_linker_member(fs);
        const auto offsets = read_second_linker_member_offsets(fs);
        return read_machine_types_from_archive_members(fs, offsets);
    }

    MachineType to_machine_type(const uint16_t value)
    {
        const MachineType t = static_cast<MachineType>(value);
        switch (t)
        {
            case MachineType::UNKNOWN:
            case MachineType::AM33:
            case MachineType::AMD64:
            case MachineType::ARM:
            case MachineType::ARM64:
            case MachineType::ARM64EC:
            case MachineType::ARM64X:
            case MachineType::ARMNT:
            case MachineType::EBC:
            case MachineType::I386:
            case MachineType::IA64:
            case MachineType::M32R:
            case MachineType::MIPS16:
            case MachineType::MIPSFPU:
            case MachineType::MIPSFPU16:
            case MachineType::POWERPC:
            case MachineType::POWERPCFP:
            case MachineType::R4000:
            case MachineType::RISCV32:
            case MachineType::RISCV64:
            case MachineType::RISCV128:
            case MachineType::SH3:
            case MachineType::SH3DSP:
            case MachineType::SH4:
            case MachineType::SH5:
            case MachineType::THUMB:
            case MachineType::WCEMIPSV2: return t;
            default: Checks::msg_exit_maybe_upgrade(VCPKG_LINE_INFO, msgUnknownMachineCode, msg::value = value);
        }
    }
}
