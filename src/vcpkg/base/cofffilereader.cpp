#include <vcpkg/base/checks.h>
#include <vcpkg/base/cofffilereader.h>
#include <vcpkg/base/optional.h>
#include <vcpkg/base/stringliteral.h>
#include <vcpkg/base/system.debug.h>

#include <stdio.h>

using namespace std;

namespace vcpkg::CoffFileReader
{
    static uint16_t read_uint16_le(const char* data) noexcept
    {
        return static_cast<unsigned char>(data[0]) | static_cast<unsigned char>(data[1]) << 8;
    }

    template<class T>
    static Optional<T> peek_value_from_stream(const ReadFilePointer& fs)
    {
        T data;
        if (fs.read(&data, sizeof(T), 1) == 1)
        {
            if (fs.seek(-static_cast<long>(sizeof(T)), SEEK_CUR) == 0)
            {
                return data;
            }
        }

        return nullopt;
    }

    static void verify_equal_strings(const LineInfo& line_info,
                                     StringView expected,
                                     StringView actual,
                                     const char* label)
    {
        Checks::check_exit(line_info,
                           expected == actual,
                           "Incorrect string (%s) found. Expected: (%s) but found (%s)",
                           label,
                           expected,
                           actual); // FIXME HEX
    }

    static void read_and_verify_pe_signature(const ReadFilePointer& fs)
    {
        static constexpr long OFFSET_TO_PE_SIGNATURE_OFFSET = 0x3c;

        static constexpr StringLiteral PE_SIGNATURE = "PE\0\0";
        static constexpr size_t PE_SIGNATURE_SIZE = 4;

        Checks::check_exit(VCPKG_LINE_INFO, fs.seek(OFFSET_TO_PE_SIGNATURE_OFFSET, SEEK_SET) == 0);
        int32_t offset_to_pe_signature;
        Checks::check_exit(VCPKG_LINE_INFO, fs.read(&offset_to_pe_signature, sizeof(int32_t), 1) == 1);
        Checks::check_exit(VCPKG_LINE_INFO, fs.seek(offset_to_pe_signature, SEEK_SET) == 0);

        char signature[PE_SIGNATURE_SIZE];
        Checks::check_exit(VCPKG_LINE_INFO, fs.read(signature, sizeof(char), PE_SIGNATURE_SIZE) == PE_SIGNATURE_SIZE);
        verify_equal_strings(VCPKG_LINE_INFO, PE_SIGNATURE, {signature, PE_SIGNATURE_SIZE}, "PE_SIGNATURE");
    }

    static uint64_t align_to_size(const uint64_t unaligned, const uint64_t alignment_size)
    {
        uint64_t aligned = unaligned - 1;
        aligned /= alignment_size;
        aligned += 1;
        aligned *= alignment_size;
        return aligned;
    }

    struct CoffFileHeader
    {
        static constexpr size_t HEADER_SIZE = 20;

        static CoffFileHeader read(const ReadFilePointer& fs)
        {
            CoffFileHeader ret;
            ret.data.resize(HEADER_SIZE);
            Checks::check_exit(VCPKG_LINE_INFO, fs.read(&ret.data[0], HEADER_SIZE, 1) == 1);
            return ret;
        }

        MachineType machine_type() const
        {
            const auto machine = read_uint16_le(data.c_str());
            return to_machine_type(machine);
        }

    private:
        std::string data;
    };

    struct ArchiveMemberHeader
    {
        static constexpr size_t HEADER_SIZE = 60;

        static ArchiveMemberHeader read(const ReadFilePointer& fs)
        {
            static constexpr size_t HEADER_END_OFFSET = 58;
            static constexpr StringLiteral HEADER_END = "`\n";
            static constexpr size_t HEADER_END_SIZE = 2;

            ArchiveMemberHeader ret;
            ret.data.resize(HEADER_SIZE);
            Checks::check_exit(VCPKG_LINE_INFO, fs.read(&ret.data[0], HEADER_SIZE, 1) == 1);
            if (ret.data[0] != '\0') // Due to freeglut. github issue #223
            {
                const std::string header_end = ret.data.substr(HEADER_END_OFFSET, HEADER_END_SIZE);
                verify_equal_strings(VCPKG_LINE_INFO, HEADER_END, header_end, "LIB HEADER_END");
            }

            return ret;
        }

