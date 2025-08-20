#include <vcpkg/base/cache.h>
#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/delayed-init.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/git.h>
#include <vcpkg/base/json.h>
#include <vcpkg/base/jsonreader.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/util.h>

#include <vcpkg/documentation.h>
#include <vcpkg/metrics.h>
#include <vcpkg/paragraphs.h>
#include <vcpkg/registries-parsing.h>
#include <vcpkg/sourceparagraph.h>
#include <vcpkg/vcpkgpaths.h>
#include <vcpkg/versiondeserializers.h>
#include <vcpkg/versions.h>

#include <algorithm>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace
{
    using namespace vcpkg;

    struct GitTreeStringDeserializer : Json::StringDeserializer
    {
        LocalizedString type_name() const override { return msg::format(msgAGitObjectSha); }

        static const GitTreeStringDeserializer instance;
    };
    const GitTreeStringDeserializer GitTreeStringDeserializer::instance;

    struct RegistryPathStringDeserializer : Json::StringDeserializer
    {
        LocalizedString type_name() const override { return msg::format(msgARegistryPath); }

        static const RegistryPathStringDeserializer instance;
    };

    const RegistryPathStringDeserializer RegistryPathStringDeserializer::instance;

    using Baseline = std::map<std::string, Version, std::less<>>;

    struct GitRegistry;

    struct GitRegistryEntry final : RegistryEntry
    {
        GitRegistryEntry(StringView port_name,
                         const GitRegistry& parent,
                         bool stale,
                         std::vector<GitVersionDbEntry>&& version_entries);

        ExpectedL<SourceControlFileAndLocation> try_load_port(const Version& version) const override;

    private:
        ExpectedL<Unit> ensure_not_stale() const;

        std::string port_name;

        const GitRegistry& parent;

        // Indicates whether port_versions and git_trees were filled in with stale (i.e. lock) data.
        mutable bool stale;

        mutable std::vector<GitVersionDbEntry> last_loaded;
    };

    struct GitRegistry final : RegistryImplementation
    {
        GitRegistry(const VcpkgPaths& paths, std::string&& repo, std::string&& reference, std::string&& baseline)
            : m_paths(paths)
            , m_repo(std::move(repo))
            , m_reference(std::move(reference))
            , m_baseline_identifier(std::move(baseline))
        {
        }

        StringLiteral kind() const override { return "git"; }

        ExpectedL<std::unique_ptr<RegistryEntry>> get_port_entry(StringView) const override;

        ExpectedL<Unit> append_all_port_names(std::vector<std::string>&) const override;

        ExpectedL<bool> try_append_all_port_names_no_network(std::vector<std::string>& port_names) const override;

        ExpectedL<Optional<Version>> get_baseline_version(StringView) const override;

    private:
        friend GitRegistryEntry;

        const ExpectedL<LockFile::Entry>& get_lock_entry() const
        {
            return m_lock_entry.get(
                [this]() { return m_paths.get_installed_lockfile().get_or_fetch(m_paths, m_repo, m_reference); });
        }

        const ExpectedL<Path>& get_versions_tree_path() const
        {
            return m_versions_tree.get([this]() -> ExpectedL<Path> {
                auto& maybe_lock_entry = get_lock_entry();
                auto lock_entry = maybe_lock_entry.get();
                if (!lock_entry)
                {
                    return maybe_lock_entry.error();
                }

                auto maybe_up_to_date = lock_entry->ensure_up_to_date(m_paths);
                if (!maybe_up_to_date)
                {
                    return std::move(maybe_up_to_date).error();
                }

                auto maybe_tree = m_paths.git_find_object_id_for_remote_registry_path(lock_entry->commit_id(),
                                                                                      FileVersions.to_string());
                auto tree = maybe_tree.get();
                if (!tree)
                {
                    get_global_metrics_collector().track_define(DefineMetric::RegistriesErrorNoVersionsAtCommit);
                    return msg::format_error(msgCouldNotFindGitTreeAtCommit,
                                             msg::package_name = m_repo,
                                             msg::commit_sha = lock_entry->commit_id())
                        .append_raw('\n')
                        .append_raw(maybe_tree.error());
                }

                auto maybe_path = m_paths.git_extract_tree_from_remote_registry(*tree);
                auto path = maybe_path.get();
                if (!path)
                {
                    return msg::format_error(msgFailedToCheckoutRepo, msg::package_name = m_repo)
                        .append_raw('\n')
                        .append(maybe_path.error());
                }

                return std::move(*path);
            });
        }

        struct VersionsTreePathResult
        {
            Path p;
            bool stale;
        };

        ExpectedL<VersionsTreePathResult> get_unstale_stale_versions_tree_path() const
        {
            auto& maybe_versions_tree = get_versions_tree_path();
            if (auto versions_tree = maybe_versions_tree.get())
            {
                return VersionsTreePathResult{*versions_tree, false};
            }

            return maybe_versions_tree.error();
        }

        ExpectedL<VersionsTreePathResult> get_stale_versions_tree_path() const
        {
            const auto& maybe_entry = get_lock_entry();
            auto entry = maybe_entry.get();
            if (!entry)
            {
                return maybe_entry.error();
            }

            if (!entry->stale())
            {
                return get_unstale_stale_versions_tree_path();
            }

            if (!m_stale_versions_tree.has_value())
            {
                auto maybe_tree =
                    m_paths.git_find_object_id_for_remote_registry_path(entry->commit_id(), FileVersions.to_string());
                auto tree = maybe_tree.get();
                if (!tree)
                {
                    // This could be caused by git gc or otherwise -- fall back to full fetch
                    return get_unstale_stale_versions_tree_path();
                }

                auto maybe_path = m_paths.git_extract_tree_from_remote_registry(*tree);
                auto path = maybe_path.get();
                if (!path)
                {
                    // This could be caused by git gc or otherwise -- fall back to full fetch
                    return get_unstale_stale_versions_tree_path();
                }

                m_stale_versions_tree = std::move(*path);
            }

            return VersionsTreePathResult{m_stale_versions_tree.value_or_exit(VCPKG_LINE_INFO), true};
        }

        const VcpkgPaths& m_paths;

        std::string m_repo;
        std::string m_reference;
        std::string m_baseline_identifier;
        DelayedInit<ExpectedL<LockFile::Entry>> m_lock_entry;
        mutable Optional<Path> m_stale_versions_tree;
        DelayedInit<ExpectedL<Path>> m_versions_tree;
        DelayedInit<ExpectedL<Baseline>> m_baseline;
    };

    struct BuiltinPortTreeRegistryEntry final : RegistryEntry
    {
        BuiltinPortTreeRegistryEntry(const SourceControlFileAndLocation& load_result_) : load_result(load_result_) { }

        ExpectedL<SourceControlFileAndLocation> try_load_port(const Version& v) const override
        {
            auto& core_paragraph = load_result.source_control_file->core_paragraph;
            if (v == core_paragraph->version)
            {
                return load_result.clone();
            }

            return msg::format_error(msgVersionBuiltinPortTreeEntryMissing,
                                     msg::package_name = core_paragraph->name,
                                     msg::expected = v.to_string(),
                                     msg::actual = core_paragraph->version.to_string());
        }

        const SourceControlFileAndLocation& load_result;
    };

    struct BuiltinGitRegistryEntry final : RegistryEntry
    {
        BuiltinGitRegistryEntry(const VcpkgPaths& paths) : m_paths(paths) { }

        ExpectedL<SourceControlFileAndLocation> try_load_port(const Version& version) const override;

        const VcpkgPaths& m_paths;

        std::string port_name;

        std::vector<GitVersionDbEntry> port_version_entries;
    };

    struct FilesystemRegistryEntry final : RegistryEntry
    {
        explicit FilesystemRegistryEntry(const ReadOnlyFilesystem& fs,
                                         StringView port_name,
                                         std::vector<FilesystemVersionDbEntry>&& version_entries)
            : fs(fs), port_name(port_name.data(), port_name.size()), version_entries(std::move(version_entries))
        {
        }

        ExpectedL<SourceControlFileAndLocation> try_load_port(const Version& version) const override;

        const ReadOnlyFilesystem& fs;
        std::string port_name;
        std::vector<FilesystemVersionDbEntry> version_entries;
    };

    // This registry implementation is the builtin registry without a baseline
    // that will only consult files in ports
    struct BuiltinFilesRegistry final : RegistryImplementation
    {
        BuiltinFilesRegistry(const VcpkgPaths& paths)
            : m_fs(paths.get_filesystem()), m_builtin_ports_directory(paths.builtin_ports_directory())
        {
        }

        StringLiteral kind() const override { return JsonIdBuiltinFiles; }

        ExpectedL<std::unique_ptr<RegistryEntry>> get_port_entry(StringView port_name) const override;

        ExpectedL<Unit> append_all_port_names(std::vector<std::string>&) const override;

        ExpectedL<bool> try_append_all_port_names_no_network(std::vector<std::string>& port_names) const override;

        ExpectedL<Optional<Version>> get_baseline_version(StringView port_name) const override;

        ~BuiltinFilesRegistry() = default;

        DelayedInit<Baseline> m_baseline;

    private:
        const ExpectedL<SourceControlFileAndLocation>& get_scfl(StringView port_name) const
        {
            auto path = m_builtin_ports_directory / port_name;
            return m_scfls.get_lazy(path, [&, this]() {
                return Paragraphs::try_load_builtin_port_required(m_fs, port_name, m_builtin_ports_directory)
                    .maybe_scfl;
            });
        }

        const ReadOnlyFilesystem& m_fs;
        const Path m_builtin_ports_directory;
        Cache<Path, ExpectedL<SourceControlFileAndLocation>> m_scfls;
    };

    // This registry implementation is a builtin registry with a provided
    // baseline that will perform git operations on the root git repo
    struct BuiltinGitRegistry final : RegistryImplementation
    {
        BuiltinGitRegistry(const VcpkgPaths& paths, std::string&& baseline)
            : m_baseline_identifier(std::move(baseline))
            , m_files_impl(std::make_unique<BuiltinFilesRegistry>(paths))
            , m_paths(paths)
        {
        }

        StringLiteral kind() const override { return JsonIdBuiltinGit; }

        ExpectedL<std::unique_ptr<RegistryEntry>> get_port_entry(StringView port_name) const override;

        ExpectedL<Unit> append_all_port_names(std::vector<std::string>&) const override;

        ExpectedL<bool> try_append_all_port_names_no_network(std::vector<std::string>& port_names) const override;

        ExpectedL<Optional<Version>> get_baseline_version(StringView port_name) const override;

        ~BuiltinGitRegistry() = default;

        std::string m_baseline_identifier;
        DelayedInit<ExpectedL<Baseline>> m_baseline;

    private:
        std::unique_ptr<BuiltinFilesRegistry> m_files_impl;

        const VcpkgPaths& m_paths;
    };

    // This registry entry is a stub that fails on all APIs; this is used in
    // read-only vcpkg if the user has not provided a baseline.
    struct BuiltinErrorRegistry final : RegistryImplementation
    {
        StringLiteral kind() const override { return JsonIdBuiltinError; }

        ExpectedL<std::unique_ptr<RegistryEntry>> get_port_entry(StringView) const override
        {
            return msg::format_error(msgErrorRequireBaseline);
        }

        ExpectedL<Unit> append_all_port_names(std::vector<std::string>&) const override
        {
            return msg::format_error(msgErrorRequireBaseline);
        }

        ExpectedL<bool> try_append_all_port_names_no_network(std::vector<std::string>&) const override
        {
            return msg::format_error(msgErrorRequireBaseline);
        }

        ExpectedL<Optional<Version>> get_baseline_version(StringView) const override
        {
            return msg::format_error(msgErrorRequireBaseline);
        }

        ~BuiltinErrorRegistry() = default;
    };

    struct FilesystemRegistry final : RegistryImplementation
    {
        FilesystemRegistry(const ReadOnlyFilesystem& fs, Path&& path, std::string&& baseline)
            : m_fs(fs), m_path(std::move(path)), m_baseline_identifier(std::move(baseline))
        {
        }

        StringLiteral kind() const override { return JsonIdFilesystem; }

        ExpectedL<std::unique_ptr<RegistryEntry>> get_port_entry(StringView) const override;

        ExpectedL<Unit> append_all_port_names(std::vector<std::string>&) const override;

        ExpectedL<bool> try_append_all_port_names_no_network(std::vector<std::string>& port_names) const override;

        ExpectedL<Optional<Version>> get_baseline_version(StringView) const override;

    private:
        const ReadOnlyFilesystem& m_fs;

        Path m_path;
        std::string m_baseline_identifier;
        DelayedInit<ExpectedL<Baseline>> m_baseline;
    };

    Path relative_path_to_versions(StringView port_name);

    // returns nullopt if the baseline is valid, but doesn't contain the specified baseline,
    // or (equivalently) if the baseline does not exist.
    ExpectedL<Baseline> parse_baseline_versions(StringView contents, StringView baseline, StringView origin);
    ExpectedL<Baseline> load_baseline_versions(const ReadOnlyFilesystem& fs,
                                               const Path& baseline_path,
                                               StringView identifier = {});

    ExpectedL<Unit> load_all_port_names_from_registry_versions(std::vector<std::string>& out,
                                                               const ReadOnlyFilesystem& fs,
                                                               const Path& registry_versions)
    {
        auto maybe_super_directories = fs.try_get_directories_non_recursive(registry_versions);
        const auto super_directories = maybe_super_directories.get();
        if (!super_directories)
        {
            return std::move(maybe_super_directories).error();
        }

        for (auto&& super_directory : *super_directories)
        {
            auto maybe_files = fs.try_get_regular_files_non_recursive(super_directory);
            const auto files = maybe_files.get();
            if (!files)
            {
                return std::move(maybe_files).error();
            }

            for (auto&& file : *files)
            {
                auto filename = file.filename();
                if (!Strings::case_insensitive_ascii_ends_with(filename, ".json")) continue;

                if (!filename.ends_with(".json"))
                {
                    return msg::format_error(msgJsonFileMissingExtension, msg::path = file);
                }

                auto port_name = filename.substr(0, filename.size() - 5);
                if (!Json::IdentifierDeserializer::is_ident(port_name))
                {
                    return msg::format_error(msgInvalidPortVersonName, msg::path = file);
                }

                out.push_back(port_name.to_string());
            }
        }

        return Unit{};
    }

    static ExpectedL<Path> git_checkout_baseline(const VcpkgPaths& paths, StringView commit_sha)
    {
        const Filesystem& fs = paths.get_filesystem();
        const auto destination_parent = paths.baselines_output() / commit_sha;
        auto destination = destination_parent / FileBaselineDotJson;
        if (!fs.exists(destination, IgnoreErrors{}))
        {
            const auto destination_tmp = destination_parent / "baseline.json.tmp";
            auto treeish = Strings::concat(commit_sha, ":versions/baseline.json");
            auto maybe_contents =
                paths.versions_dot_git_dir().then([&](Path&& dot_git) { return paths.git_show(treeish, dot_git); });

            if (auto contents = maybe_contents.get())
            {
                std::error_code ec;
                fs.create_directories(destination_parent, ec);
                if (ec)
                {
                    return {error_prefix()
                                .append(format_filesystem_call_error(ec, "create_directories", {destination_parent}))
                                .append_raw('\n')
                                .append_raw(NotePrefix)
                                .append(msgWhileCheckingOutBaseline, msg::commit_sha = commit_sha),
                            expected_right_tag};
                }
                fs.write_contents(destination_tmp, *contents, ec);
                if (ec)
                {
                    return {error_prefix()
                                .append(format_filesystem_call_error(ec, "write_contents", {destination_tmp}))
                                .append_raw('\n')
                                .append_raw(NotePrefix)
                                .append(msgWhileCheckingOutBaseline, msg::commit_sha = commit_sha),
                            expected_right_tag};
                }
                fs.rename(destination_tmp, destination, ec);
                if (ec)
                {
                    return {error_prefix()
                                .append(format_filesystem_call_error(ec, "rename", {destination_tmp, destination}))
                                .append_raw('\n')
                                .append_raw(NotePrefix)
                                .append(msgWhileCheckingOutBaseline, msg::commit_sha = commit_sha),
                            expected_right_tag};
                }
            }
            else
            {
                return {msg::format_error(msgBaselineGitShowFailed, msg::commit_sha = commit_sha)
                            .append_raw('\n')
                            .append(maybe_contents.error()),
                        expected_right_tag};
            }
        }

        return destination;
    }

    // { RegistryImplementation

    // { BuiltinFilesRegistry::RegistryImplementation
    ExpectedL<std::unique_ptr<RegistryEntry>> BuiltinFilesRegistry::get_port_entry(StringView port_name) const
    {
        return get_scfl(port_name).then(
            [&](const SourceControlFileAndLocation& scfl) -> ExpectedL<std::unique_ptr<RegistryEntry>> {
                auto scf = scfl.source_control_file.get();
                if (!scf)
                {
                    return std::unique_ptr<RegistryEntry>();
                }

                if (scf->core_paragraph->name == port_name)
                {
                    return std::make_unique<BuiltinPortTreeRegistryEntry>(scfl);
                }

                return msg::format_error(msgUnexpectedPortName,
                                         msg::expected = scf->core_paragraph->name,
                                         msg::actual = port_name,
                                         msg::path = scfl.port_directory());
            });
    }

    ExpectedL<Optional<Version>> BuiltinFilesRegistry::get_baseline_version(StringView port_name) const
    {
        // if a baseline is not specified, use the ports directory version
        return get_scfl(port_name).then([&](const SourceControlFileAndLocation& scfl) -> ExpectedL<Optional<Version>> {
            auto scf = scfl.source_control_file.get();
            if (!scf)
            {
                return Optional<Version>();
            }

            return scf->to_version();
        });
    }

    ExpectedL<Unit> BuiltinFilesRegistry::append_all_port_names(std::vector<std::string>& out) const
    {
        auto maybe_port_directories = m_fs.try_get_directories_non_recursive(m_builtin_ports_directory);
        if (auto port_directories = maybe_port_directories.get())
        {
            for (auto&& port_directory : *port_directories)
            {
                auto filename = port_directory.filename();
                if (filename == FileDotDsStore) continue;
                out.emplace_back(filename.data(), filename.size());
            }

            return Unit{};
        }

        return std::move(maybe_port_directories).error();
    }

    ExpectedL<bool> BuiltinFilesRegistry::try_append_all_port_names_no_network(
        std::vector<std::string>& port_names) const
    {
        return append_all_port_names(port_names).map([](Unit) { return true; });
    }
    // } BuiltinFilesRegistry::RegistryImplementation

    // { BuiltinGitRegistry::RegistryImplementation
    ExpectedL<std::unique_ptr<RegistryEntry>> BuiltinGitRegistry::get_port_entry(StringView port_name) const
    {
        const auto& fs = m_paths.get_filesystem();

        auto versions_path = m_paths.builtin_registry_versions / relative_path_to_versions(port_name);
        return load_git_versions_file(fs, m_paths.builtin_registry_versions, port_name)
            .entries.then([this, &port_name](Optional<std::vector<GitVersionDbEntry>>&& maybe_version_entries)
                              -> ExpectedL<std::unique_ptr<RegistryEntry>> {
                auto version_entries = maybe_version_entries.get();
                if (!version_entries)
                {
                    return m_files_impl->get_port_entry(port_name);
                }

                auto res = std::make_unique<BuiltinGitRegistryEntry>(m_paths);
                res->port_name.assign(port_name.data(), port_name.size());
                res->port_version_entries = std::move(*version_entries);
                return res;
            });
    }

    ExpectedL<Optional<Version>> lookup_in_maybe_baseline(const ExpectedL<Baseline>& maybe_baseline,
                                                          StringView port_name)
    {
        auto baseline = maybe_baseline.get();
        if (!baseline)
        {
            return LocalizedString(maybe_baseline.error())
                .append_raw('\n')
                .append(msgWhileLoadingBaselineVersionForPort, msg::package_name = port_name);
        }

        auto it = baseline->find(port_name);
        if (it != baseline->end())
        {
            return it->second;
        }

        return Optional<Version>();
    }

    ExpectedL<Optional<Version>> BuiltinGitRegistry::get_baseline_version(StringView port_name) const
    {
        return lookup_in_maybe_baseline(m_baseline.get([this]() -> ExpectedL<Baseline> {
            return git_checkout_baseline(m_paths, m_baseline_identifier)
                .then([&](Path&& path) { return load_baseline_versions(m_paths.get_filesystem(), path); })
                .map_error([&](LocalizedString&& error) {
                    return std::move(error).append(msgWhileCheckingOutBaseline,
                                                   msg::commit_sha = m_baseline_identifier);
                });
        }),
                                        port_name);
    }

    ExpectedL<Unit> BuiltinGitRegistry::append_all_port_names(std::vector<std::string>& out) const
    {
        const auto& fs = m_paths.get_filesystem();

        if (fs.exists(m_paths.builtin_registry_versions, IgnoreErrors{}))
        {
            return load_all_port_names_from_registry_versions(out, fs, m_paths.builtin_registry_versions);
        }
        else
        {
            return m_files_impl->append_all_port_names(out);
        }
    }

    ExpectedL<bool> BuiltinGitRegistry::try_append_all_port_names_no_network(std::vector<std::string>& port_names) const
    {
        return append_all_port_names(port_names).map([](Unit) { return true; });
    }
    // } BuiltinGitRegistry::RegistryImplementation

    // { FilesystemRegistry::RegistryImplementation
    ExpectedL<Optional<Version>> FilesystemRegistry::get_baseline_version(StringView port_name) const
    {
        return lookup_in_maybe_baseline(m_baseline.get([this]() {
            return load_baseline_versions(m_fs, m_path / FileVersions / FileBaselineDotJson, m_baseline_identifier);
        }),
                                        port_name);
    }

    ExpectedL<std::unique_ptr<RegistryEntry>> FilesystemRegistry::get_port_entry(StringView port_name) const
    {
        return load_filesystem_versions_file(m_fs, m_path / FileVersions, port_name, m_path)
            .then([&](Optional<std::vector<FilesystemVersionDbEntry>>&& maybe_version_entries)
                      -> ExpectedL<std::unique_ptr<RegistryEntry>> {
                auto version_entries = maybe_version_entries.get();
                if (!version_entries)
                {
                    return std::unique_ptr<RegistryEntry>{};
                }

                return std::make_unique<FilesystemRegistryEntry>(m_fs, port_name, std::move(*version_entries));
            });
    }

    ExpectedL<Unit> FilesystemRegistry::append_all_port_names(std::vector<std::string>& out) const
    {
        return load_all_port_names_from_registry_versions(out, m_fs, m_path / FileVersions);
    }

    ExpectedL<bool> FilesystemRegistry::try_append_all_port_names_no_network(std::vector<std::string>& port_names) const
    {
        return append_all_port_names(port_names).map([](Unit) { return true; });
    }
    // } FilesystemRegistry::RegistryImplementation

    // { GitRegistry::RegistryImplementation
    ExpectedL<std::unique_ptr<RegistryEntry>> GitRegistry::get_port_entry(StringView port_name) const
    {
        auto maybe_stale_vtp = get_stale_versions_tree_path();
        auto stale_vtp = maybe_stale_vtp.get();
        if (!stale_vtp)
        {
            return std::move(maybe_stale_vtp).error();
        }

        {
            // try to load using "stale" version database
            auto maybe_maybe_version_entries =
                load_git_versions_file(m_paths.get_filesystem(), stale_vtp->p, port_name).entries;
            auto maybe_version_entries = maybe_maybe_version_entries.get();
            if (!maybe_version_entries)
            {
                return std::move(maybe_maybe_version_entries).error();
            }

            auto version_entries = maybe_version_entries->get();
            if (version_entries)
            {
                return std::make_unique<GitRegistryEntry>(
                    port_name, *this, stale_vtp->stale, std::move(*version_entries));
            }
        }

        if (!stale_vtp->stale)
        {
            // data is already live but we don't know of this port
            return std::unique_ptr<RegistryEntry>();
        }

        return get_versions_tree_path().then([this, &port_name](const Path& live_vcb) {
            return load_git_versions_file(m_paths.get_filesystem(), live_vcb, port_name)
                .entries.then([this, &port_name](Optional<std::vector<GitVersionDbEntry>>&& maybe_version_entries)
                                  -> ExpectedL<std::unique_ptr<RegistryEntry>> {
                    auto version_entries = maybe_version_entries.get();
                    if (!version_entries)
                    {
                        // data is already live but we don't know of this port
                        return std::unique_ptr<RegistryEntry>();
                    }

                    return std::make_unique<GitRegistryEntry>(port_name, *this, false, std::move(*version_entries));
                });
        });
    }

    GitRegistryEntry::GitRegistryEntry(StringView port_name,
                                       const GitRegistry& parent,
                                       bool stale,
                                       std::vector<GitVersionDbEntry>&& version_entries)
        : port_name(port_name.data(), port_name.size())
        , parent(parent)
        , stale(stale)
        , last_loaded(std::move(version_entries))
    {
    }

    ExpectedL<Optional<Version>> GitRegistry::get_baseline_version(StringView port_name) const
    {
        return lookup_in_maybe_baseline(m_baseline.get([this, port_name]() -> ExpectedL<Baseline> {
            // We delay baseline validation until here to give better error messages and suggestions
            if (!is_git_sha(m_baseline_identifier))
            {
                auto& maybe_lock_entry = get_lock_entry();
                auto lock_entry = maybe_lock_entry.get();
                if (!lock_entry)
                {
                    return maybe_lock_entry.error();
                }

                auto maybe_up_to_date = lock_entry->ensure_up_to_date(m_paths);
                if (maybe_up_to_date)
                {
                    return msg::format_error(
                        msgGitRegistryMustHaveBaseline, msg::url = m_repo, msg::commit_sha = lock_entry->commit_id());
                }

                return std::move(maybe_up_to_date).error();
            }

            auto path_to_baseline = Path(FileVersions) / FileBaselineDotJson;
            auto maybe_contents = m_paths.git_show_from_remote_registry(m_baseline_identifier, path_to_baseline);
            if (!maybe_contents)
            {
                auto& maybe_lock_entry = get_lock_entry();
                auto lock_entry = maybe_lock_entry.get();
                if (!lock_entry)
                {
                    return maybe_lock_entry.error();
                }

                auto maybe_up_to_date = lock_entry->ensure_up_to_date(m_paths);
                if (!maybe_up_to_date)
                {
                    return std::move(maybe_up_to_date).error();
                }

                maybe_contents = m_paths.git_show_from_remote_registry(m_baseline_identifier, path_to_baseline);
            }

            if (!maybe_contents)
            {
                msg::println(msgFetchingBaselineInfo, msg::package_name = m_repo);
                auto maybe_err = m_paths.git_fetch(m_repo, m_baseline_identifier);
                if (!maybe_err)
                {
                    get_global_metrics_collector().track_define(DefineMetric::RegistriesErrorCouldNotFindBaseline);
                    return msg::format_error(msgFailedToFetchRepo, msg::url = m_repo)
                        .append_raw('\n')
                        .append(maybe_err.error());
                }

                maybe_contents = m_paths.git_show_from_remote_registry(m_baseline_identifier, path_to_baseline);
            }

            if (!maybe_contents)
            {
                get_global_metrics_collector().track_define(DefineMetric::RegistriesErrorCouldNotFindBaseline);
                return msg::format_error(msgCouldNotFindBaselineInCommit,
                                         msg::url = m_repo,
                                         msg::commit_sha = m_baseline_identifier,
                                         msg::package_name = port_name)
                    .append_raw('\n')
                    .append_raw(maybe_contents.error());
            }

            auto contents = maybe_contents.get();
            return parse_baseline_versions(*contents, JsonIdDefault, path_to_baseline)
                .map_error([&](LocalizedString&& error) {
                    get_global_metrics_collector().track_define(DefineMetric::RegistriesErrorCouldNotFindBaseline);
                    return msg::format_error(msgErrorWhileFetchingBaseline,
                                             msg::value = m_baseline_identifier,
                                             msg::package_name = m_repo)
                        .append_raw('\n')
                        .append(error);
                });
        }),
                                        port_name);
    }

    ExpectedL<Unit> GitRegistry::append_all_port_names(std::vector<std::string>& out) const
    {
        auto maybe_versions_path = get_stale_versions_tree_path();
        if (auto versions_path = maybe_versions_path.get())
        {
            return load_all_port_names_from_registry_versions(out, m_paths.get_filesystem(), versions_path->p);
        }

        return std::move(maybe_versions_path).error();
    }

    ExpectedL<bool> GitRegistry::try_append_all_port_names_no_network(std::vector<std::string>&) const
    {
        // At this time we don't record enough information to know what the last fetch for a registry is,
        // so we can't even return what the most recent answer was.
        //
        // This would be fixable if we recorded LockFile in the registries cache.
        return false;
    }
    // } GitRegistry::RegistryImplementation

    // } RegistryImplementation

    LocalizedString format_version_git_entry_missing(StringView port_name,
                                                     const Version& expected_version,
                                                     const std::vector<GitVersionDbEntry>& version_entries)
    {
        auto error_msg =
            msg::format_error(msgVersionGitEntryMissing, msg::package_name = port_name, msg::version = expected_version)
                .append_raw('\n');
        for (auto&& version_entry : version_entries)
        {
            error_msg.append_indent().append_raw(version_entry.version.version.to_string()).append_raw('\n');
        }

        error_msg.append(msgVersionIncomparable4, msg::url = docs::versioning_url).append_raw('\n');
        error_msg.append(msgSeeURL, msg::url = docs::troubleshoot_versioning_url);
        return error_msg;
    }

    // { RegistryEntry

    // { BuiltinRegistryEntry::RegistryEntry
    ExpectedL<SourceControlFileAndLocation> BuiltinGitRegistryEntry::try_load_port(const Version& version) const
    {
        auto it =
            std::find_if(port_version_entries.begin(),
                         port_version_entries.end(),
                         [&](const GitVersionDbEntry& entry) noexcept { return entry.version.version == version; });
        if (it == port_version_entries.end())
        {
            return format_version_git_entry_missing(port_name, version, port_version_entries)
                .append_raw('\n')
                .append_raw(NotePrefix)
                .append(msgChecksUpdateVcpkg);
        }

        return m_paths.versions_dot_git_dir()
            .then([&, this](Path&& dot_git) {
                return m_paths.git_checkout_port(port_name, it->git_tree, dot_git).map_error([](LocalizedString&& err) {
                    return std::move(err)
                        .append_raw('\n')
                        .append_raw(NotePrefix)
                        .append(msgSeeURL, msg::url = docs::troubleshoot_versioning_url);
                });
            })
            .then([this, &it](Path&& p) -> ExpectedL<SourceControlFileAndLocation> {
                return Paragraphs::try_load_port_required(
                           m_paths.get_filesystem(),
                           port_name,
                           PortLocation{std::move(p),
                                        Paragraphs::builtin_git_tree_spdx_location(it->git_tree),
                                        PortSourceKind::Builtin})
                    .maybe_scfl;
            });
    }
    // } BuiltinRegistryEntry::RegistryEntry

    // { FilesystemRegistryEntry::RegistryEntry
    ExpectedL<SourceControlFileAndLocation> FilesystemRegistryEntry::try_load_port(const Version& version) const
    {
        auto it = std::find_if(
            version_entries.begin(), version_entries.end(), [&](const FilesystemVersionDbEntry& entry) noexcept {
                return entry.version.version == version;
            });
        if (it == version_entries.end())
        {
            return msg::format_error(
                msgVersionDatabaseEntryMissing, msg::package_name = port_name, msg::version = version);
        }

        return Paragraphs::try_load_port_required(
                   fs, port_name, PortLocation{it->p, no_assertion, PortSourceKind::Filesystem})
            .maybe_scfl;
    }
    // } FilesystemRegistryEntry::RegistryEntry

    // { GitRegistryEntry::RegistryEntry
    ExpectedL<Unit> GitRegistryEntry::ensure_not_stale() const
    {
        if (stale)
        {
            auto maybe_live_vdb = parent.get_versions_tree_path();
            auto live_vdb = maybe_live_vdb.get();
            if (!live_vdb)
            {
                return std::move(maybe_live_vdb).error();
            }

            auto maybe_maybe_version_entries =
                load_git_versions_file(parent.m_paths.get_filesystem(), *live_vdb, port_name).entries;
            auto maybe_version_entries = maybe_maybe_version_entries.get();
            if (!maybe_version_entries)
            {
                return std::move(maybe_maybe_version_entries).error();
            }

            auto version_entries = maybe_version_entries->get();
            if (!version_entries)
            {
                // Somehow the port existed in the stale version database but doesn't exist in the
                // live one?
                return msg::format_error(msgCouldNotFindVersionDatabaseFile,
                                         msg::path = *live_vdb / relative_path_to_versions(port_name));
            }

            last_loaded = std::move(*version_entries);
            stale = false;
        }

        return Unit{};
    }

    ExpectedL<SourceControlFileAndLocation> GitRegistryEntry::try_load_port(const Version& version) const
    {
        auto match_version = [&](const GitVersionDbEntry& entry) noexcept { return entry.version.version == version; };
        auto it = std::find_if(last_loaded.begin(), last_loaded.end(), match_version);
        if (it == last_loaded.end() && stale)
        {
            // didn't find the version, maybe a newer version database will have it
            auto maybe_not_stale = ensure_not_stale();
            if (!maybe_not_stale)
            {
                return std::move(maybe_not_stale).error();
            }

            it = std::find_if(last_loaded.begin(), last_loaded.end(), match_version);
        }

        if (it == last_loaded.end())
        {
            return format_version_git_entry_missing(port_name, version, last_loaded);
        }

        return parent.m_paths.git_extract_tree_from_remote_registry(it->git_tree)
            .then([this, &it](Path&& p) -> ExpectedL<SourceControlFileAndLocation> {
                return Paragraphs::try_load_port_required(
                           parent.m_paths.get_filesystem(),
                           port_name,
                           PortLocation{p, fmt::format("git+{}@{}", parent.m_repo, it->git_tree), PortSourceKind::Git})
                    .maybe_scfl;
            });
    }

    // } GitRegistryEntry::RegistryEntry

    // } RegistryEntry
}

