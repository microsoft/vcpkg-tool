#include <vcpkg/base/json.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/system.debug.h>

#include <vcpkg/configuration.h>
#include <vcpkg/metrics.h>
#include <vcpkg/paragraphs.h>
#include <vcpkg/portfileprovider.h>
#include <vcpkg/registries.h>
#include <vcpkg/sourceparagraph.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>
#include <vcpkg/versiondeserializers.h>

using namespace vcpkg;

namespace
{
    struct OverlayRegistryEntry final : RegistryEntry
    {
        OverlayRegistryEntry(Path&& p, Version&& v) : root(p), version(v) { }

        View<Version> get_port_versions() const override { return {&version, 1}; }
        ExpectedS<PathAndLocation> get_version(const Version& v) const override
        {
            if (v == version)
            {
                return PathAndLocation{root, ""};
            }
            return Strings::format("Version %s not found; only %s is available.", v.to_string(), version.to_string());
        }

        Path root;
        Version version;
    };
}

namespace vcpkg
{
    MapPortFileProvider::MapPortFileProvider(const std::unordered_map<std::string, SourceControlFileAndLocation>& map)
        : ports(map)
    {
    }

    ExpectedS<const SourceControlFileAndLocation&> MapPortFileProvider::get_control_file(const std::string& spec) const
    {
        auto scf = ports.find(spec);
        if (scf == ports.end()) return std::string("does not exist in map");
        return scf->second;
    }

    std::vector<const SourceControlFileAndLocation*> MapPortFileProvider::load_all_control_files() const
    {
        return Util::fmap(ports, [](auto&& kvpair) -> const SourceControlFileAndLocation* { return &kvpair.second; });
    }

    PathsPortFileProvider::PathsPortFileProvider(const VcpkgPaths& paths, std::unique_ptr<IOverlayProvider>&& overlay)
        : m_baseline(make_baseline_provider(paths))
        , m_versioned(make_versioned_portfile_provider(paths))
        , m_overlay(std::move(overlay))
    {
    }

    ExpectedS<const SourceControlFileAndLocation&> PathsPortFileProvider::get_control_file(
        const std::string& spec) const
    {
        auto maybe_scfl = m_overlay->get_control_file(spec);
        if (auto scfl = maybe_scfl.get())
        {
            return *scfl;
        }
        auto maybe_baseline = m_baseline->get_baseline_version(spec);
        if (auto baseline = maybe_baseline.get())
        {
            return m_versioned->get_control_file({spec, *baseline});
        }
        else
        {
            return std::move(maybe_baseline).error().extract_data();
        }
    }

    std::vector<const SourceControlFileAndLocation*> PathsPortFileProvider::load_all_control_files() const
    {
        std::map<std::string, const SourceControlFileAndLocation*> m;
        m_overlay->load_all_control_files(m);
        m_versioned->load_all_control_files(m);
        return Util::fmap(m, [](const auto& p) { return p.second; });
    }

    namespace
    {
        struct BaselineProviderImpl : IBaselineProvider
        {
            BaselineProviderImpl(const VcpkgPaths& paths_) : paths(paths_) { }
            BaselineProviderImpl(const BaselineProviderImpl&) = delete;
            BaselineProviderImpl& operator=(const BaselineProviderImpl&) = delete;

            virtual ExpectedL<Version> get_baseline_version(StringView port_name) const override
            {
                auto it = m_baseline_cache.find(port_name);
                if (it != m_baseline_cache.end())
                {
                    return it->second;
                }
                else
                {
                    auto version = paths.get_registry_set().baseline_for_port(port_name);
                    m_baseline_cache.emplace(port_name.to_string(), version);
                    return version;
                }
            }

        private:
            const VcpkgPaths& paths;
            mutable std::map<std::string, ExpectedL<Version>, std::less<>> m_baseline_cache;
        };

        struct VersionedPortfileProviderImpl : IVersionedPortfileProvider
        {
            VersionedPortfileProviderImpl(const Filesystem& fs, const RegistrySet& rset)
                : m_fs(fs), m_registry_set(rset)
            {
            }
            VersionedPortfileProviderImpl(const VersionedPortfileProviderImpl&) = delete;
            VersionedPortfileProviderImpl& operator=(const VersionedPortfileProviderImpl&) = delete;

