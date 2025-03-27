#include <vcpkg/base/cache.h>
#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/util.h>

#include <vcpkg/metrics.h>
#include <vcpkg/paragraphs.h>
#include <vcpkg/portfileprovider.h>
#include <vcpkg/registries.h>
#include <vcpkg/sourceparagraph.h>

#include <functional>
#include <map>

using namespace vcpkg;

namespace
{
    PortLocation get_try_load_port_subdirectory_uncached_location(OverlayPortKind kind,
                                                                  const Path& directory,
                                                                  StringView port_name)
    {
        switch (kind)
        {
            case OverlayPortKind::Directory:
                return PortLocation{directory / port_name, no_assertion, PortSourceKind::Overlay};
            case OverlayPortKind::Builtin:
                return PortLocation{
                    directory / port_name, Paragraphs::builtin_port_spdx_location(port_name), PortSourceKind::Builtin};
            case OverlayPortKind::Unknown:
            case OverlayPortKind::Port:
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }
    }
} // unnamed namespace

namespace vcpkg
{
    OverlayPortIndexEntry::OverlayPortIndexEntry(OverlayPortKind kind, const Path& directory)
        : m_kind(kind), m_directory(directory), m_loaded_ports()
    {
        if (m_kind == OverlayPortKind::Port)
        {
            Checks::unreachable(VCPKG_LINE_INFO);
        }
    }

    OverlayPortIndexEntry::OverlayPortIndexEntry(OverlayPortIndexEntry&&) = default;

    OverlayPortKind OverlayPortIndexEntry::determine_kind(const ReadOnlyFilesystem& fs)
    {
        if (m_kind == OverlayPortKind::Unknown)
        {
            if (!m_loaded_ports.empty())
            {
                Checks::unreachable(VCPKG_LINE_INFO, "OverlayPortKind::Unknown empty cache constraint violated");
            }

            auto maybe_scfl =
                Paragraphs::try_load_port(fs, PortLocation{m_directory, no_assertion, PortSourceKind::Overlay})
                    .maybe_scfl;
            if (auto scfl = maybe_scfl.get())
            {
                if (scfl->source_control_file)
                {
                    // succeeded in loading it, so this must be a port
                    m_kind = OverlayPortKind::Port;
                    auto name_copy = scfl->to_name(); // copy name before moving maybe_scfl
                    m_loaded_ports.emplace(name_copy, std::move(maybe_scfl));
                }
                else
                {
                    // the directory didn't look like a port at all, consider it an overlay-port-dir
                    m_kind = OverlayPortKind::Directory;
                }
            }
            else
            {
                // it looked like a port but we failed to load it for some reason
                m_kind = OverlayPortKind::Port;
                m_loaded_ports.emplace(std::string(), std::move(maybe_scfl));
            }
        }

        return m_kind;
    }

    const ExpectedL<SourceControlFileAndLocation>* OverlayPortIndexEntry::try_load_port_cached_port(
        StringView port_name)
    {
        if (m_kind != OverlayPortKind::Port)
        {
            Checks::unreachable(VCPKG_LINE_INFO);
        }

        const auto& this_overlay = *m_loaded_ports.begin();
        if (auto scfl = this_overlay.second.get())
        {
            if (scfl->to_name() != port_name)
            {
                return nullptr; // this overlay-port is OK, but this isn't the right one
            }
        }

        return &this_overlay.second;
    }

    OverlayPortIndexEntry::MapT::iterator OverlayPortIndexEntry::try_load_port_subdirectory_uncached(
        MapT::iterator hint, const ReadOnlyFilesystem& fs, StringView port_name)
    {
        auto load_result = Paragraphs::try_load_port(
            fs, get_try_load_port_subdirectory_uncached_location(m_kind, m_directory, port_name));
        auto& maybe_scfl = load_result.maybe_scfl;
        if (auto scfl = maybe_scfl.get())
        {
            if (auto scf = scfl->source_control_file.get())
            {
                const auto& actual_name = scf->to_name();
                if (actual_name != port_name)
                {
                    maybe_scfl =
                        LocalizedString::from_raw(scfl->control_path)
                            .append_raw(": ")
                            .append_raw(ErrorPrefix)
                            .append(msgMismatchedNames, msg::package_name = port_name, msg::actual = actual_name);
                }
            }
            else
            {
                return m_loaded_ports.end();
            }
        }

        return m_loaded_ports.emplace_hint(hint, port_name.to_string(), std::move(maybe_scfl));
    }