// deserializers
namespace
{
    using namespace vcpkg;

    struct BaselineDeserializer final : Json::IDeserializer<std::map<std::string, Version, std::less<>>>
    {
        LocalizedString type_name() const override { return msg::format(msgABaselineObject); }

        Optional<type> visit_object(Json::Reader& r, const Json::Object& obj) const override
        {
            std::map<std::string, Version, std::less<>> result;

            for (auto pr : obj)
            {
                const auto& version_value = pr.second;
                Version version;
                r.visit_in_key(version_value, pr.first, version, baseline_version_tag_deserializer);

                result.emplace(pr.first.to_string(), std::move(version));
            }

            return result;
        }

        static const BaselineDeserializer instance;
    };

    const BaselineDeserializer BaselineDeserializer::instance;

    Path relative_path_to_versions(StringView port_name)
    {
        char prefix[] = {port_name[0], '-', '\0'};
        return Path(prefix) / port_name.to_string() + ".json";
    }

    ExpectedL<Baseline> parse_baseline_versions(StringView contents, StringView baseline, StringView origin)
    {
        auto maybe_object = Json::parse_object(contents, origin);
        auto object = maybe_object.get();
        if (!object)
        {
            return std::move(maybe_object).error();
        }

        auto real_baseline = baseline.size() == 0 ? StringView{JsonIdDefault} : baseline;
        auto baseline_value = object->get(real_baseline);
        if (!baseline_value)
        {
            return LocalizedString::from_raw(origin)
                .append_raw(": ")
                .append_raw(ErrorPrefix)
                .append(msgMissingRequiredField,
                        msg::json_field = baseline,
                        msg::json_type = msg::format(msgABaselineObject));
        }

        Json::Reader r(origin);
        Baseline result;
        r.visit_in_key(*baseline_value, real_baseline, result, BaselineDeserializer::instance);
        if (!r.messages().any_errors())
        {
            return std::move(result);
        }

        return msg::format_error(msgFailedToParseBaseline, msg::path = origin)
            .append_raw('\n')
            .append_raw(r.messages().join());
    }