        std::string name() const
        {
            static constexpr size_t HEADER_NAME_OFFSET = 0;
            static constexpr size_t HEADER_NAME_SIZE = 16;
            return data.substr(HEADER_NAME_OFFSET, HEADER_NAME_SIZE);
        }

        uint64_t member_size() const
        {
            static constexpr size_t ALIGNMENT_SIZE = 2;

            static constexpr size_t HEADER_SIZE_OFFSET = 48;
            static constexpr size_t HEADER_SIZE_FIELD_SIZE = 10;
            const std::string as_string = data.substr(HEADER_SIZE_OFFSET, HEADER_SIZE_FIELD_SIZE);
            // This is in ASCII decimal representation
            const uint64_t value = std::strtoull(as_string.c_str(), nullptr, 10);
            return align_to_size(value, ALIGNMENT_SIZE);
        }

        std::string data;
    };

    struct ImportHeader
    {
        static constexpr size_t HEADER_SIZE = 20;

        static ImportHeader read(const ReadFilePointer& fs)
        {
            static constexpr size_t SIG1_OFFSET = 0;
            static constexpr auto SIG1 = static_cast<uint16_t>(MachineType::UNKNOWN);
            static constexpr size_t SIG1_SIZE = 2;

            static constexpr size_t SIG2_OFFSET = 2;
            static constexpr uint16_t SIG2 = 0xFFFF;
            static constexpr size_t SIG2_SIZE = 2;

            ImportHeader ret;
            ret.data.resize(HEADER_SIZE);
            Checks::check_exit(VCPKG_LINE_INFO, fs.read(&ret.data[0], HEADER_SIZE, 1) == 1);

            const std::string sig1_as_string = ret.data.substr(SIG1_OFFSET, SIG1_SIZE);
            const auto sig1 = read_uint16_le(sig1_as_string.c_str());
            Checks::check_exit(VCPKG_LINE_INFO, sig1 == SIG1, "Sig1 was incorrect. Expected %s but got %s", SIG1, sig1);

            const std::string sig2_as_string = ret.data.substr(SIG2_OFFSET, SIG2_SIZE);
            const auto sig2 = read_uint16_le(sig2_as_string.c_str());
            Checks::check_exit(VCPKG_LINE_INFO, sig2 == SIG2, "Sig2 was incorrect. Expected %s but got %s", SIG2, sig2);

            return ret;
        }

        MachineType machine_type() const
        {
            static constexpr size_t MACHINE_TYPE_OFFSET = 6;
            static constexpr size_t MACHINE_TYPE_SIZE = 2;

            std::string machine_field_as_string = data.substr(MACHINE_TYPE_OFFSET, MACHINE_TYPE_SIZE);
            const auto machine = read_uint16_le(machine_field_as_string.c_str());
            return to_machine_type(machine);
        }

    private:
        std::string data;
    };

    static void read_and_verify_archive_file_signature(const ReadFilePointer& fs)
    {
        static constexpr StringLiteral FILE_START = "!<arch>\n";
        static constexpr size_t FILE_START_SIZE = 8;

        Checks::check_exit(VCPKG_LINE_INFO, fs.seek(0, SEEK_SET) == 0);

        char file_start[FILE_START_SIZE];
        Checks::check_exit(VCPKG_LINE_INFO, fs.read(&file_start, FILE_START_SIZE, 1) == 1);
        verify_equal_strings(VCPKG_LINE_INFO, FILE_START, {file_start, FILE_START_SIZE}, "LIB FILE_START");
    }

    DllInfo read_dll(const ReadFilePointer& fs)
    {
        read_and_verify_pe_signature(fs);
        CoffFileHeader header = CoffFileHeader::read(fs);
        const MachineType machine = header.machine_type();
        return {machine};
    }

