#include <vcpkg/base/checks.h>
#include <vcpkg/base/cofffilereader.h>
#include <vcpkg/base/optional.h>
#include <vcpkg/base/stringview.h>
#include <vcpkg/base/system.debug.h>

#include <stdio.h>

namespace
{
    using namespace vcpkg;
    static_assert(sizeof(CoffFileHeader) == 20, "The CoffFileHeader struct must match its on-disk representation");
    static_assert(sizeof(CommonPEOptionalHeaders) == 24,
                  "The CommonPEOptionalHeaders struct must match its on-disk representation");
    static_assert(sizeof(UniquePEOptionalHeaders) == 72,
                  "The UniquePEOptionalHeaders struct must match its on-disk representation.");
    static_assert(sizeof(UniquePEPlusOptionalHeaders) == 88,
                  "The UniquePEPlusOptionalHeaders struct must match its on-disk representation.");
    static_assert(sizeof(ImageDataDirectory) == 8,
                  "The ImageDataDirectory struct must match its on-disk representation.");
    static_assert(sizeof(SectionTableHeader) == 40,
                  "The SectionTableHeader struct must match its on-disk representation.");

    ExpectedL<Unit> read_pe_signature_and_get_coff_header_offset(ReadFilePointer& f)
    {
        static constexpr long OFFSET_TO_PE_SIGNATURE_OFFSET = 0x3c;
        static constexpr StringLiteral PE_SIGNATURE = "PE\0\0";
        static constexpr auto PE_SIGNATURE_SIZE = static_cast<uint32_t>(PE_SIGNATURE.size());
        {
            auto seek_to_signature_offset = f.try_seek_to(OFFSET_TO_PE_SIGNATURE_OFFSET);
            if (!seek_to_signature_offset.has_value())
            {
                return std::move(seek_to_signature_offset).error();
            }
        }

        uint32_t offset;
        {
            auto read_offset = f.try_read_all(&offset, sizeof(offset));
            if (!read_offset.has_value())
            {
                return std::move(read_offset).error();
            }
        }

        {
            auto seek_to_signature = f.try_seek_to(offset);
            if (!seek_to_signature.has_value())
            {
                return std::move(seek_to_signature).error();
            }
        }

        char signature[PE_SIGNATURE_SIZE];
        {
            auto read_signature = f.try_read_all(signature, PE_SIGNATURE_SIZE);
            if (!read_signature.has_value())
            {
                return std::move(read_signature).error();
            }
        }

        if (PE_SIGNATURE != StringView{signature, PE_SIGNATURE_SIZE})
        {
            return msg::format(msgPESignatureMismatch, msg::path = f.path());
        }

        return Unit{};
    }

    ExpectedL<Unit> try_read_optional_header(DllMetadata& metadata, ReadFilePointer& f)
    {
        // pre: metadata.coff_header has been loaded
        const auto size_of_optional_header = metadata.coff_header.size_of_optional_header;
        if (size_of_optional_header < (sizeof(CommonPEOptionalHeaders) + sizeof(UniquePEOptionalHeaders)))
        {
            return msg::format(msgPECoffHeaderTooShort, msg::path = f.path());
        }

        std::vector<unsigned char> optional_header(size_of_optional_header);
        {
            auto optional_header_read = f.try_read_all(optional_header.data(), size_of_optional_header);
            if (!optional_header_read.has_value())
            {
                return std::move(optional_header_read).error();
            }
        }

        ::memcpy(&metadata.common_optional_headers, optional_header.data(), sizeof(metadata.common_optional_headers));
        size_t offset_to_data_directories;
        if (metadata.common_optional_headers.magic == 0x10b)
        {
            metadata.pe_type = PEType::PE32;
            memcpy(&metadata.pe_headers,
                   optional_header.data() + sizeof(CommonPEOptionalHeaders),
                   sizeof(metadata.pe_headers));
            offset_to_data_directories = 96;
        }
        else if (metadata.common_optional_headers.magic == 0x20b)
        {
            metadata.pe_type = PEType::PE32Plus;
            memcpy(&metadata.pe_plus_headers,
                   optional_header.data() + sizeof(CommonPEOptionalHeaders),
                   sizeof(metadata.pe_plus_headers));
            offset_to_data_directories = 112;
        }
        else
        {
            return msg::format(msgPEPlusTagInvalid, msg::path = f.path());
        }

        size_t number_of_data_directories =
            (optional_header.size() - offset_to_data_directories) / sizeof(ImageDataDirectory);
        metadata.data_directories.resize(number_of_data_directories);
        memcpy(metadata.data_directories.data(),
               optional_header.data() + offset_to_data_directories,
               metadata.data_directories.size() * sizeof(ImageDataDirectory));

        return Unit{};
    }

