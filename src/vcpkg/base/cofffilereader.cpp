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

    MachineType read_dll_machine_type(const ReadFilePointer& fs)
    {
        read_and_verify_pe_signature(fs);
        CoffFileHeader header;
        Checks::check_exit(VCPKG_LINE_INFO, fs.read(&header, sizeof(header), 1) == 1);
        return to_machine_type(header.machine);
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
                           fs.read(&offsets[0], sizeof(uint32_t), archive_member_count) == archive_member_count);

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