    LibInfo read_lib(const ReadFilePointer& fs)
    {
        read_and_verify_archive_file_signature(fs);

        // First Linker Member
        const ArchiveMemberHeader first_linker_member_header = ArchiveMemberHeader::read(fs);
        Checks::check_exit(VCPKG_LINE_INFO,
                           first_linker_member_header.name().substr(0, 2) == "/ ",
                           "Could not find proper first linker member");
        Checks::check_exit(VCPKG_LINE_INFO, fs.seek(first_linker_member_header.member_size(), SEEK_CUR) == 0);

        const ArchiveMemberHeader second_linker_member_header = ArchiveMemberHeader::read(fs);
        Checks::check_exit(VCPKG_LINE_INFO,
                           second_linker_member_header.name().substr(0, 2) == "/ ",
                           "Could not find proper second linker member");

        const auto second_size = second_linker_member_header.member_size();
        // The first 4 bytes contains the number of archive members
        uint32_t archive_member_count;
        Checks::check_exit(VCPKG_LINE_INFO,
                           second_size >= sizeof(archive_member_count),
                           "Second linker member was too small to contain a single uint32_t");
        Checks::check_exit(VCPKG_LINE_INFO, fs.read(&archive_member_count, sizeof(archive_member_count), 1) == 1);
        const auto maximum_possible_archive_members = (second_size / sizeof(uint32_t)) - 1;
        Checks::check_exit(VCPKG_LINE_INFO,
                           archive_member_count <= maximum_possible_archive_members,
                           "Second linker member was too small to contain the expected number of archive members");
        std::vector<uint32_t> offsets(archive_member_count);
        Checks::check_exit(VCPKG_LINE_INFO,
                           fs.read(&offsets[0], sizeof(uint32_t), archive_member_count) == archive_member_count);

        // Ignore offsets that point to offset 0. See vcpkg github #223 #288 #292
        offsets.erase(std::remove(offsets.begin(), offsets.end(), 0u), offsets.end());
        // Sort the offsets, because it is possible for them to be unsorted. See vcpkg github #292
        std::sort(offsets.begin(), offsets.end());
        uint64_t leftover = second_size - sizeof(uint32_t) - (archive_member_count * sizeof(uint32_t));
        Checks::check_exit(VCPKG_LINE_INFO, fs.seek(leftover, SEEK_CUR) == 0);

        // const bool has_longname_member_header = peek_value_from_stream<uint16_t>(fs).value_or_exit(VCPKG_LINE_INFO)
        // == 0x2F2F; if (has_longname_member_header)
        //{
        //    const ArchiveMemberHeader longnames_member_header = ArchiveMemberHeader::read(fs);
        //    marker.advance_by(ArchiveMemberHeader::HEADER_SIZE + longnames_member_header.member_size());
        //    marker.seek_to_marker(fs);
        //}

        std::vector<MachineType> machine_types{offsets.size()};
        // Next we have the obj and pseudo-object files
        for (size_t idx = 0; idx < archive_member_count; ++idx)
        {
            const auto offset = offsets[idx];
            // Skip the header, no need to read it
            Checks::check_exit(VCPKG_LINE_INFO, fs.seek(offset + ArchiveMemberHeader::HEADER_SIZE, SEEK_SET) == 0);
            const auto first_two_bytes = peek_value_from_stream<uint16_t>(fs).value_or_exit(VCPKG_LINE_INFO);
            const bool is_import_header = to_machine_type(first_two_bytes) == MachineType::UNKNOWN;
            const MachineType machine =
                is_import_header ? ImportHeader::read(fs).machine_type() : CoffFileHeader::read(fs).machine_type();
            machine_types[idx] = machine;
        }

        std::sort(machine_types.begin(), machine_types.end());
        machine_types.erase(std::unique(machine_types.begin(), machine_types.end()), machine_types.end());
        return {std::move(machine_types)};
    }
}