            const ExpectedS<std::unique_ptr<RegistryEntry>>& entry(StringView name) const
            {
                auto entry_it = m_entry_cache.find(name);
                if (entry_it == m_entry_cache.end())
                {
                    if (auto reg = m_registry_set.registry_for_port(name))
                    {
                        if (auto entry = reg->get_port_entry(name))
                        {
                            entry_it = m_entry_cache.emplace(name.to_string(), std::move(entry)).first;
                        }
                        else
                        {
                            entry_it =
                                m_entry_cache
                                    .emplace(name.to_string(),
                                             Strings::concat("Error: Could not find a definition for port ", name))
                                    .first;
                        }
                    }
                    else
                    {
                        entry_it = m_entry_cache
                                       .emplace(name.to_string(),
                                                Strings::concat("Error: no registry configured for port ", name))
                                       .first;
                    }
                }
                return entry_it->second;
            }

            virtual View<Version> get_port_versions(StringView port_name) const override
            {
                return entry(port_name).value_or_exit(VCPKG_LINE_INFO)->get_port_versions();
            }

            ExpectedS<std::unique_ptr<SourceControlFileAndLocation>> load_control_file(
                const VersionSpec& version_spec) const
            {
                const auto& maybe_ent = entry(version_spec.port_name);
                if (auto ent = maybe_ent.get())
                {
                    auto maybe_path = ent->get()->get_version(version_spec.version);
                    if (auto path = maybe_path.get())
                    {
                        auto maybe_control_file = Paragraphs::try_load_port(m_fs, path->path);
                        if (auto scf = maybe_control_file.get())
                        {
                            auto scf_vspec = scf->get()->to_version_spec();
                            if (scf_vspec == version_spec)
                            {
                                return std::make_unique<SourceControlFileAndLocation>(SourceControlFileAndLocation{
                                    std::move(*scf),
                                    std::move(path->path),
                                    std::move(path->location),
                                });
                            }
                            else
                            {
                                return msg::format(msg::msgErrorMessage)
                                    .append(msgVersionSpecMismatch,
                                            msg::path = path->path,
                                            msg::expected_version = version_spec,
                                            msg::actual_version = scf_vspec)
                                    .extract_data();
                            }
                        }
                        else
                        {
                            // This should change to a soft error when ParseExpected is eliminated.
                            print_error_message(maybe_control_file.error());
                            Checks::msg_exit_maybe_upgrade(VCPKG_LINE_INFO,
                                                           msgFailedToLoadPort,
                                                           msg::package_name = version_spec.port_name,
                                                           msg::path = path->path);
                        }
                    }
                    else
                    {
                        get_global_metrics_collector().track_define(DefineMetric::VersioningErrorVersion);
                        return maybe_path.error();
                    }
                }
                return maybe_ent.error();
            }

            virtual ExpectedS<const SourceControlFileAndLocation&> get_control_file(
                const VersionSpec& version_spec) const override
            {
                auto it = m_control_cache.find(version_spec);
                if (it == m_control_cache.end())
                {
                    it = m_control_cache.emplace(version_spec, load_control_file(version_spec)).first;
                }
                return it->second.map([](const auto& x) -> const SourceControlFileAndLocation& { return *x.get(); });
            }

            virtual void load_all_control_files(
                std::map<std::string, const SourceControlFileAndLocation*>& out) const override
            {
                auto all_ports = Paragraphs::load_all_registry_ports(m_fs, m_registry_set);
                for (auto&& scfl : all_ports)
                {
                    auto port_name = scfl.source_control_file->core_paragraph->name;
                    auto version = scfl.source_control_file->core_paragraph->to_version();
                    auto it = m_control_cache
                                  .emplace(VersionSpec{std::move(port_name), std::move(version)},
                                           std::make_unique<SourceControlFileAndLocation>(std::move(scfl)))
                                  .first;
                    out.emplace(it->first.port_name, it->second.value_or_exit(VCPKG_LINE_INFO).get());
                }
            }