    ExpectedL<Unit> try_read_section_headers(DllMetadata& metadata, ReadFilePointer& f)
    {
        // pre: f is positioned directly after the optional header
        const auto number_of_sections = metadata.coff_header.number_of_sections;
        metadata.section_headers.resize(number_of_sections);
        return f.try_read_all(metadata.section_headers.data(), number_of_sections * sizeof(SectionTableHeader));
    }

    // seeks the file `f` to the location in the file denoted by `rva`;
    // returns the remaining size of data in the section
    ExpectedL<size_t> try_seek_to_rva(const DllMetadata& metadata, ReadFilePointer& f, uint32_t rva)
    {
        // The PE spec says that the sections have to be sorted by virtual_address and
        // contiguous, but this does not assume that for paranoia reasons.
        for (const auto& section : metadata.section_headers)
        {
            const auto section_size = std::max(section.size_of_raw_data, section.virtual_size);
            if (rva < section.virtual_address || rva >= section.virtual_address + section_size)
            {
                continue;
            }

            const auto start_offset_within_section = rva - section.virtual_address;
            const auto file_pointer = start_offset_within_section + section.pointer_to_raw_data;
            const size_t leftover = section.size_of_raw_data - start_offset_within_section;
            return f.try_seek_to(file_pointer).map([=](Unit) { return leftover; });
        }

        return msg::format(msgPERvaNotFound, msg::path = f.path(), msg::value = rva);
    }

    ExpectedL<Unit> try_read_image_config_directory(DllMetadata& metadata, ReadFilePointer& f)
    {
        // pre: try_read_section_headers succeeded on `metadata`
        if (metadata.data_directories.size() < 11)
        {
            // metadata.load_config_type = LoadConfigType::UnsetOrOld;
            return Unit{};
        }

        const auto& load_config_data_directory = metadata.data_directories[10];
        auto maybe_remaining_size = try_seek_to_rva(metadata, f, load_config_data_directory.virtual_address);
        if (const auto remaining_size = maybe_remaining_size.get())
        {
            if (*remaining_size < load_config_data_directory.size)
            {
                return msg::format(msgPEConfigCrossesSectionBoundary, msg::path = f.path());
            }

            switch (metadata.pe_type)
            {
                case PEType::PE32:
                    if (*remaining_size >= sizeof(metadata.image_config_directory32))
                    {
                        auto config_read = f.try_read_all(&metadata.image_config_directory32,
                                                          sizeof(metadata.image_config_directory32));
                        if (config_read.has_value())
                        {
                            metadata.load_config_type = LoadConfigType::PE32;
                        }

                        return config_read;
                    }
                    break;
                case PEType::PE32Plus:
                    if (*remaining_size >= sizeof(metadata.image_config_directory64))
                    {
                        auto config_read = f.try_read_all(&metadata.image_config_directory64,
                                                          sizeof(metadata.image_config_directory64));
                        if (config_read.has_value())
                        {
                            metadata.load_config_type = LoadConfigType::PE32Plus;
                        }

                        return config_read;
                    }
                    break;
                case PEType::Unset:
                default: Checks::unreachable(VCPKG_LINE_INFO);
            }
        }

        return std::move(maybe_remaining_size).error();
    }

    ExpectedL<std::string> try_read_ntbs_from_rva(const DllMetadata& metadata, ReadFilePointer& f, uint32_t rva)
    {
        // Note that maximum_size handles the case that size_of_raw_data < virtual_size, where the loader
        // inserts the null(s).
        return try_seek_to_rva(metadata, f, rva).then([&](size_t maximum_size) -> ExpectedL<std::string> {
            std::string result;
            for (;;)
            {
                if (result.size() == maximum_size)
                {
                    break;
                }

                auto maybe_ch = f.try_getc();
                if (auto ch = maybe_ch.get())
                {
                    if (*ch == '\0')
                    {
                        break;
                    }

                    result.push_back(*ch);
                    continue;
                }

                return std::move(maybe_ch).error();
            }

            return std::move(result);
        });
    }

    static_assert(sizeof(ArchiveMemberHeader) == 60,
                  "The ArchiveMemberHeader struct must match its on-disk representation");

