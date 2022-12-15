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
    ExpectedL<uint32_t> try_seek_to_rva(const DllMetadata& metadata, ReadFilePointer& f, uint32_t rva)
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
            const uint32_t leftover = section.size_of_raw_data - start_offset_within_section;
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

    ExpectedL<Unit> try_read_struct_from_rva(
        const DllMetadata& metadata, ReadFilePointer& f, void* target, uint32_t rva, uint32_t size)
    {
        return try_seek_to_rva(metadata, f, rva).then([&](uint32_t maximum_size) -> ExpectedL<Unit> {
            if (maximum_size >= size)
            {
                return f.try_read_all(target, size);
            }

            return f.try_read_all(target, maximum_size).map([&](Unit) {
                ::memset(static_cast<char*>(target) + maximum_size, 0, size - maximum_size);
                return Unit{};
            });
        });
    }

    ExpectedL<std::string> try_read_ntbs_from_rva(const DllMetadata& metadata, ReadFilePointer& f, uint32_t rva)
    {
        // Note that maximum_size handles the case that size_of_raw_data < virtual_size, where the loader
        // inserts the null(s).
        return try_seek_to_rva(metadata, f, rva).then([&](uint32_t maximum_size) -> ExpectedL<std::string> {
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
        if (memcmp(first_linker_member_header.name, "/ ", 2) != 0)
        {
            Debug::println("Could not find proper first linker member");
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        Checks::check_exit(VCPKG_LINE_INFO, f.seek(first_linker_member_header.decoded_size(), SEEK_CUR) == 0);
    }

    std::vector<uint32_t> read_second_linker_member_offsets(const vcpkg::ReadFilePointer& f)
    {
        ArchiveMemberHeader second_linker_member_header;
        Checks::check_exit(VCPKG_LINE_INFO,
                           f.read(&second_linker_member_header, sizeof(second_linker_member_header), 1) == 1);
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

        Checks::check_exit(VCPKG_LINE_INFO, f.read(&archive_member_count, sizeof(archive_member_count), 1) == 1);
        const auto maximum_possible_archive_members = (second_size / sizeof(uint32_t)) - 1;
        if (archive_member_count > maximum_possible_archive_members)
        {
            Debug::println("Second linker member was too small to contain the expected number of archive members");
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

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

    LibInformation read_lib_information_from_archive_members(const vcpkg::ReadFilePointer& f,
                                                             const std::vector<uint32_t>& member_offsets)
    {
        std::vector<MachineType> machine_types; // used as sets because n is tiny
        std::vector<std::string> directives;
        // Next we have the obj and pseudo-object files
        for (unsigned int offset : member_offsets)
        {
            // Skip the header, no need to read it
            const auto coff_base = offset + sizeof(ArchiveMemberHeader);
            Checks::check_exit(VCPKG_LINE_INFO, f.seek(coff_base, SEEK_SET) == 0);
            static_assert(sizeof(CoffFileHeader) == sizeof(ImportHeader), "Boom");
            char loaded_header[sizeof(CoffFileHeader)];
            Checks::check_exit(VCPKG_LINE_INFO, f.read(&loaded_header, sizeof(loaded_header), 1) == 1);

            CoffFileHeader coff_header;
            ::memcpy(&coff_header, loaded_header, sizeof(coff_header));
            auto result_machine_type = to_machine_type(coff_header.machine);
            bool import_object = false;
            if (result_machine_type == MachineType::UNKNOWN)
            {
                ImportHeader import_header;
                ::memcpy(&import_header, loaded_header, sizeof(import_header));
                if (import_header.sig2 == 0xFFFFu)
                {
                    import_object = true;
                    result_machine_type = to_machine_type(import_header.machine);
                }
            }

            if (!import_object)
            {
                // Object files shouldn't have optional headers, but the spec says we should skip over one if any
                f.seek(coff_header.size_of_optional_header, SEEK_CUR);
                // Read section headers
                std::vector<SectionTableHeader> sections;
                sections.resize(coff_header.number_of_sections);
                Checks::check_exit(VCPKG_LINE_INFO,
                                   f.read(sections.data(),
                                          sizeof(SectionTableHeader),
                                          coff_header.number_of_sections) == coff_header.number_of_sections);
                // Look for linker directive sections
                for (auto&& section : sections)
                {
                    if (!(section.characteristics & SectionTableFlags::LinkInfo) ||
                        memcmp(".drectve", &section.name, 8) != 0 || section.number_of_relocations != 0 ||
                        section.number_of_line_numbers != 0)
                    {
                        continue;
                    }

                    // read the actual directive
                    std::string directive_command_line;
                    const auto section_offset = coff_base + section.pointer_to_raw_data;
                    Checks::check_exit(VCPKG_LINE_INFO, f.seek(section_offset, SEEK_SET) == 0);
                    directive_command_line.resize(section.size_of_raw_data);
                    auto fun = f.read(directive_command_line.data(), 1, section.size_of_raw_data);
                    Checks::check_exit(VCPKG_LINE_INFO, fun == section.size_of_raw_data);

                    if (Strings::starts_with(directive_command_line, StringLiteral{"\xEF\xBB\xBF"}))
                    {
                        // chop off the BOM
                        directive_command_line.erase(0, 3);
                    }

                    for (auto&& directive : tokenize_command_line(directive_command_line))
                    {
                        auto insertion_point =
                            std::lower_bound(directives.begin(), directives.end(), directive_command_line);
                        if (insertion_point == directives.end() || *insertion_point != directive)
                        {
                            directives.insert(insertion_point, std::move(directive));
                        }
                    }
                }
            }

            if (result_machine_type == MachineType::UNKNOWN ||
                std::find(machine_types.begin(), machine_types.end(), result_machine_type) != machine_types.end())
            {
                continue;
            }

            machine_types.push_back(result_machine_type);
        }

        std::sort(machine_types.begin(), machine_types.end());
        return LibInformation{std::move(machine_types), std::move(directives)};
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

    static void handle_uninteresting_command_line_ch(char ch, size_t& slash_count, std::string& this_arg)
    {
        // n backslashes not followed by a quotation mark produce n backslashes
        if (slash_count)
        {
            this_arg.append(slash_count, '\\');
            slash_count = 0;
        }

        this_arg.push_back(ch);
    }

    static void handle_maybe_adding_argument(std::vector<std::string>& result, std::string& this_arg)
    {
        if (!this_arg.empty())
        {
            result.push_back(std::move(this_arg));
            this_arg.clear();
        }
    }

    std::vector<std::string> tokenize_command_line(StringView cmd_line)
    {
        std::vector<std::string> result;
        std::string this_arg;
        bool in_quoted_argument = false;
        std::size_t slash_count = 0;
        for (auto&& ch : cmd_line)
        {
            if (ch == '\\')
            {
                ++slash_count;
            }
            else if (ch == '"')
            {
                // 2n backslashes followed by a quotation mark produce n backslashes followed by an (ending) quotation
                // mark
                //
                // 2n + 1 backslashes followed by a quotation mark produce n backslashes followed by an (escaped)
                // quotation mark
                if (slash_count)
                {
                    this_arg.append(slash_count >> 1, '\\');
                    if (std::exchange(slash_count, std::size_t{}) & 0x1u)
                    {
                        // escaped
                        handle_uninteresting_command_line_ch('"', slash_count, this_arg);
                    }
                    else
                    {
                        // not escaped
                        in_quoted_argument = !in_quoted_argument;
                    }
                }
                else
                {
                    in_quoted_argument = !in_quoted_argument;
                }
            }
            else if (ParserBase::is_whitespace(ch))
            {
                if (in_quoted_argument)
                {
                    handle_uninteresting_command_line_ch(ch, slash_count, this_arg);
                }
                else
                {
                    handle_maybe_adding_argument(result, this_arg);
                }
            }
            else
            {
                handle_uninteresting_command_line_ch(ch, slash_count, this_arg);
            }
        }

        this_arg.append(slash_count, '\\');
        handle_maybe_adding_argument(result, this_arg);
        return result;
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

    ExpectedL<bool> try_read_if_dll_has_exports(const DllMetadata& dll, ReadFilePointer& f)
    {
        if (dll.data_directories.size() < 1)
        {
            Debug::print("No export directory\n");
            return false;
        }

        const auto& export_data_directory = dll.data_directories[0];
        if (export_data_directory.virtual_address == 0)
        {
            Debug::print("Null export directory.\n");
            return false;
        }

        ExportDirectoryTable export_directory_table;
        auto export_read_result = try_read_struct_from_rva(
            dll, f, &export_directory_table, export_data_directory.virtual_address, sizeof(export_directory_table));
        if (!export_read_result.has_value())
        {
            return std::move(export_read_result).error();
        }

        return export_directory_table.address_table_entries != 0;
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

    LibInformation read_lib_information(const ReadFilePointer& f)
    {
        read_and_verify_archive_file_signature(f);
        read_and_skip_first_linker_member(f);
        const auto offsets = read_second_linker_member_offsets(f);
        return read_lib_information_from_archive_members(f, offsets);
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