    ExpectedL<Baseline> load_baseline_versions(const ReadOnlyFilesystem& fs,
                                               const Path& baseline_path,
                                               StringView baseline)
    {
        return fs.try_read_contents(baseline_path).then([&](FileContents&& fc) {
            return parse_baseline_versions(fc.content, baseline, fc.origin);
        });
    }
}

namespace vcpkg
{
    ExpectedL<LockFile::Entry> LockFile::get_or_fetch(const VcpkgPaths& paths, StringView repo, StringView reference)
    {
        auto range = lockdata.equal_range(repo);
        auto it = std::find_if(range.first, range.second, [&reference](const LockDataType::value_type& repo2entry) {
            return repo2entry.second.reference == reference;
        });

        if (it == range.second)
        {
            msg::println(msgFetchingRegistryInfo, msg::url = repo, msg::value = reference);
            auto maybe_commit = paths.git_fetch_from_remote_registry(repo, reference);
            if (auto commit = maybe_commit.get())
            {
                it = lockdata.emplace(repo.to_string(), EntryData{reference.to_string(), *commit, false});
                modified = true;
            }
            else
            {
                return std::move(maybe_commit).error();
            }
        }

        return LockFile::Entry{this, it};
    }
    ExpectedL<Unit> LockFile::Entry::ensure_up_to_date(const VcpkgPaths& paths) const
    {
        if (data->second.stale)
        {
            StringView repo(data->first);
            StringView reference(data->second.reference);
            msg::println(msgFetchingRegistryInfo, msg::url = repo, msg::value = reference);

            auto maybe_commit_id = paths.git_fetch_from_remote_registry(repo, reference);
            if (const auto commit_id = maybe_commit_id.get())
            {
                data->second.commit_id = *commit_id;
                data->second.stale = false;
                lockfile->modified = true;
            }
            else
            {
                return std::move(maybe_commit_id).error();
            }
        }

        return Unit{};
    }