    MachineType read_import_machine_type_after_sig1(const ReadFilePointer& f)
    {
        struct ImportHeaderPrefixAfterSig1
        {
            uint16_t sig2;
            uint16_t version;
            uint16_t machine;
        } tmp;

        Checks::check_exit(VCPKG_LINE_INFO, f.read(&tmp, sizeof(tmp), 1) == 1);
        if (tmp.sig2 == 0xFFFF)
        {
            return to_machine_type(tmp.machine);
        }

        // This can happen, for example, if this is a .drectve member
        return MachineType::UNKNOWN;
    }

    void read_and_verify_archive_file_signature(const ReadFilePointer& f)
    {
        static constexpr StringLiteral FILE_START = "!<arch>\n";
        Checks::check_exit(VCPKG_LINE_INFO, f.seek(0, SEEK_SET) == 0);

        char file_start[FILE_START.size()];
        Checks::check_exit(VCPKG_LINE_INFO, f.read(&file_start, FILE_START.size(), 1) == 1);

        if (FILE_START != StringView{file_start, FILE_START.size()})
        {
            msg::println_error(msgIncorrectArchiveFileSignature);
            Checks::exit_fail(VCPKG_LINE_INFO);
        }
    }

    void read_and_skip_first_linker_member(const vcpkg::ReadFilePointer& f)
    {
        ArchiveMemberHeader first_linker_member_header;
        Checks::check_exit(VCPKG_LINE_INFO,
                           f.read(&first_linker_member_header, sizeof(first_linker_member_header), 1) == 1);
        Checks::check_exit(VCPKG_LINE_INFO,
                           memcmp(first_linker_member_header.name, "/ ", 2) == 0,
                           "Could not find proper first linker member");
        Checks::check_exit(VCPKG_LINE_INFO, f.seek(first_linker_member_header.decoded_size(), SEEK_CUR) == 0);
    }

    std::vector<uint32_t> read_second_linker_member_offsets(const vcpkg::ReadFilePointer& f)
    {
        ArchiveMemberHeader second_linker_member_header;
        Checks::check_exit(VCPKG_LINE_INFO,
                           f.read(&second_linker_member_header, sizeof(second_linker_member_header), 1) == 1);
        Checks::check_exit(VCPKG_LINE_INFO,
                           memcmp(second_linker_member_header.name, "/ ", 2) == 0,
                           "Could not find proper second linker member");

        const auto second_size = second_linker_member_header.decoded_size();
        // The first 4 bytes contains the number of archive members
        uint32_t archive_member_count;
        Checks::check_exit(VCPKG_LINE_INFO,
                           second_size >= sizeof(archive_member_count),
                           "Second linker member was too small to contain a single uint32_t");
        Checks::check_exit(VCPKG_LINE_INFO, f.read(&archive_member_count, sizeof(archive_member_count), 1) == 1);
        const auto maximum_possible_archive_members = (second_size / sizeof(uint32_t)) - 1;
        Checks::check_exit(VCPKG_LINE_INFO,
                           archive_member_count <= maximum_possible_archive_members,
                           "Second linker member was too small to contain the expected number of archive members");
        std::vector<uint32_t> offsets(archive_member_count);
        Checks::check_exit(VCPKG_LINE_INFO,
                           f.read(offsets.data(), sizeof(uint32_t), archive_member_count) == archive_member_count);

        // Ignore offsets that point to offset 0. See vcpkg github #223 #288 #292
        offsets.erase(std::remove(offsets.begin(), offsets.end(), 0u), offsets.end());
        // Sort the offsets, because it is possible for them to be unsorted. See vcpkg github #292
        std::sort(offsets.begin(), offsets.end());
        uint64_t leftover = second_size - sizeof(uint32_t) - (archive_member_count * sizeof(uint32_t));
        Checks::check_exit(VCPKG_LINE_INFO, f.seek(leftover, SEEK_CUR) == 0);
        return offsets;
    }

