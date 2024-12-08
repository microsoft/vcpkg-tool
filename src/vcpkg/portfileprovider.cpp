#include <vcpkg/base/cache.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/util.h>

#include <vcpkg/metrics.h>
#include <vcpkg/paragraphs.h>
#include <vcpkg/portfileprovider.h>
#include <vcpkg/registries.h>
#include <vcpkg/sourceparagraph.h>

using namespace vcpkg;

namespace vcpkg
{
    MapPortFileProvider::MapPortFileProvider(const std::unordered_map<std::string, SourceControlFileAndLocation>& map)
        : ports(map)
    {
    }

    ExpectedL<const SourceControlFileAndLocation&> MapPortFileProvider::get_control_file(const std::string& spec) const
    {
        auto scf = ports.find(spec);
        if (scf == ports.end()) return msg::format(msgPortDoesNotExist, msg::package_name = spec);
        return scf->second;
    }

    std::vector<const SourceControlFileAndLocation*> MapPortFileProvider::load_all_control_files() const
    {
        return Util::fmap(ports, [](auto&& kvpair) -> const SourceControlFileAndLocation* { return &kvpair.second; });
    }

    PathsPortFileProvider::PathsPortFileProvider(const RegistrySet& registry_set,
                                                 std::unique_ptr<IFullOverlayProvider>&& overlay)
        : m_baseline(make_baseline_provider(registry_set))
        , m_versioned(make_versioned_portfile_provider(registry_set))
        , m_overlay(std::move(overlay))
    {
    }

    ExpectedL<const SourceControlFileAndLocation&> PathsPortFileProvider::get_control_file(
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
            return std::move(maybe_baseline).error();
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
            BaselineProviderImpl(const RegistrySet& registry_set_) : registry_set(registry_set_) { }
            BaselineProviderImpl(const BaselineProviderImpl&) = delete;
            BaselineProviderImpl& operator=(const BaselineProviderImpl&) = delete;

            virtual ExpectedL<Version> get_baseline_version(StringView port_name) const override
            {
                return m_baseline_cache.get_lazy(port_name, [this, port_name]() -> ExpectedL<Version> {
                    return registry_set.baseline_for_port(port_name).then(
                        [&](Optional<Version>&& maybe_version) -> ExpectedL<Version> {
                            auto version = maybe_version.get();
                            if (!version)
                            {
                                return msg::format_error(msgPortNotInBaseline, msg::package_name = port_name);
                            }

                            return std::move(*version);
                        });
                });
            }

        private:
            const RegistrySet& registry_set;
            Cache<std::string, ExpectedL<Version>> m_baseline_cache;
        };

        struct VersionedPortfileProviderImpl : IFullVersionedPortfileProvider
        {
            VersionedPortfileProviderImpl(const RegistrySet& rset) : m_registry_set(rset) { }
            VersionedPortfileProviderImpl(const VersionedPortfileProviderImpl&) = delete;
            VersionedPortfileProviderImpl& operator=(const VersionedPortfileProviderImpl&) = delete;