    Registry::Registry(std::vector<std::string>&& patterns, std::unique_ptr<RegistryImplementation>&& impl)
        : patterns_(std::move(patterns)), implementation_(std::move(impl))
    {
        Util::sort_unique_erase(patterns_);
        Checks::check_exit(VCPKG_LINE_INFO, implementation_ != nullptr);
    }

    const RegistryImplementation* RegistrySet::registry_for_port(StringView name) const
    {
        auto candidates = registries_for_port(name);
        if (candidates.empty())
        {
            return default_registry();
        }

        return candidates[0];
    }

    size_t package_pattern_match(StringView name, StringView pattern)
    {
        const auto pattern_size = pattern.size();
        const auto maybe_star_index = pattern_size - 1;
        if (pattern_size != 0 && pattern[maybe_star_index] == '*')
        {
            // pattern ends in wildcard
            if (name.size() >= maybe_star_index && std::equal(pattern.begin(), pattern.end() - 1, name.begin()))
            {
                return pattern_size;
            }
        }
        else if (name == pattern)
        {
            // exact match is like matching "infinity" prefix
            return SIZE_MAX;
        }

        return 0;
    }

    std::vector<const RegistryImplementation*> RegistrySet::registries_for_port(StringView name) const
    {
        struct RegistryCandidate
        {
            const RegistryImplementation* impl;
            std::size_t matched_prefix;
        };

        std::vector<RegistryCandidate> candidates;
        for (auto&& registry : registries())
        {
            std::size_t longest_prefix = 0;
            for (auto&& pattern : registry.patterns())
            {
                longest_prefix = std::max(longest_prefix, package_pattern_match(name, pattern));
            }

            if (longest_prefix != 0)
            {
                candidates.push_back({&registry.implementation(), longest_prefix});
            }
        }

        if (candidates.empty())
        {
            return std::vector<const RegistryImplementation*>();
        }

        std::stable_sort(
            candidates.begin(), candidates.end(), [](const RegistryCandidate& lhs, const RegistryCandidate& rhs) {
                return lhs.matched_prefix > rhs.matched_prefix;
            });

        return Util::fmap(std::move(candidates), [](const RegistryCandidate& target) { return target.impl; });
    }