    std::vector<MachineType> read_machine_types_from_archive_members(const vcpkg::ReadFilePointer& f,
                                                                     const std::vector<uint32_t>& member_offsets)
    {
        std::vector<MachineType> machine_types; // used as a set because n is tiny
        // Next we have the obj and pseudo-object files
        for (unsigned int offset : member_offsets)
        {
            // Skip the header, no need to read it
            Checks::check_exit(VCPKG_LINE_INFO, f.seek(offset + sizeof(ArchiveMemberHeader), SEEK_SET) == 0);
            uint16_t machine_type_raw;
            Checks::check_exit(VCPKG_LINE_INFO, f.read(&machine_type_raw, sizeof(machine_type_raw), 1) == 1);

            auto result_machine_type = to_machine_type(machine_type_raw);
            if (result_machine_type == MachineType::UNKNOWN)
            {
                result_machine_type = read_import_machine_type_after_sig1(f);
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
} // unnamed namespace

namespace vcpkg
{
    bool DllMetadata::is_arm64_ec() const noexcept
    {
        switch (load_config_type)
        {
            case LoadConfigType::UnsetOrOld: return false;
            case LoadConfigType::PE32: return image_config_directory32.CHPEMetadataPointer != 0;
            case LoadConfigType::PE32Plus: return image_config_directory64.CHPEMetadataPointer != 0;
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }
    }

    MachineType DllMetadata::get_machine_type() const noexcept { return to_machine_type(coff_header.machine); }

    void ArchiveMemberHeader::check_end_of_header() const
    {
        // The name[0] == '\0' check is for freeglut, see GitHub issue #223
        Checks::msg_check_exit(VCPKG_LINE_INFO,
                               name[0] == '\0' || (end_of_header[0] == '`' && end_of_header[1] == '\n'),
                               msgIncorrectLibHeaderEnd);
    }

    uint64_t ArchiveMemberHeader::decoded_size() const
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

    ExpectedL<DllMetadata> try_read_dll_metadata(ReadFilePointer& f)
    {
        {
            auto signature = read_pe_signature_and_get_coff_header_offset(f);
            if (!signature.has_value())
            {
                return std::move(signature).error();
            }
        }

        DllMetadata result{};

        {
            auto read_coff = f.try_read_all(&result.coff_header, sizeof(result.coff_header));
            if (!read_coff.has_value())
            {
                return std::move(read_coff).error();
            }
        }

        {
            auto read_optional_header = try_read_optional_header(result, f);
            if (!read_optional_header.has_value())
            {
                return std::move(read_optional_header).error();
            }
        }

        {
            auto read_section_headers = try_read_section_headers(result, f);
            if (!read_section_headers)
            {
                return std::move(read_section_headers).error();
            }
        }

        {
            auto read_load_config_directory = try_read_image_config_directory(result, f);
            if (!read_load_config_directory)
            {
                return std::move(read_load_config_directory).error();
            }
        }

        return result;
    }

    ExpectedL<std::vector<std::string>> try_read_dll_imported_dll_names(const DllMetadata& dll, ReadFilePointer& f)
    {
        if (dll.data_directories.size() < 2)
        {
            Debug::print("No import directory\n");
            return std::vector<std::string>{};
        }

        const auto& import_data_directory = dll.data_directories[1];
        if (import_data_directory.virtual_address == 0)
        {
            Debug::print("Null import directory\n");
            return std::vector<std::string>{};
        }

        return try_seek_to_rva(dll, f, import_data_directory.virtual_address)
            .then([&](size_t remaining_size) -> ExpectedL<std::vector<std::string>> {
                if (remaining_size < import_data_directory.size)
                {
                    return msg::format(msgPEImportCrossesSectionBoundary, msg::path = f.path());
                }

                std::vector<std::string> results;
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
                    auto entry_load = f.try_read_all(&entry, sizeof(entry));
                    if (!entry_load.has_value())
                    {
                        return std::move(entry_load).error();
                    }

                    if (::memcmp(&entry, &all_zeroes, sizeof(ImportDirectoryTableEntry)) == 0)
                    {
                        // found the special "null terminator"
                        break;
                    }

                    name_rvas.push_back(entry.name_rva);
                }

                for (const auto name_rva : name_rvas)
                {
                    auto maybe_string = try_read_ntbs_from_rva(dll, f, name_rva);
                    if (const auto s = maybe_string.get())
                    {
                        results.push_back(std::move(*s));
                    }
                    else
                    {
                        return std::move(maybe_string).error();
                    }
                }

                return results;
            });
    }

    std::vector<MachineType> read_lib_machine_types(const ReadFilePointer& f)
    {
        read_and_verify_archive_file_signature(f);
        read_and_skip_first_linker_member(f);
        const auto offsets = read_second_linker_member_offsets(f);
        return read_machine_types_from_archive_members(f, offsets);
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
