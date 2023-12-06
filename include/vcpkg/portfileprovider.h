#pragma once

#include <vcpkg/base/fwd/expected.h>
#include <vcpkg/base/fwd/span.h>

#include <vcpkg/fwd/portfileprovider.h>
#include <vcpkg/fwd/registries.h>
#include <vcpkg/fwd/versions.h>

#include <vcpkg/sourceparagraph.h>

namespace vcpkg
{
    struct PortFileProvider
    {
        virtual ~PortFileProvider();
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
        virtual View<Version> get_port_versions(StringView port_name) const = 0;
        virtual ~IVersionedPortfileProvider();

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
        virtual ~IBaselineProvider();
    };

    struct IOverlayProvider
    {
        virtual ~IOverlayProvider();
        virtual Optional<const SourceControlFileAndLocation&> get_control_file(StringView port_name) const = 0;
    };

    struct IFullOverlayProvider : IOverlayProvider
    {
        virtual void load_all_control_files(std::map<std::string, const SourceControlFileAndLocation*>& out) const = 0;
    };

    struct PathsPortFileProvider : PortFileProvider
    {
        explicit PathsPortFileProvider(const ReadOnlyFilesystem& fs,
                                       const RegistrySet& registry_set,
                                       std::unique_ptr<IFullOverlayProvider>&& overlay);
        ExpectedL<const SourceControlFileAndLocation&> get_control_file(const std::string& src_name) const override;
        std::vector<const SourceControlFileAndLocation*> load_all_control_files() const override;

    private:
        std::unique_ptr<IBaselineProvider> m_baseline;
        std::unique_ptr<IFullVersionedPortfileProvider> m_versioned;
        std::unique_ptr<IFullOverlayProvider> m_overlay;
    };

    std::unique_ptr<IBaselineProvider> make_baseline_provider(const RegistrySet& registry_set);
    std::unique_ptr<IFullVersionedPortfileProvider> make_versioned_portfile_provider(const ReadOnlyFilesystem& fs,
                                                                                     const RegistrySet& registry_set);
    std::unique_ptr<IFullOverlayProvider> make_overlay_provider(const ReadOnlyFilesystem& fs,
                                                                const Path& original_cwd,
                                                                View<std::string> overlay_ports);
    std::unique_ptr<IOverlayProvider> make_manifest_provider(const ReadOnlyFilesystem& fs,
                                                             const Path& original_cwd,
                                                             View<std::string> overlay_ports,
                                                             const Path& manifest_path,
                                                             std::unique_ptr<SourceControlFile>&& manifest_scf);
}