    ExpectedL<Optional<Version>> RegistrySet::baseline_for_port(StringView port_name) const
    {
        auto impl = registry_for_port(port_name);
        if (!impl) return msg::format_error(msgNoRegistryForPort, msg::package_name = port_name);
        return impl->get_baseline_version(port_name);
    }

    bool RegistrySet::is_default_builtin_registry() const
    {
        return default_registry_ && default_registry_->kind() == JsonIdBuiltinFiles;
    }
    bool RegistrySet::has_modifications() const { return !registries_.empty() || !is_default_builtin_registry(); }
} // namespace vcpkg

namespace
{
    void remove_unreachable_port_names_by_patterns(std::vector<std::string>& result,
                                                   std::size_t start_at,
                                                   View<std::string> patterns)
    {
        // Remove names in result[start_at .. end] which no package pattern matches
        result.erase(std::remove_if(result.begin() + start_at,
                                    result.end(),
                                    [&](const std::string& name) {
                                        return std::none_of(
                                            patterns.begin(), patterns.end(), [&](const std::string& pattern) {
                                                return package_pattern_match(name, pattern) != 0;
                                            });
                                    }),
                     result.end());
    }
} // unnamed namespace

namespace vcpkg
{
    ExpectedL<std::vector<std::string>> RegistrySet::get_all_reachable_port_names() const
    {
        std::vector<std::string> result;
        for (const auto& registry : registries())
        {
            const auto start_at = result.size();
            auto this_append = registry.implementation().append_all_port_names(result);
            if (!this_append)
            {
                return std::move(this_append).error();
            }

            remove_unreachable_port_names_by_patterns(result, start_at, registry.patterns());
        }

        if (auto registry = default_registry())
        {
            auto this_append = registry->append_all_port_names(result);
            if (!this_append)
            {
                return std::move(this_append).error();
            }
        }

        Util::sort_unique_erase(result);
        return result;
    }