        private:
            const Filesystem& m_fs;
            const RegistrySet& m_registry_set;
            mutable std::
                unordered_map<VersionSpec, ExpectedS<std::unique_ptr<SourceControlFileAndLocation>>, VersionSpecHasher>
                    m_control_cache;
            mutable std::map<std::string, ExpectedS<std::unique_ptr<RegistryEntry>>, std::less<>> m_entry_cache;
        };

        struct OverlayProviderImpl : IOverlayProvider
        {
            OverlayProviderImpl(const VcpkgPaths& paths, View<std::string> overlay_ports)
                : m_fs(paths.get_filesystem())
                , m_overlay_ports(Util::fmap(overlay_ports,
                                             [&paths](const std::string& s) -> Path { return paths.original_cwd / s; }))
            {
                for (auto&& overlay : m_overlay_ports)
                {
                    Debug::println("Using overlay: ", overlay);

                    Checks::msg_check_exit(VCPKG_LINE_INFO,
                                           vcpkg::is_directory(m_fs.status(overlay, VCPKG_LINE_INFO)),
                                           msgOverlayPatchDir,
                                           msg::path = overlay);
                }
            }

            OverlayProviderImpl(const OverlayProviderImpl&) = delete;
            OverlayProviderImpl& operator=(const OverlayProviderImpl&) = delete;

            Optional<SourceControlFileAndLocation> load_port(StringView port_name) const
            {
                auto s_port_name = port_name.to_string();

                for (auto&& ports_dir : m_overlay_ports)
                {
                    // Try loading individual port
                    if (Paragraphs::is_port_directory(m_fs, ports_dir))
                    {
                        auto maybe_scf = Paragraphs::try_load_port(m_fs, ports_dir);
                        if (auto scfp = maybe_scf.get())
                        {
                            auto& scf = *scfp;
                            if (scf->core_paragraph->name == port_name)
                            {
                                return SourceControlFileAndLocation{std::move(scf), ports_dir};
                            }
                        }
                        else
                        {
                            print_error_message(maybe_scf.error());
                            Checks::msg_exit_maybe_upgrade(VCPKG_LINE_INFO,
                                                           msgFailedToLoadPort,
                                                           msg::package_name = port_name,
                                                           msg::path = ports_dir);
                        }

                        continue;
                    }

                    auto ports_spec = ports_dir / port_name;
                    if (Paragraphs::is_port_directory(m_fs, ports_spec))
                    {
                        auto found_scf = Paragraphs::try_load_port(m_fs, ports_spec);
                        if (auto scfp = found_scf.get())
                        {
                            auto& scf = *scfp;
                            if (scf->core_paragraph->name == port_name)
                            {
                                return SourceControlFileAndLocation{std::move(scf), std::move(ports_spec)};
                            }
                            Checks::msg_exit_maybe_upgrade(
                                VCPKG_LINE_INFO,
                                msg::format(msgFailedToLoadPort, msg::package_name = port_name, msg::path = ports_spec)
                                    .append_raw('\n')
                                    .append(msgMismatchedNames,
                                            msg::package_name = port_name,
                                            msg::actual = scf->core_paragraph->name));
                        }
                        else
                        {
                            print_error_message(found_scf.error());
                            Checks::msg_exit_maybe_upgrade(VCPKG_LINE_INFO,
                                                           msgFailedToLoadPort,
                                                           msg::package_name = port_name,
                                                           msg::path = ports_dir);
                        }
                    }
                }
                return nullopt;
            }

            virtual Optional<const SourceControlFileAndLocation&> get_control_file(StringView port_name) const override
            {
                auto it = m_overlay_cache.find(port_name);
                if (it == m_overlay_cache.end())
                {
                    it = m_overlay_cache.emplace(port_name.to_string(), load_port(port_name)).first;
                }
                return it->second;
            }

