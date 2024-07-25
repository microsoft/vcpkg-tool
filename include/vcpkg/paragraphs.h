#pragma once

#include <vcpkg/base/fwd/expected.h>
#include <vcpkg/base/fwd/stringview.h>

#include <vcpkg/fwd/binaryparagraph.h>
#include <vcpkg/fwd/paragraphparser.h>
#include <vcpkg/fwd/registries.h>

#include <vcpkg/base/diagnostics.h>

#include <vcpkg/sourceparagraph.h>

#include <utility>
#include <vector>

namespace vcpkg::Paragraphs
{
    uint64_t get_load_ports_stats();

    Optional<Paragraph> parse_single_merged_paragraph(DiagnosticContext& context,
                                                      StringView str,
                                                      StringView origin,
                                                      int init_row);
    Optional<Paragraph> parse_single_paragraph(DiagnosticContext& context,
                                               StringView str,
                                               StringView origin,
                                               int init_row);
    Optional<Paragraph> get_single_paragraph(DiagnosticContext& context,
                                             const ReadOnlyFilesystem& fs,
                                             const Path& control_path);

    ExpectedL<std::vector<Paragraph>> get_paragraphs(const ReadOnlyFilesystem& fs, const Path& control_path);

    Optional<std::vector<Paragraph>> parse_paragraphs(DiagnosticContext& context,
                                                      StringView str,
                                                      StringView origin,
                                                      int init_row);
    ExpectedL<std::vector<Paragraph>> parse_paragraphs(StringView str, StringView origin, int init_row);

    bool is_port_directory(const ReadOnlyFilesystem& fs, const Path& maybe_directory);

    struct PortLoadResult
    {
        ExpectedL<SourceControlFileAndLocation> maybe_scfl;
        std::string on_disk_contents;
    };

    // If an error occurs, the Expected will be in the error state.
    // Otherwise, if the port is known, the maybe_scfl.get()->source_control_file contains the loaded port information.
    // Otherwise, maybe_scfl.get()->source_control_file is nullptr.
    PortLoadResult try_load_port(const ReadOnlyFilesystem& fs, StringView port_name, const PortLocation& port_location);
    // Identical to try_load_port, but the port unknown condition is mapped to an error.
    PortLoadResult try_load_port_required(const ReadOnlyFilesystem& fs,
                                          StringView port_name,
                                          const PortLocation& port_location);
    ExpectedL<std::unique_ptr<SourceControlFile>> try_load_project_manifest_text(StringView text,
                                                                                 StringView control_path,
                                                                                 MessageSink& warning_sink);
    ExpectedL<std::unique_ptr<SourceControlFile>> try_load_port_manifest_text(StringView text,
                                                                              StringView control_path,
                                                                              MessageSink& warning_sink);
    ExpectedL<std::unique_ptr<SourceControlFile>> try_load_control_file_text(StringView text,
                                                                             StringView control_path,
                                                                             int init_row);

    ExpectedL<BinaryControlFile> try_load_cached_package(const ReadOnlyFilesystem& fs,
                                                         const Path& package_dir,
                                                         const PackageSpec& spec);

    struct LoadResults
    {
        std::vector<SourceControlFileAndLocation> paragraphs;
        std::vector<std::pair<std::string, LocalizedString>> errors;
    };

    LoadResults try_load_all_registry_ports(const ReadOnlyFilesystem& fs, const RegistrySet& registries);

    std::vector<SourceControlFileAndLocation> load_all_registry_ports(const ReadOnlyFilesystem& fs,
                                                                      const RegistrySet& registries);

    LoadResults try_load_overlay_ports(const ReadOnlyFilesystem& fs, const Path& dir);
    std::vector<SourceControlFileAndLocation> load_overlay_ports(const ReadOnlyFilesystem& fs, const Path& dir);
}