    ExpectedL<std::vector<std::string>> RegistrySet::get_all_known_reachable_port_names_no_network() const
    {
        std::vector<std::string> result;
        for (const auto& registry : registries())
        {
            const auto start_at = result.size();
            const auto patterns = registry.patterns();
            auto maybe_append = registry.implementation().try_append_all_port_names_no_network(result);
            auto append = maybe_append.get();
            if (!append)
            {
                return std::move(maybe_append).error();
            }

            if (*append)
            {
                remove_unreachable_port_names_by_patterns(result, start_at, patterns);
            }
            else
            {
                // we don't know all names, but we can at least assume the exact match patterns
                // will be names
                std::remove_copy_if(patterns.begin(),
                                    patterns.end(),
                                    std::back_inserter(result),
                                    [&](const std::string& package_pattern) -> bool {
                                        return package_pattern.empty() || package_pattern.back() == '*';
                                    });
            }
        }

        if (auto registry = default_registry())
        {
            auto maybe_append = registry->try_append_all_port_names_no_network(result);
            if (!maybe_append)
            {
                return std::move(maybe_append).error();
            }
        }

        Util::sort_unique_erase(result);
        return result;
    }
} // namespace vcpkg

namespace
{
    ExpectedL<Optional<std::vector<GitVersionDbEntry>>> load_git_versions_file_impl(const ReadOnlyFilesystem& fs,
                                                                                    const Path& versions_file_path)
    {
        std::error_code ec;
        auto contents = fs.read_contents(versions_file_path, ec);
        if (ec)
        {
            if (ec == std::errc::no_such_file_or_directory)
            {
                return nullopt;
            }

            return format_filesystem_call_error(ec, "read_contents", {versions_file_path});
        }

        return Json::parse_object(contents, versions_file_path)
            .then([&](Json::Object&& versions_json) -> ExpectedL<Optional<std::vector<GitVersionDbEntry>>> {
                auto maybe_versions_array = versions_json.get(JsonIdVersions);
                if (!maybe_versions_array || !maybe_versions_array->is_array())
                {
                    return msg::format_error(msgFailedToParseNoVersionsArray, msg::path = versions_file_path);
                }

                std::vector<GitVersionDbEntry> db_entries;
                GitVersionDbEntryArrayDeserializer deserializer{};
                Json::Reader r(versions_file_path);
                r.visit_in_key(*maybe_versions_array, JsonIdVersions, db_entries, deserializer);
                if (r.messages().any_errors())
                {
                    return r.messages().join();
                }

                return db_entries;
            });
    }