            virtual void load_all_control_files(
                std::map<std::string, const SourceControlFileAndLocation*>& out) const override
            {
                auto first = std::make_reverse_iterator(m_overlay_ports.end());
                const auto last = std::make_reverse_iterator(m_overlay_ports.begin());
                for (; first != last; ++first)
                {
                    auto&& ports_dir = *first;
                    // Try loading individual port
                    if (Paragraphs::is_port_directory(m_fs, ports_dir))
                    {
                        auto maybe_scf = Paragraphs::try_load_port(m_fs, ports_dir);
                        if (auto scfp = maybe_scf.get())
                        {
                            SourceControlFileAndLocation scfl{std::move(*scfp), ports_dir};
                            auto name = scfl.source_control_file->core_paragraph->name;
                            auto it = m_overlay_cache.emplace(std::move(name), std::move(scfl)).first;
                            Checks::check_exit(VCPKG_LINE_INFO, it->second.get());
                            out.emplace(it->first, it->second.get());
                        }
                        else
                        {
                            print_error_message(maybe_scf.error());
                            Checks::msg_exit_maybe_upgrade(
                                VCPKG_LINE_INFO, msgFailedToLoadUnnamedPortFromPath, msg::path = ports_dir);
                        }

                        continue;
                    }

                    // Try loading all ports inside ports_dir
                    auto found_scfls = Paragraphs::load_overlay_ports(m_fs, ports_dir);
                    for (auto&& scfl : found_scfls)
                    {
                        auto name = scfl.source_control_file->core_paragraph->name;
                        auto it = m_overlay_cache.emplace(std::move(name), std::move(scfl)).first;
                        Checks::check_exit(VCPKG_LINE_INFO, it->second.get());
                        out.emplace(it->first, it->second.get());
                    }
                }
            }

        private:
            const Filesystem& m_fs;
            const std::vector<Path> m_overlay_ports;
            mutable std::map<std::string, Optional<SourceControlFileAndLocation>, std::less<>> m_overlay_cache;
        };

        struct ManifestProviderImpl : IOverlayProvider
        {
            ManifestProviderImpl(const VcpkgPaths& paths,
                                 View<std::string> overlay_ports,
                                 const Path& manifest_path,
                                 std::unique_ptr<SourceControlFile>&& manifest_scf)
                : m_overlay_ports{paths, overlay_ports}
                , m_manifest_scf_and_location{std::move(manifest_scf), manifest_path}
            {
            }

            virtual Optional<const SourceControlFileAndLocation&> get_control_file(StringView port_name) const override
            {
                if (port_name == m_manifest_scf_and_location.source_control_file->core_paragraph->name)
                {
                    return m_manifest_scf_and_location;
                }

                return m_overlay_ports.get_control_file(port_name);
            }

            virtual void load_all_control_files(
                std::map<std::string, const SourceControlFileAndLocation*>& out) const override
            {
                m_overlay_ports.load_all_control_files(out);
                out.emplace(
                    std::piecewise_construct,
                    std::forward_as_tuple(m_manifest_scf_and_location.source_control_file->core_paragraph->name),
                    std::forward_as_tuple(&m_manifest_scf_and_location));
            }

            OverlayProviderImpl m_overlay_ports;
            SourceControlFileAndLocation m_manifest_scf_and_location;
        };
    } // unnamed namespace

    std::unique_ptr<IBaselineProvider> make_baseline_provider(const vcpkg::VcpkgPaths& paths)
    {
        return std::make_unique<BaselineProviderImpl>(paths);
    }

    std::unique_ptr<IVersionedPortfileProvider> make_versioned_portfile_provider(const vcpkg::VcpkgPaths& paths)
    {
        return std::make_unique<VersionedPortfileProviderImpl>(paths.get_filesystem(), paths.get_registry_set());
    }

    std::unique_ptr<IOverlayProvider> make_overlay_provider(const vcpkg::VcpkgPaths& paths,
                                                            View<std::string> overlay_ports)
    {
        return std::make_unique<OverlayProviderImpl>(paths, std::move(overlay_ports));
    }

    std::unique_ptr<IOverlayProvider> make_manifest_provider(const vcpkg::VcpkgPaths& paths,
                                                             View<std::string> overlay_ports,
                                                             const Path& manifest_path,
                                                             std::unique_ptr<SourceControlFile>&& manifest_scf)
    {
        return std::make_unique<ManifestProviderImpl>(paths, overlay_ports, manifest_path, std::move(manifest_scf));
    }

} // namespace vcpkg
