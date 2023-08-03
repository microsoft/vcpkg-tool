#pragma once

#include <vcpkg/fwd/binaryparagraph.h>
#include <vcpkg/fwd/paragraphparser.h>
#include <vcpkg/fwd/registries.h>

#include <vcpkg/base/expected.h>

#include <vcpkg/sourceparagraph.h>

namespace vcpkg::Paragraphs
{
    uint64_t get_load_ports_stats();

    ExpectedL<Paragraph> parse_single_merged_paragraph(StringView str, StringView origin);
    ExpectedL<Paragraph> parse_single_paragraph(StringView str, StringView origin);
    ExpectedL<Paragraph> get_single_paragraph(const ReadOnlyFilesystem& fs, const Path& control_path);

    ExpectedL<std::vector<Paragraph>> get_paragraphs(const ReadOnlyFilesystem& fs, const Path& control_path);

    ExpectedL<std::vector<Paragraph>> parse_paragraphs(StringView str, StringView origin);

    bool is_port_directory(const ReadOnlyFilesystem& fs, const Path& maybe_directory);

    ParseExpected<SourceControlFile> try_load_port(const ReadOnlyFilesystem& fs, const Path& port_directory);
    ParseExpected<SourceControlFile> try_load_port_text(const std::string& text,
                                                        StringView origin,
                                                        bool is_manifest,
                                                        MessageSink& warning_sink);

    ExpectedL<BinaryControlFile> try_load_cached_package(const ReadOnlyFilesystem& fs,
                                                         const Path& package_dir,
                                                         const PackageSpec& spec);

    struct LoadResults
    {
        std::vector<SourceControlFileAndLocation> paragraphs;
        std::vector<std::unique_ptr<ParseControlErrorInfo>> errors;
    };

    // this allows one to pass this around as an overload set to stuff like `Util::fmap`,
    // as opposed to making it a function
    constexpr struct
    {
        const std::string& operator()(const SourceControlFileAndLocation* loc) const
        {
            return (*this)(*loc->source_control_file);
        }
        const std::string& operator()(const SourceControlFileAndLocation& loc) const
        {
            return (*this)(*loc.source_control_file);
        }
        const std::string& operator()(const SourceControlFile& scf) const { return scf.core_paragraph->name; }
    } get_name_of_control_file;

    LoadResults try_load_all_registry_ports(const ReadOnlyFilesystem& fs, const RegistrySet& registries);

    std::vector<SourceControlFileAndLocation> load_all_registry_ports(const ReadOnlyFilesystem& fs,
                                                                      const RegistrySet& registries);
    std::vector<SourceControlFileAndLocation> load_overlay_ports(const ReadOnlyFilesystem& fs, const Path& dir);
}