    ExpectedL<Optional<std::vector<FilesystemVersionDbEntry>>> load_filesystem_versions_file_impl(
        const ReadOnlyFilesystem& fs, const Path& versions_file_path, const Path& registry_root)
    {
        std::error_code ec;
        auto contents = fs.read_contents(versions_file_path, ec);
        if (ec)
        {
            if (ec == std::errc::no_such_file_or_directory)
            {
                return nullopt;
            }

            return format_filesystem_call_error(ec, "read_contents", {versions_file_path});
        }

        return Json::parse_object(contents, versions_file_path)
            .then([&](Json::Object&& versions_json) -> ExpectedL<Optional<std::vector<FilesystemVersionDbEntry>>> {
                auto maybe_versions_array = versions_json.get(JsonIdVersions);
                if (!maybe_versions_array || !maybe_versions_array->is_array())
                {
                    return msg::format_error(msgFailedToParseNoVersionsArray, msg::path = versions_file_path);
                }

                std::vector<FilesystemVersionDbEntry> db_entries;
                FilesystemVersionDbEntryArrayDeserializer deserializer{registry_root};
                Json::Reader r(versions_file_path);
                r.visit_in_key(*maybe_versions_array, JsonIdVersions, db_entries, deserializer);
                if (r.messages().any_errors())
                {
                    return r.messages().join();
                }

                return db_entries;
            });
    }
} // unnamed namespace

namespace vcpkg
{
    GitVersionsLoadResult load_git_versions_file(const ReadOnlyFilesystem& fs,
                                                 const Path& registry_versions,
                                                 StringView port_name)
    {
        auto versions_file_path = registry_versions / relative_path_to_versions(port_name);
        auto result = load_git_versions_file_impl(fs, versions_file_path);
        if (!result)
        {
            result.error()
                .append_raw('\n')
                .append_raw(NotePrefix)
                .append(msgWhileParsingVersionsForPort, msg::package_name = port_name, msg::path = versions_file_path);
        }

        return {std::move(result), std::move(versions_file_path)};
    }

    FullGitVersionsDatabase::FullGitVersionsDatabase(
        const ReadOnlyFilesystem& fs,
        const Path& registry_versions,
        std::map<std::string, GitVersionsLoadResult, std::less<>>&& initial)
        : m_fs(&fs), m_registry_versions(registry_versions), m_cache(std::move(initial))
    {
    }

    FullGitVersionsDatabase::FullGitVersionsDatabase(FullGitVersionsDatabase&&) = default;
    FullGitVersionsDatabase& FullGitVersionsDatabase::operator=(FullGitVersionsDatabase&&) = default;

    const GitVersionsLoadResult& FullGitVersionsDatabase::lookup(StringView port_name)
    {
        auto it = m_cache.lower_bound(port_name);
        if (it != m_cache.end() && port_name >= it->first)
        {
            return it->second;
        }

        return m_cache.emplace_hint(it, port_name, load_git_versions_file(*m_fs, m_registry_versions, port_name))
            ->second;
    }

    const std::map<std::string, GitVersionsLoadResult, std::less<>>& FullGitVersionsDatabase::cache() const
    {
        return m_cache;
    }

