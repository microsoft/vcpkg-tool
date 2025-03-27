#pragma once

#include <vcpkg/base/fwd/expected.h>
#include <vcpkg/base/fwd/span.h>

#include <vcpkg/fwd/portfileprovider.h>
#include <vcpkg/fwd/registries.h>
#include <vcpkg/fwd/sourceparagraph.h>
#include <vcpkg/fwd/versions.h>

#include <vcpkg/base/path.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace vcpkg
{
    struct OverlayPortPaths
    {
        Optional<Path> builtin_overlay_port_dir;
        std::vector<Path> overlay_ports;

        bool empty() const noexcept;
    };

    struct OverlayPortIndexEntry
    {
        OverlayPortIndexEntry(OverlayPortKind kind, const Path& directory);
        OverlayPortIndexEntry(const OverlayPortIndexEntry&) = delete;
        OverlayPortIndexEntry(OverlayPortIndexEntry&&);

        const ExpectedL<SourceControlFileAndLocation>* try_load_port(const ReadOnlyFilesystem& fs,
                                                                     StringView port_name);

        ExpectedL<Unit> try_load_all_ports(const ReadOnlyFilesystem& fs,
                                           std::map<std::string, const SourceControlFileAndLocation*>& out);

        void check_directory(const ReadOnlyFilesystem& fs) const;

    private:
        OverlayPortKind m_kind;
        Path m_directory;

        using MapT = std::map<std::string, ExpectedL<SourceControlFileAndLocation>, std::less<>>;
        // If kind == OverlayPortKind::Unknown, empty
        // Otherwise, if kind == OverlayPortKind::Port,
        //    upon load success, contains exactly one entry with the loaded name of the port
        //    upon load failure, contains exactly one entry with a key of empty string, value being the load error
        // Otherwise, if kind == OverlayPortKind::Directory, contains an entry for each loaded overlay-port in the
        // directory
        MapT m_loaded_ports;

        OverlayPortKind determine_kind(const ReadOnlyFilesystem& fs);
        const ExpectedL<SourceControlFileAndLocation>* try_load_port_cached_port(StringView port_name);

        MapT::iterator try_load_port_subdirectory_uncached(MapT::iterator hint,
                                                           const ReadOnlyFilesystem& fs,
                                                           StringView port_name);

        const ExpectedL<SourceControlFileAndLocation>* try_load_port_subdirectory_with_cache(
            const ReadOnlyFilesystem& fs, StringView port_name);
    };

    struct PortFileProvider
    {
        virtual ~PortFileProvider() = default;
        virtual ExpectedL<const SourceControlFileAndLocation&> get_control_file(const std::string& src_name) const = 0;
        virtual std::vector<const SourceControlFileAndLocation*> load_all_control_files() const = 0;
    };

    struct MapPortFileProvider : PortFileProvider
    {
        explicit MapPortFileProvider(const std::unordered_map<std::string, SourceControlFileAndLocation>& map);
        MapPortFileProvider(const MapPortFileProvider&) = delete;
        MapPortFileProvider& operator=(const MapPortFileProvider&) = delete;
        ExpectedL<const SourceControlFileAndLocation&> get_control_file(const std::string& src_name) const override;
        std::vector<const SourceControlFileAndLocation*> load_all_control_files() const override;

    private:
        const std::unordered_map<std::string, SourceControlFileAndLocation>& ports;
    };

    struct IVersionedPortfileProvider
    {
        virtual ~IVersionedPortfileProvider() = default;

        virtual ExpectedL<const SourceControlFileAndLocation&> get_control_file(
            const VersionSpec& version_spec) const = 0;
    };

    struct IFullVersionedPortfileProvider : IVersionedPortfileProvider
    {
        virtual void load_all_control_files(std::map<std::string, const SourceControlFileAndLocation*>& out) const = 0;
    };

    struct IBaselineProvider
    {
        virtual ExpectedL<Version> get_baseline_version(StringView port_name) const = 0;
        virtual ~IBaselineProvider() = default;
    };

    struct IOverlayProvider
    {
        virtual ~IOverlayProvider() = default;
        virtual Optional<const SourceControlFileAndLocation&> get_control_file(StringView port_name) const = 0;
    };

    struct IFullOverlayProvider : IOverlayProvider
    {
        virtual void load_all_control_files(std::map<std::string, const SourceControlFileAndLocation*>& out) const = 0;
    };

    struct PathsPortFileProvider : PortFileProvider
    {
        explicit PathsPortFileProvider(const RegistrySet& registry_set,
                                       std::unique_ptr<IFullOverlayProvider>&& overlay);
        ExpectedL<const SourceControlFileAndLocation&> get_control_file(const std::string& src_name) const override;
        std::vector<const SourceControlFileAndLocation*> load_all_control_files() const override;

    private:
        std::unique_ptr<IBaselineProvider> m_baseline;
        std::unique_ptr<IFullVersionedPortfileProvider> m_versioned;
        std::unique_ptr<IFullOverlayProvider> m_overlay;
    };

    std::unique_ptr<IBaselineProvider> make_baseline_provider(const RegistrySet& registry_set);
    std::unique_ptr<IFullVersionedPortfileProvider> make_versioned_portfile_provider(const RegistrySet& registry_set);
    std::unique_ptr<IFullOverlayProvider> make_overlay_provider(const ReadOnlyFilesystem& fs,
                                                                const OverlayPortPaths& overlay_ports);
    std::unique_ptr<IOverlayProvider> make_manifest_provider(const ReadOnlyFilesystem& fs,
                                                             const OverlayPortPaths& overlay_ports,
                                                             const Path& manifest_path,
                                                             std::unique_ptr<SourceControlFile>&& manifest_scf);
}