    const ExpectedL<SourceControlFileAndLocation>* OverlayPortIndexEntry::try_load_port_subdirectory_with_cache(
        const ReadOnlyFilesystem& fs, StringView port_name)
    {
        switch (m_kind)
        {
            case OverlayPortKind::Directory:
            case OverlayPortKind::Builtin:
                // intentionally empty
                break;
            case OverlayPortKind::Unknown:
            case OverlayPortKind::Port:
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }

        auto already_loaded = m_loaded_ports.lower_bound(port_name);
        if (already_loaded == m_loaded_ports.end() || already_loaded->first != port_name)
        {
            already_loaded = try_load_port_subdirectory_uncached(already_loaded, fs, port_name);
        }

        if (already_loaded == m_loaded_ports.end())
        {
            return nullptr;
        }

        return &already_loaded->second;
    }

    const ExpectedL<SourceControlFileAndLocation>* OverlayPortIndexEntry::try_load_port(const ReadOnlyFilesystem& fs,
                                                                                        StringView port_name)
    {
        switch (determine_kind(fs))
        {
            case OverlayPortKind::Port: return try_load_port_cached_port(port_name);
            case OverlayPortKind::Directory:
            case OverlayPortKind::Builtin: return try_load_port_subdirectory_with_cache(fs, port_name);
            case OverlayPortKind::Unknown:
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }
    }

    ExpectedL<Unit> OverlayPortIndexEntry::try_load_all_ports(
        const ReadOnlyFilesystem& fs, std::map<std::string, const SourceControlFileAndLocation*>& out)
    {
        switch (determine_kind(fs))
        {
            case OverlayPortKind::Port:
            {
                auto& maybe_this_port = *m_loaded_ports.begin();
                if (auto this_port = maybe_this_port.second.get())
                {
                    auto already_in_out = out.lower_bound(maybe_this_port.first);
                    if (already_in_out == out.end() || already_in_out->first != maybe_this_port.first)
                    {
                        out.emplace_hint(already_in_out, maybe_this_port.first, this_port);
                    }

                    return Unit{};
                }

                return maybe_this_port.second.error();
            }
            case OverlayPortKind::Directory:
            case OverlayPortKind::Builtin:
            {
                auto maybe_subdirectories = fs.try_get_directories_non_recursive(m_directory);
                if (auto subdirectories = maybe_subdirectories.get())
                {
                    std::vector<LocalizedString> errors;
                    Util::sort(*subdirectories);
                    auto first_loaded = m_loaded_ports.begin();
                    const auto last_loaded = m_loaded_ports.end();
                    auto first_out = out.begin();
                    const auto last_out = out.end();
                    for (const auto& full_subdirectory : *subdirectories)
                    {
                        auto subdirectory = full_subdirectory.filename();
                        while (first_out != last_out && first_out->first < subdirectory)
                        {
                            ++first_out;
                        }

                        if (first_out != last_out && first_out->first == subdirectory)
                        {
                            // this subdirectory is already in the output; we shouldn't replace or attempt to load it
                            ++first_out;
                            continue;
                        }

                        while (first_loaded != last_loaded && first_loaded->first < subdirectory)
                        {
                            ++first_loaded;
                        }

                        if (first_loaded == last_loaded || first_loaded->first != subdirectory)
                        {
                            // the subdirectory isn't cached, load it into the cache
                            first_loaded = try_load_port_subdirectory_uncached(first_loaded, fs, subdirectory);
                        } // else: the subdirectory is already loaded

                        if (auto this_port = first_loaded->second.get())
                        {
                            first_out = out.emplace_hint(first_out, first_loaded->first, this_port);
                            ++first_out;
                        }
                        else
                        {
                            errors.push_back(first_loaded->second.error());
                        }

                        ++first_loaded;
                    }

                    if (errors.empty())
                    {
                        return Unit{};
                    }

                    return LocalizedString::from_raw(Strings::join("\n", errors));
                }

                return maybe_subdirectories.error();
            }
            case OverlayPortKind::Unknown:
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }
    }