    ExpectedL<FullGitVersionsDatabase> load_all_git_versions_files(const ReadOnlyFilesystem& fs,
                                                                   const Path& registry_versions)
    {
        auto maybe_letter_directories = fs.try_get_directories_non_recursive(registry_versions);
        auto letter_directories = maybe_letter_directories.get();
        if (!letter_directories)
        {
            return std::move(maybe_letter_directories).error();
        }

        std::map<std::string, GitVersionsLoadResult, std::less<>> initial_result;
        for (auto&& letter_directory : *letter_directories)
        {
            auto maybe_versions_files = fs.try_get_files_non_recursive(letter_directory);
            auto versions_files = maybe_versions_files.get();
            if (!versions_files)
            {
                return std::move(maybe_versions_files).error();
            }

            for (auto&& versions_file : *versions_files)
            {
                auto port_name_json = versions_file.filename();
                static constexpr StringLiteral dot_json = ".json";
                if (!port_name_json.ends_with(dot_json))
                {
                    continue;
                }

                StringView port_name{port_name_json.data(), port_name_json.size() - dot_json.size()};
                auto maybe_port_versions = load_git_versions_file_impl(fs, versions_file);
                if (!maybe_port_versions)
                {
                    maybe_port_versions.error()
                        .append_raw('\n')
                        .append_raw(NotePrefix)
                        .append(
                            msgWhileParsingVersionsForPort, msg::package_name = port_name, msg::path = versions_file);
                }

                initial_result.emplace(port_name, GitVersionsLoadResult{std::move(maybe_port_versions), versions_file});
            }
        }

        return FullGitVersionsDatabase{fs, registry_versions, std::move(initial_result)};
    }

    ExpectedL<Optional<std::vector<FilesystemVersionDbEntry>>> load_filesystem_versions_file(
        const ReadOnlyFilesystem& fs, const Path& registry_versions, StringView port_name, const Path& registry_root)
    {
        auto versions_file_path = registry_versions / relative_path_to_versions(port_name);
        auto result = load_filesystem_versions_file_impl(fs, versions_file_path, registry_root);
        if (!result)
        {
            result.error()
                .append_raw('\n')
                .append_raw(NotePrefix)
                .append(msgWhileParsingVersionsForPort, msg::package_name = port_name, msg::path = versions_file_path);
        }

        return result;
    }

    ExpectedL<Baseline> get_builtin_baseline(const VcpkgPaths& paths)
    {
        return load_baseline_versions(paths.get_filesystem(), paths.builtin_registry_versions / FileBaselineDotJson);
    }

    std::unique_ptr<RegistryImplementation> make_builtin_registry(const VcpkgPaths& paths)
    {
        if (paths.use_git_default_registry())
        {
            return std::make_unique<BuiltinErrorRegistry>();
        }
        else
        {
            return std::make_unique<BuiltinFilesRegistry>(paths);
        }
    }
    std::unique_ptr<RegistryImplementation> make_builtin_registry(const VcpkgPaths& paths, std::string baseline)
    {
        if (paths.use_git_default_registry())
        {
            return std::make_unique<GitRegistry>(
                paths, builtin_registry_git_url.to_string(), "HEAD", std::move(baseline));
        }
        else
        {
            return std::make_unique<BuiltinGitRegistry>(paths, std::move(baseline));
        }
    }
    std::unique_ptr<RegistryImplementation> make_git_registry(const VcpkgPaths& paths,
                                                              std::string repo,
                                                              std::string reference,
                                                              std::string baseline)
    {
        return std::make_unique<GitRegistry>(paths, std::move(repo), std::move(reference), std::move(baseline));
    }
    std::unique_ptr<RegistryImplementation> make_filesystem_registry(const ReadOnlyFilesystem& fs,
                                                                     Path path,
                                                                     std::string baseline)
    {
        return std::make_unique<FilesystemRegistry>(fs, std::move(path), std::move(baseline));
    }

    LocalizedString FilesystemVersionDbEntryDeserializer::type_name() const
    {
        return msg::format(msgAVersionDatabaseEntry);
    }
    View<StringLiteral> FilesystemVersionDbEntryDeserializer::valid_fields() const noexcept
    {
        static constexpr StringLiteral fields[] = {VCPKG_SCHEMED_DESERIALIZER_FIELDS, JsonIdPath};
        return fields;
    }

    Optional<FilesystemVersionDbEntry> FilesystemVersionDbEntryDeserializer::visit_object(Json::Reader& r,
                                                                                          const Json::Object& obj) const
    {
        FilesystemVersionDbEntry ret;
        ret.version = visit_required_schemed_version(type_name(), r, obj);

        std::string path_res_storage;
        r.required_object_field(
            type_name(), obj, JsonIdPath, path_res_storage, RegistryPathStringDeserializer::instance);
        StringView path_res{path_res_storage};
        if (!path_res.starts_with("$/"))
        {
            r.add_generic_error(msg::format(msgARegistryPath), msg::format(msgARegistryPathMustStartWithDollar));
            return nullopt;
        }

        if (path_res.contains('\\') || path_res.contains("//"))
        {
            r.add_generic_error(msg::format(msgARegistryPath),
                                msg::format(msgARegistryPathMustBeDelimitedWithForwardSlashes));
            return nullopt;
        }

        auto first = path_res.begin();
        const auto last = path_res.end();
        for (const char* candidate;; first = candidate)
        {
            candidate = std::find(first, last, '/');
            if (candidate == last)
            {
                break;
            }

            ++candidate;
            if (candidate == last)
            {
                break;
            }

            if (*candidate != '.')
            {
                continue;
            }

            ++candidate;
            if (candidate == last || *candidate == '/')
            {
                r.add_generic_error(msg::format(msgARegistryPath), msg::format(msgARegistryPathMustNotHaveDots));
                return nullopt;
            }

            if (*candidate != '.')
            {
                first = candidate;
                continue;
            }

            ++candidate;
            if (candidate == last || *candidate == '/')
            {
                r.add_generic_error(msg::format(msgARegistryPath), msg::format(msgARegistryPathMustNotHaveDots));
                return nullopt;
            }
        }

        ret.p = registry_root / path_res.substr(2);

        return ret;
    }

    LocalizedString FilesystemVersionDbEntryArrayDeserializer::type_name() const
    {
        return msg::format(msgAnArrayOfVersions);
    }

    Optional<std::vector<FilesystemVersionDbEntry>> FilesystemVersionDbEntryArrayDeserializer::visit_array(
        Json::Reader& r, const Json::Array& arr) const
    {
        return r.array_elements(arr, underlying);
    }

    LocalizedString GitVersionDbEntryDeserializer::type_name() const { return msg::format(msgAVersionDatabaseEntry); }
    View<StringLiteral> GitVersionDbEntryDeserializer::valid_fields() const noexcept
    {
        static constexpr StringLiteral fields[] = {VCPKG_SCHEMED_DESERIALIZER_FIELDS, JsonIdGitTree};
        return fields;
    }

    Optional<GitVersionDbEntry> GitVersionDbEntryDeserializer::visit_object(Json::Reader& r,
                                                                            const Json::Object& obj) const
    {
        GitVersionDbEntry ret;
        ret.version = visit_required_schemed_version(type_name(), r, obj);
        r.required_object_field(type_name(), obj, JsonIdGitTree, ret.git_tree, GitTreeStringDeserializer::instance);
        return ret;
    }

    LocalizedString GitVersionDbEntryArrayDeserializer::type_name() const { return msg::format(msgAnArrayOfVersions); }

    Optional<std::vector<GitVersionDbEntry>> GitVersionDbEntryArrayDeserializer::visit_array(
        Json::Reader& r, const Json::Array& arr) const
    {
        return r.array_elements(arr, GitVersionDbEntryDeserializer());
    }
}