            const ExpectedL<std::unique_ptr<RegistryEntry>>& entry(StringView name) const
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
                            entry_it = m_entry_cache
                                           .emplace(name.to_string(),
                                                    msg::format(msgPortDoesNotExist, msg::package_name = name))
                                           .first;
                        }
                    }
                    else
                    {
                        entry_it = m_entry_cache
                                       .emplace(name.to_string(),
                                                msg::format_error(msgNoRegistryForPort, msg::package_name = name))
                                       .first;
                    }
                }
                return entry_it->second;
            }

            virtual View<Version> get_port_versions(StringView port_name) const override
            {
                return entry(port_name)
                    .value_or_exit(VCPKG_LINE_INFO)
                    ->get_port_versions()
                    .value_or_exit(VCPKG_LINE_INFO);
            }

            ExpectedL<SourceControlFileAndLocation> load_control_file(const VersionSpec& version_spec) const
            {
                const auto& maybe_ent = entry(version_spec.port_name);
                if (auto ent = maybe_ent.get())
                {
                    if (!ent->get())
                    {
                        return msg::format_error(msgPortDoesNotExist, msg::package_name = version_spec.port_name);
                    }

                    auto maybe_scfl = ent->get()->try_load_port(version_spec.version);
                    if (auto scfl = maybe_scfl.get())
                    {
                        auto scf_vspec = scfl->to_version_spec();
                        if (scf_vspec == version_spec)
                        {
                            return std::move(*scfl);
                        }
                        else
                        {
                            return msg::format_error(msgVersionSpecMismatch,
                                                     msg::path = scfl->control_path,
                                                     msg::expected_version = version_spec,
                                                     msg::actual_version = scf_vspec);
                        }
                    }
                    else
                    {
                        return maybe_scfl.error()
                            .append_raw('\n')
                            .append_raw(NotePrefix)
                            .append(msgWhileLoadingPortVersion, msg::version_spec = version_spec)
                            .append_raw('\n');
                    }
                }

                return maybe_ent.error();
            }

            virtual ExpectedL<const SourceControlFileAndLocation&> get_control_file(
                const VersionSpec& version_spec) const override
            {
                auto it = m_control_cache.find(version_spec);
                if (it == m_control_cache.end())
                {
                    it = m_control_cache.emplace(version_spec, load_control_file(version_spec)).first;
                }

                return it->second.map(
                    [](const SourceControlFileAndLocation& x) -> const SourceControlFileAndLocation& { return x; });
            }

            virtual void load_all_control_files(
                std::map<std::string, const SourceControlFileAndLocation*>& out) const override
            {
                auto all_ports = Paragraphs::load_all_registry_ports(m_registry_set);
                for (auto&& scfl : all_ports)
                {
                    auto it = m_control_cache.emplace(scfl.to_version_spec(), std::move(scfl)).first;
                    out.emplace(it->first.port_name, &it->second.value_or_exit(VCPKG_LINE_INFO));
                }
            }

        private:
            const RegistrySet& m_registry_set;
            mutable std::unordered_map<VersionSpec, ExpectedL<SourceControlFileAndLocation>, VersionSpecHasher>
                m_control_cache;
            mutable std::map<std::string, ExpectedL<std::unique_ptr<RegistryEntry>>, std::less<>> m_entry_cache;
        };

        struct OverlayProviderImpl : IFullOverlayProvider
        {
            OverlayProviderImpl(const ReadOnlyFilesystem& fs, View<Path> overlay_ports)
                : m_fs(fs), m_overlay_ports(overlay_ports.begin(), overlay_ports.end())
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
                        auto maybe_scfl =
                            Paragraphs::try_load_port_required(m_fs, port_name, PortLocation{ports_dir}).maybe_scfl;
                        if (auto scfl = maybe_scfl.get())
                        {
                            if (scfl->to_name() == port_name)
                            {
                                return std::move(*scfl);
                            }
                        }
                        else
                        {
                            print_error_message(maybe_scfl.error());
                            msg::println();
                            Checks::exit_maybe_upgrade(VCPKG_LINE_INFO);
                        }

                        continue;
                    }

                    auto ports_spec = ports_dir / port_name;
                    if (Paragraphs::is_port_directory(m_fs, ports_spec))
                    {
                        auto found_scfl =
                            Paragraphs::try_load_port_required(m_fs, port_name, PortLocation{ports_spec}).maybe_scfl;
                        if (auto scfl = found_scfl.get())
                        {
                            auto& scfl_name = scfl->to_name();
                            if (scfl_name == port_name)
                            {
                                return std::move(*scfl);
                            }

                            Checks::msg_exit_maybe_upgrade(VCPKG_LINE_INFO,
                                                           LocalizedString::from_raw(ports_spec)
                                                               .append_raw(": ")
                                                               .append_raw(ErrorPrefix)
                                                               .append(msgMismatchedNames,
                                                                       msg::package_name = port_name,
                                                                       msg::actual = scfl_name));
                        }
                        else
                        {
                            print_error_message(found_scfl.error());
                            msg::println();
                            Checks::exit_maybe_upgrade(VCPKG_LINE_INFO);
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
                        auto maybe_scfl =
                            Paragraphs::try_load_port_required(m_fs, ports_dir.filename(), PortLocation{ports_dir})
                                .maybe_scfl;
                        if (auto scfl = maybe_scfl.get())
                        {
                            // copy name before moving *scfl
                            auto name = scfl->to_name();
                            auto it = m_overlay_cache.emplace(std::move(name), std::move(*scfl)).first;
                            Checks::check_exit(VCPKG_LINE_INFO, it->second.get());
                            out.emplace(it->first, it->second.get());
                        }
                        else
                        {
                            print_error_message(maybe_scfl.error());
                            msg::println();
                            Checks::exit_maybe_upgrade(VCPKG_LINE_INFO);
                        }

                        continue;
                    }

                    // Try loading all ports inside ports_dir
                    auto results = Paragraphs::try_load_overlay_ports(m_fs, ports_dir);
                    if (!results.errors.empty())
                    {
                        print_error_message(LocalizedString::from_raw(Strings::join(
                            "\n",
                            results.errors,
                            [](const std::pair<std::string, LocalizedString>& err) -> const LocalizedString& {
                                return err.second;
                            })));
                        Checks::exit_maybe_upgrade(VCPKG_LINE_INFO);
                    }

                    for (auto&& scfl : results.paragraphs)
                    {
                        auto name = scfl.to_name();
                        auto it = m_overlay_cache.emplace(std::move(name), std::move(scfl)).first;
                        Checks::check_exit(VCPKG_LINE_INFO, it->second.get());
                        out.emplace(it->first, it->second.get());
                    }
                }
            }

        private:
            const ReadOnlyFilesystem& m_fs;
            const std::vector<Path> m_overlay_ports;
            mutable std::map<std::string, Optional<SourceControlFileAndLocation>, std::less<>> m_overlay_cache;
        };

        struct ManifestProviderImpl : IFullOverlayProvider
        {
            ManifestProviderImpl(const ReadOnlyFilesystem& fs,
                                 View<Path> overlay_ports,
                                 const Path& manifest_path,
                                 std::unique_ptr<SourceControlFile>&& manifest_scf)
                : m_overlay_ports{fs, overlay_ports}
                , m_manifest_scf_and_location{std::move(manifest_scf), manifest_path}
            {
            }

            virtual Optional<const SourceControlFileAndLocation&> get_control_file(StringView port_name) const override
            {
                if (port_name == m_manifest_scf_and_location.to_name())
                {
                    return m_manifest_scf_and_location;
                }

                return m_overlay_ports.get_control_file(port_name);
            }

            virtual void load_all_control_files(
                std::map<std::string, const SourceControlFileAndLocation*>& out) const override
            {
                m_overlay_ports.load_all_control_files(out);
                out.emplace(std::piecewise_construct,
                            std::forward_as_tuple(m_manifest_scf_and_location.to_name()),
                            std::forward_as_tuple(&m_manifest_scf_and_location));
            }

            OverlayProviderImpl m_overlay_ports;
            SourceControlFileAndLocation m_manifest_scf_and_location;
        };
    } // unnamed namespace

    std::unique_ptr<IBaselineProvider> make_baseline_provider(const RegistrySet& registry_set)
    {
        return std::make_unique<BaselineProviderImpl>(registry_set);
    }

    std::unique_ptr<IFullVersionedPortfileProvider> make_versioned_portfile_provider(const RegistrySet& registry_set)
    {
        return std::make_unique<VersionedPortfileProviderImpl>(registry_set);
    }

    std::unique_ptr<IFullOverlayProvider> make_overlay_provider(const ReadOnlyFilesystem& fs, View<Path> overlay_ports)
    {
        return std::make_unique<OverlayProviderImpl>(fs, overlay_ports);
    }

    std::unique_ptr<IOverlayProvider> make_manifest_provider(const ReadOnlyFilesystem& fs,
                                                             View<Path> overlay_ports,
                                                             const Path& manifest_path,
                                                             std::unique_ptr<SourceControlFile>&& manifest_scf)
    {
        return std::make_unique<ManifestProviderImpl>(fs, overlay_ports, manifest_path, std::move(manifest_scf));
    }

} // namespace vcpkg