    void OverlayPortIndexEntry::check_directory(const ReadOnlyFilesystem& fs) const
    {
        Debug::println("Using overlay: ", m_directory);

        Checks::msg_check_exit(VCPKG_LINE_INFO,
                               vcpkg::is_directory(fs.status(m_directory, VCPKG_LINE_INFO)),
                               msgOverlayPatchDir,
                               msg::path = m_directory);
    }

    struct OverlayPortIndex
    {
        OverlayPortIndex() = delete;
        OverlayPortIndex(const OverlayPortIndex&) = delete;
        OverlayPortIndex(OverlayPortIndex&&) = default;

        OverlayPortIndex(const OverlayPortPaths& paths)
        {
            for (auto&& overlay_port : paths.overlay_ports)
            {
                m_entries.emplace_back(OverlayPortKind::Unknown, overlay_port);
            }

            if (auto builtin_overlay_port_dir = paths.builtin_overlay_port_dir.get())
            {
                m_entries.emplace_back(OverlayPortKind::Builtin, *builtin_overlay_port_dir);
            }
        }

        const ExpectedL<SourceControlFileAndLocation>* try_load_port(const ReadOnlyFilesystem& fs, StringView port_name)
        {
            for (auto&& entry : m_entries)
            {
                auto result = entry.try_load_port(fs, port_name);
                if (result)
                {
                    return result;
                }
            }

            return nullptr;
        }

        ExpectedL<Unit> try_load_all_ports(const ReadOnlyFilesystem& fs,
                                           std::map<std::string, const SourceControlFileAndLocation*>& out)
        {
            for (auto&& entry : m_entries)
            {
                auto result = entry.try_load_all_ports(fs, out);
                if (!result)
                {
                    return result;
                }
            }

            return Unit{};
        }

        void check_directories(const ReadOnlyFilesystem& fs)
        {
            for (auto&& overlay : m_entries)
            {
                overlay.check_directory(fs);
            }
        }

    private:
        std::vector<OverlayPortIndexEntry> m_entries;
    };

    bool OverlayPortPaths::empty() const noexcept
    {
        return !builtin_overlay_port_dir.has_value() && overlay_ports.empty();
    }

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
            OverlayProviderImpl(const ReadOnlyFilesystem& fs, const OverlayPortPaths& overlay_port_paths)
                : m_fs(fs), m_overlay_index(overlay_port_paths)
            {
                m_overlay_index.check_directories(m_fs);
            }

            OverlayProviderImpl(const OverlayProviderImpl&) = delete;
            OverlayProviderImpl& operator=(const OverlayProviderImpl&) = delete;
            virtual Optional<const SourceControlFileAndLocation&> get_control_file(StringView port_name) const override
            {
                auto loaded = m_overlay_index.try_load_port(m_fs, port_name);
                if (loaded)
                {
                    return loaded->value_or_exit(VCPKG_LINE_INFO);
                }

                return nullopt;
            }

            virtual void load_all_control_files(
                std::map<std::string, const SourceControlFileAndLocation*>& out) const override
            {
                m_overlay_index.try_load_all_ports(m_fs, out).value_or_exit(VCPKG_LINE_INFO);
            }

        private:
            const ReadOnlyFilesystem& m_fs;
            mutable OverlayPortIndex m_overlay_index;
        };

        struct ManifestProviderImpl : IFullOverlayProvider
        {
            ManifestProviderImpl(const ReadOnlyFilesystem& fs,
                                 const OverlayPortPaths& overlay_ports,
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

    std::unique_ptr<IFullOverlayProvider> make_overlay_provider(const ReadOnlyFilesystem& fs,
                                                                const OverlayPortPaths& overlay_ports)
    {
        return std::make_unique<OverlayProviderImpl>(fs, overlay_ports);
    }

    std::unique_ptr<IOverlayProvider> make_manifest_provider(const ReadOnlyFilesystem& fs,
                                                             const OverlayPortPaths& overlay_ports,
                                                             const Path& manifest_path,
                                                             std::unique_ptr<SourceControlFile>&& manifest_scf)
    {
        return std::make_unique<ManifestProviderImpl>(fs, overlay_ports, manifest_path, std::move(manifest_scf));
    }

} // namespace vcpkg
