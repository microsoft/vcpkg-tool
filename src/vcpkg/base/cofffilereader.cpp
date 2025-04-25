#include <vcpkg/base/checks.h>
#include <vcpkg/base/cofffilereader.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/optional.h>
#include <vcpkg/base/parse.h>
#include <vcpkg/base/stringview.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/util.h>

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

    // reads f as a portable executable and checks for magic number signatures.
    // if an I/O error occurs, returns the error; otherwise,
    // returns iff signatures match
    ExpectedL<bool> read_pe_signature_and_get_coff_header_offset(ReadFilePointer& f)
    {
        static constexpr StringLiteral EXPECTED_MZ_HEADER = "MZ";
        {
            char mz_signature[EXPECTED_MZ_HEADER.size()];
            auto maybe_read_mz = f.try_read_all_from(0, mz_signature, sizeof(mz_signature));
            if (!maybe_read_mz)
            {
                return std::move(maybe_read_mz).error();
            }

            if (EXPECTED_MZ_HEADER != StringView{mz_signature, sizeof(mz_signature)})
            {
                return false;
            }
        }

        static constexpr long OFFSET_TO_PE_SIGNATURE_OFFSET = 0x3c;
        static constexpr StringLiteral EXPECTED_PE_SIGNATURE = "PE\0\0";
        uint32_t offset;
        {
            auto read_offset = f.try_read_all_from(OFFSET_TO_PE_SIGNATURE_OFFSET, &offset, sizeof(offset));
            if (!read_offset.has_value())
            {
                return std::move(read_offset).error();
            }
        }

        char pe_signature[EXPECTED_PE_SIGNATURE.size()];
        {
            auto read_signature = f.try_read_all_from(offset, pe_signature, sizeof(pe_signature));
            if (!read_signature.has_value())
            {
                return std::move(read_signature).error();
            }
        }

        return EXPECTED_PE_SIGNATURE == StringView{pe_signature, sizeof(pe_signature)};
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
        const auto load_config_data_directory = metadata.try_get_image_data_directory(10);
        if (!load_config_data_directory)
        {
            // metadata.load_config_type = LoadConfigType::UnsetOrOld;
            return Unit{};
        }

        auto maybe_remaining_size = try_seek_to_rva(metadata, f, load_config_data_directory->virtual_address);
        if (const auto remaining_size = maybe_remaining_size.get())
        {
            if (*remaining_size < load_config_data_directory->size)
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

    ExpectedL<Unit> read_and_verify_archive_file_signature(ReadFilePointer& f)
    {
        static constexpr StringLiteral FILE_START = "!<arch>\n";
        static constexpr auto FILE_START_SIZE = FILE_START.size();
        return f.try_seek_to(0).then([&f](Unit) -> ExpectedL<Unit> {
            char file_start[FILE_START_SIZE];
            auto result = f.try_read_all(file_start, FILE_START_SIZE);
            if (result.has_value() && FILE_START != StringView{file_start, FILE_START_SIZE})
            {
                return msg::format_error(msgIncorrectArchiveFileSignature);
            }

            return result;
        });
    }

    uint32_t bswap(uint32_t value)
    {
        return (value >> 24) | ((value & 0x00FF0000u) >> 8) | ((value & 0x0000FF00u) << 8) | (value << 24);
    }

    ExpectedL<std::vector<uint32_t>> try_read_first_linker_member_offsets(ReadFilePointer& f)
    {
        ArchiveMemberHeader first_linker_member_header;
        Checks::check_exit(VCPKG_LINE_INFO,
                           f.read(&first_linker_member_header, sizeof(first_linker_member_header), 1) == 1);
        if (memcmp(first_linker_member_header.name, "/ ", 2) != 0)
        {
            return msg::format_error(msgLibraryFirstLinkerMemberMissing);
        }

        auto first_size = first_linker_member_header.decoded_size();
        uint32_t archive_symbol_count;
        if (first_size < sizeof(archive_symbol_count))
        {
            return msg::format_error(msgLibraryArchiveMemberTooSmall);
        }

        {
            auto read = f.try_read_all(&archive_symbol_count, sizeof(archive_symbol_count));
            if (!read.has_value())
            {
                return std::move(read).error();
            }
        }

        archive_symbol_count = bswap(archive_symbol_count);
        const auto maximum_possible_archive_members = (first_size / sizeof(uint32_t)) - 1;
        if (archive_symbol_count > maximum_possible_archive_members)
        {
            return msg::format_error(msgLibraryArchiveMemberTooSmall);
        }

        std::vector<uint32_t> offsets(archive_symbol_count);
        {
            auto read = f.try_read_all(offsets.data(), sizeof(uint32_t) * archive_symbol_count);
            if (!read.has_value())
            {
                return std::move(read).error();
            }
        }

        // convert (big endian) offsets to little endian
        for (auto&& offset : offsets)
        {
            offset = bswap(offset);
        }

        offsets.erase(std::remove(offsets.begin(), offsets.end(), 0u), offsets.end());
        Util::sort_unique_erase(offsets);
        uint64_t leftover = first_size - sizeof(uint32_t) - (archive_symbol_count * sizeof(uint32_t));
        {
            auto seek = f.try_seek_to(leftover, SEEK_CUR);
            if (!seek.has_value())
            {
                return std::move(seek).error();
            }
        }

        return offsets;
    }

    ExpectedL<Optional<std::vector<uint32_t>>> try_read_second_linker_member_offsets(ReadFilePointer& f)
    {
        ArchiveMemberHeader second_linker_member_header;
        {
            auto read = f.try_read_all(&second_linker_member_header, sizeof(second_linker_member_header));
            if (!read.has_value())
            {
                return std::move(read).error();
            }
        }

        if (memcmp(second_linker_member_header.name, "/ ", 2) != 0)
        {
            return nullopt;
        }

        const auto second_size = second_linker_member_header.decoded_size();
        // The first 4 bytes contains the number of archive members
        uint32_t archive_member_count;

        if (second_size < sizeof(archive_member_count))
        {
            return msg::format_error(msgLibraryArchiveMemberTooSmall);
        }

        {
            auto read = f.try_read_all(&archive_member_count, sizeof(archive_member_count));
            if (!read.has_value())
            {
                return std::move(read).error();
            }
        }

        const auto maximum_possible_archive_members = (second_size / sizeof(uint32_t)) - 1;
        if (archive_member_count > maximum_possible_archive_members)
        {
            return msg::format_error(msgLibraryArchiveMemberTooSmall);
        }

        std::vector<uint32_t> offsets(archive_member_count);
        {
            auto read = f.try_read_all(offsets.data(), sizeof(uint32_t) * archive_member_count);
            if (!read.has_value())
            {
                return std::move(read).error();
            }
        }

        // Ignore offsets that point to offset 0. See vcpkg github #223 #288 #292
        offsets.erase(std::remove(offsets.begin(), offsets.end(), 0u), offsets.end());
        // Sort the offsets, because it is possible for them to be unsorted. See vcpkg github #292
        std::sort(offsets.begin(), offsets.end());
        uint64_t leftover = second_size - sizeof(uint32_t) - (archive_member_count * sizeof(uint32_t));
        {
            auto seek = f.try_seek_to(leftover, SEEK_CUR);
            if (!seek.has_value())
            {
                return std::move(seek).error();
            }
        }

        return offsets;
    }

    static void add_machine_type(std::vector<MachineType>& machine_types, MachineType machine_type)
    {
        if (machine_type == MachineType::UNKNOWN ||
            std::find(machine_types.begin(), machine_types.end(), machine_type) != machine_types.end())
        {
            return;
        }

        machine_types.push_back(machine_type);
    }

    ExpectedL<LibInformation> read_lib_information_from_archive_members(ReadFilePointer& f,
                                                                        const std::vector<uint32_t>& member_offsets)
    {
        std::vector<MachineType> machine_types; // used as set because n is tiny
        std::set<std::string, std::less<>> directives;
        // Next we have the obj and pseudo-object files
        for (unsigned int offset : member_offsets)
        {
            // Skip the header, no need to read it
            const auto coff_base = offset + sizeof(ArchiveMemberHeader);
            uint32_t tag_sniffer;
            {
                auto read = f.try_read_all_from(coff_base, &tag_sniffer, sizeof(tag_sniffer));
                if (!read.has_value())
                {
                    return std::move(read).error();
                }
            }

            if (tag_sniffer == LlvmBitcodeSignature)
            {
                // obj is LLVM bitcode
                add_machine_type(machine_types, MachineType::LLVM_BITCODE);
            }
            else if (tag_sniffer == ImportHeaderSignature)
            {
                // obj is an import obj
                ImportHeaderAfterSignature import_header;
                {
                    auto read = f.try_read_all(&import_header, sizeof(import_header));
                    if (!read.has_value())
                    {
                        return std::move(read).error();
                    }
                }

                add_machine_type(machine_types, static_cast<MachineType>(import_header.machine));
            }
            else
            {
                // obj is traditional COFF
                CoffFileHeaderSignature coff_signature;
                static_assert(sizeof(tag_sniffer) == sizeof(coff_signature), "BOOM");
                memcpy(&coff_signature, &tag_sniffer, sizeof(tag_sniffer));
                add_machine_type(machine_types, static_cast<MachineType>(coff_signature.machine));

                CoffFileHeaderAfterSignature coff_header;
                {
                    auto read = f.try_read_all(&coff_header, sizeof(coff_header));
                    if (!read.has_value())
                    {
                        return std::move(read).error();
                    }
                }

                // Object files shouldn't have optional headers, but the spec says we should skip over one if any
                {
                    auto seek = f.try_seek_to(coff_header.size_of_optional_header, SEEK_CUR);
                    if (!seek.has_value())
                    {
                        return std::move(seek).error();
                    }
                }

                // Read section headers
                std::vector<SectionTableHeader> sections;
                sections.resize(coff_signature.number_of_sections);
                {
                    auto read =
                        f.try_read_all(sections.data(), sizeof(SectionTableHeader) * coff_signature.number_of_sections);
                    if (!read.has_value())
                    {
                        return std::move(read).error();
                    }
                }

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
                    directive_command_line.resize(section.size_of_raw_data);
                    {
                        auto read = f.try_read_all_from(
                            section_offset, directive_command_line.data(), section.size_of_raw_data);
                        if (!read.has_value())
                        {
                            return std::move(read).error();
                        }
                    }

                    StringView directive_command_line_view{directive_command_line};
                    directive_command_line_view.remove_bom();
                    for (auto&& directive : tokenize_command_line(directive_command_line_view))
                    {
                        directives.insert(std::move(directive));
                    }
                }
            }
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

    const ImageDataDirectory* DllMetadata::try_get_image_data_directory(size_t directory_index) const noexcept
    {
        // pre: try_read_section_headers succeeded on `metadata`
        if (data_directories.size() > directory_index)
        {
            const auto result = data_directories.data() + directory_index;
            if (result->virtual_address != 0)
            {
                return result;
            }
        }

        return nullptr;
    }

    MachineType DllMetadata::get_machine_type() const noexcept { return static_cast<MachineType>(coff_header.machine); }

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

    ExpectedL<Optional<DllMetadata>> try_read_dll_metadata(ReadFilePointer& f)
    {
        Optional<DllMetadata> result;
        {
            auto maybe_signature_ok = read_pe_signature_and_get_coff_header_offset(f);
            if (auto signature_ok = maybe_signature_ok.get())
            {
                if (!*signature_ok)
                {
                    return nullopt;
                }
            }
            else
            {
                return std::move(maybe_signature_ok).error();
            }
        }

        DllMetadata& target = result.emplace();

        {
            auto read_coff = f.try_read_all(&target.coff_header, sizeof(target.coff_header));
            if (!read_coff.has_value())
            {
                return std::move(read_coff).error();
            }
        }

        {
            auto read_optional_header = try_read_optional_header(target, f);
            if (!read_optional_header.has_value())
            {
                return std::move(read_optional_header).error();
            }
        }

        {
            auto read_section_headers = try_read_section_headers(target, f);
            if (!read_section_headers)
            {
                return std::move(read_section_headers).error();
            }
        }

        {
            auto read_load_config_directory = try_read_image_config_directory(target, f);
            if (!read_load_config_directory)
            {
                return std::move(read_load_config_directory).error();
            }
        }

        return result;
    }

    ExpectedL<DllMetadata> try_read_dll_metadata_required(ReadFilePointer& f)
    {
        return try_read_dll_metadata(f).then([&](Optional<DllMetadata>&& maybe_metadata) -> ExpectedL<DllMetadata> {
            if (auto metadata = maybe_metadata.get())
            {
                return std::move(*metadata);
            }

            return LocalizedString::from_raw(f.path())
                .append_raw(": ")
                .append_raw(ErrorPrefix)
                .append(msgFileIsNotExecutable);
        });
    }

    ExpectedL<bool> try_read_if_dll_has_exports(const DllMetadata& dll, ReadFilePointer& f)
    {
        const auto export_data_directory = dll.try_get_image_data_directory(0);
        if (!export_data_directory)
        {
            Debug::print("No export directory.\n");
            return false;
        }

        ExportDirectoryTable export_directory_table;
        auto export_read_result = try_read_struct_from_rva(
            dll, f, &export_directory_table, export_data_directory->virtual_address, sizeof(export_directory_table));
        if (!export_read_result.has_value())
        {
            return std::move(export_read_result).error();
        }

        return export_directory_table.address_table_entries != 0;
    }

    ExpectedL<std::vector<std::string>> try_read_dll_imported_dll_names(const DllMetadata& dll, ReadFilePointer& f)
    {
        const auto import_data_directory = dll.try_get_image_data_directory(1);
        if (!import_data_directory)
        {
            Debug::print("No import directory\n");
            return std::vector<std::string>{};
        }

        return try_seek_to_rva(dll, f, import_data_directory->virtual_address)
            .then([&](size_t remaining_size) -> ExpectedL<std::vector<std::string>> {
                if (remaining_size < import_data_directory->size)
                {
                    return msg::format(msgPEImportCrossesSectionBoundary, msg::path = f.path());
                }

                std::vector<std::string> results;
                remaining_size = import_data_directory->size;
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

    ExpectedL<LibInformation> read_lib_information(ReadFilePointer& f)
    {
        auto read_signature = read_and_verify_archive_file_signature(f);
        if (!read_signature.has_value())
        {
            return std::move(read_signature).error();
        }

        auto first_offsets = try_read_first_linker_member_offsets(f);
        if (!first_offsets.has_value())
        {
            return std::move(first_offsets).error();
        }

        auto second_offsets = try_read_second_linker_member_offsets(f);
        if (!second_offsets.has_value())
        {
            return std::move(second_offsets).error();
        }

        const std::vector<uint32_t>* offsets;
        // "Although both linker members provide a directory of symbols and archive members that
        // contain them, the second linker member is used in preference to the first by all current linkers."
        if (auto maybe_offsets2 = second_offsets.get()->get())
        {
            offsets = maybe_offsets2;
        }
        else if (auto maybe_offsets = first_offsets.get())
        {
            offsets = maybe_offsets;
        }
        else
        {
            return msg::format_error(msgInvalidLibraryMissingLinkerMembers);
        }

        return read_lib_information_from_archive_members(f, *offsets);
    }
}
