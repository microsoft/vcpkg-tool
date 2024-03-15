#include <vcpkg/base/cache.h>
#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/delayed-init.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/json.h>
#include <vcpkg/base/jsonreader.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/util.h>

#include <vcpkg/documentation.h>
#include <vcpkg/metrics.h>
#include <vcpkg/paragraphs.h>
#include <vcpkg/registries.h>
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

    struct GitVersionDbEntryDeserializer final : Json::IDeserializer<GitVersionDbEntry>
    {
        LocalizedString type_name() const override;
        View<StringView> valid_fields() const override;
        Optional<GitVersionDbEntry> visit_object(Json::Reader& r, const Json::Object& obj) const override;
    };

    LocalizedString GitVersionDbEntryDeserializer::type_name() const { return msg::format(msgAVersionDatabaseEntry); }
    View<StringView> GitVersionDbEntryDeserializer::valid_fields() const
    {
        static constexpr StringView u_git[] = {JsonIdGitTree};
        static const auto t_git = vcpkg::Util::Vectors::concat<StringView>(schemed_deserializer_fields(), u_git);

        return t_git;
    }

    Optional<GitVersionDbEntry> GitVersionDbEntryDeserializer::visit_object(Json::Reader& r,
                                                                            const Json::Object& obj) const
    {
        GitVersionDbEntry ret;
        ret.version = visit_required_schemed_version(type_name(), r, obj);
        r.required_object_field(type_name(), obj, JsonIdGitTree, ret.git_tree, GitTreeStringDeserializer::instance);
        return ret;
    }

    struct GitVersionDbEntryArrayDeserializer final : Json::IDeserializer<std::vector<GitVersionDbEntry>>
    {
        virtual LocalizedString type_name() const override;
        virtual Optional<std::vector<GitVersionDbEntry>> visit_array(Json::Reader& r,
                                                                     const Json::Array& arr) const override;

    private:
        GitVersionDbEntryDeserializer underlying;
    };
    LocalizedString GitVersionDbEntryArrayDeserializer::type_name() const { return msg::format(msgAnArrayOfVersions); }

    Optional<std::vector<GitVersionDbEntry>> GitVersionDbEntryArrayDeserializer::visit_array(
        Json::Reader& r, const Json::Array& arr) const
    {
        return r.array_elements(arr, underlying);
    }

    struct FilesystemVersionDbEntryDeserializer final : Json::IDeserializer<FilesystemVersionDbEntry>
    {
        LocalizedString type_name() const override;
        View<StringView> valid_fields() const override;
        Optional<FilesystemVersionDbEntry> visit_object(Json::Reader& r, const Json::Object& obj) const override;
        FilesystemVersionDbEntryDeserializer(const Path& root) : registry_root(root) { }

    private:
        Path registry_root;
    };

    LocalizedString FilesystemVersionDbEntryDeserializer::type_name() const
    {
        return msg::format(msgAVersionDatabaseEntry);
    }
    View<StringView> FilesystemVersionDbEntryDeserializer::valid_fields() const
    {
        static constexpr StringView u_path[] = {JsonIdPath};
        static const auto t_path = vcpkg::Util::Vectors::concat<StringView>(schemed_deserializer_fields(), u_path);
        return t_path;
    }

    Optional<FilesystemVersionDbEntry> FilesystemVersionDbEntryDeserializer::visit_object(Json::Reader& r,
                                                                                          const Json::Object& obj) const
    {
        FilesystemVersionDbEntry ret;
        ret.version = visit_required_schemed_version(type_name(), r, obj);

        std::string path_res;
        r.required_object_field(type_name(), obj, JsonIdPath, path_res, RegistryPathStringDeserializer::instance);
        if (!Strings::starts_with(path_res, "$/"))
        {
            r.add_generic_error(msg::format(msgARegistryPath), msg::format(msgARegistryPathMustStartWithDollar));
            return nullopt;
        }

        if (Strings::contains(path_res, '\\') || Strings::contains(path_res, "//"))
        {
            r.add_generic_error(msg::format(msgARegistryPath),
                                msg::format(msgARegistryPathMustBeDelimitedWithForwardSlashes));
            return nullopt;
        }

        auto first = path_res.begin();
        const auto last = path_res.end();
        for (std::string::iterator candidate;; first = candidate)
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

        ret.p = registry_root / StringView{path_res}.substr(2);

        return ret;
    }

    struct FilesystemVersionDbEntryArrayDeserializer final : Json::IDeserializer<std::vector<FilesystemVersionDbEntry>>
    {
        virtual LocalizedString type_name() const override;
        virtual Optional<std::vector<FilesystemVersionDbEntry>> visit_array(Json::Reader& r,
                                                                            const Json::Array& arr) const override;
        FilesystemVersionDbEntryArrayDeserializer(const Path& root) : underlying{root} { }

    private:
        FilesystemVersionDbEntryDeserializer underlying;
    };
    LocalizedString FilesystemVersionDbEntryArrayDeserializer::type_name() const
    {
        return msg::format(msgAnArrayOfVersions);
    }

    Optional<std::vector<FilesystemVersionDbEntry>> FilesystemVersionDbEntryArrayDeserializer::visit_array(
        Json::Reader& r, const Json::Array& arr) const
    {
        return r.array_elements(arr, underlying);
    }

    using Baseline = std::map<std::string, Version, std::less<>>;

    struct GitRegistry;

    struct PortVersionsGitTreesStructOfArrays
    {
        PortVersionsGitTreesStructOfArrays() = default;
        PortVersionsGitTreesStructOfArrays(const PortVersionsGitTreesStructOfArrays&) = default;
        PortVersionsGitTreesStructOfArrays(PortVersionsGitTreesStructOfArrays&&) = default;
        PortVersionsGitTreesStructOfArrays& operator=(const PortVersionsGitTreesStructOfArrays&) = default;
        PortVersionsGitTreesStructOfArrays& operator=(PortVersionsGitTreesStructOfArrays&&) = default;

        explicit PortVersionsGitTreesStructOfArrays(std::vector<GitVersionDbEntry>&& db_entries)
        {
            assign(std::move(db_entries));
        }

        void assign(std::vector<GitVersionDbEntry>&& db_entries)
        {
            m_port_versions.reserve(db_entries.size());
            m_git_trees.reserve(db_entries.size());
            m_port_versions.clear();
            m_git_trees.clear();
            for (auto& entry : db_entries)
            {
                m_port_versions.push_back(std::move(entry.version.version));
                m_git_trees.push_back(std::move(entry.git_tree));
            }

            db_entries.clear();
        }

        // these two map port versions to git trees
        // these shall have the same size, and git_trees[i] shall be the git tree for port_versions[i]
        const std::vector<Version>& port_versions() const noexcept { return m_port_versions; }
        const std::vector<std::string>& git_trees() const noexcept { return m_git_trees; }

    private:
        std::vector<Version> m_port_versions;
        std::vector<std::string> m_git_trees;
    };

    struct GitRegistryEntry final : RegistryEntry
    {
        GitRegistryEntry(StringView port_name,
                         const GitRegistry& parent,
                         bool stale,
                         std::vector<GitVersionDbEntry>&& version_entries);

        ExpectedL<View<Version>> get_port_versions() const override;
        ExpectedL<PortLocation> get_version(const Version& version) const override;

    private:
        ExpectedL<Unit> ensure_not_stale() const;

        std::string port_name;

        const GitRegistry& parent;

        // Indicates whether port_versions and git_trees were filled in with stale (i.e. lock) data.
        mutable bool stale;

        mutable PortVersionsGitTreesStructOfArrays last_loaded;
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
        friend struct GitRegistryEntry;

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
        BuiltinPortTreeRegistryEntry(StringView name_, Path root_, Version version_)
            : name(name_.to_string()), root(root_), version(version_)
        {
        }

        ExpectedL<View<Version>> get_port_versions() const override { return View<Version>{&version, 1}; }
        ExpectedL<PortLocation> get_version(const Version& v) const override
        {
            if (v == version)
            {
                return PortLocation{root, "git+https://github.com/Microsoft/vcpkg#ports/" + name};
            }

            return msg::format_error(msgVersionBuiltinPortTreeEntryMissing,
                                     msg::package_name = name,
                                     msg::expected = v.to_string(),
                                     msg::actual = version.to_string());
        }

        std::string name;
        Path root;
        Version version;
    };

    struct BuiltinGitRegistryEntry final : RegistryEntry
    {
        BuiltinGitRegistryEntry(const VcpkgPaths& paths) : m_paths(paths) { }

        ExpectedL<View<Version>> get_port_versions() const override
        {
            return View<Version>{port_versions_soa.port_versions()};
        }
        ExpectedL<PortLocation> get_version(const Version& version) const override;

        const VcpkgPaths& m_paths;

        std::string port_name;

        PortVersionsGitTreesStructOfArrays port_versions_soa;
    };

    struct FilesystemRegistryEntry final : RegistryEntry
    {
        explicit FilesystemRegistryEntry(std::string&& port_name) : port_name(port_name) { }

        ExpectedL<View<Version>> get_port_versions() const override { return View<Version>{port_versions}; }

        ExpectedL<PortLocation> get_version(const Version& version) const override;

        std::string port_name;
        // these two map port versions to paths
        // these shall have the same size, and paths[i] shall be the path for port_versions[i]
        std::vector<Version> port_versions;
        std::vector<Path> version_paths;
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
        const ExpectedL<SourceControlFileAndLocation>& get_scfl(StringView port_name, const Path& path) const
        {
            return m_scfls.get_lazy(
                path, [&, this]() { return Paragraphs::try_load_port(m_fs, port_name, PortLocation{path}); });
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
    ExpectedL<Optional<std::vector<GitVersionDbEntry>>> load_git_versions_file(const ReadOnlyFilesystem& fs,
                                                                               const Path& registry_versions,
                                                                               StringView port_name);

    ExpectedL<Optional<std::vector<FilesystemVersionDbEntry>>> load_filesystem_versions_file(
        const ReadOnlyFilesystem& fs, const Path& registry_versions, StringView port_name, const Path& registry_root);

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

                if (!Strings::ends_with(filename, ".json"))
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
        auto port_directory = m_builtin_ports_directory / port_name;
        const auto& maybe_maybe_scfl = get_scfl(port_name, port_directory);
        const auto maybe_scfl = maybe_maybe_scfl.get();
        if (!maybe_scfl)
        {
            return maybe_maybe_scfl.error();
        }

        auto scf = maybe_scfl->source_control_file.get();
        if (!scf)
        {
            return std::unique_ptr<RegistryEntry>();
        }

        if (scf->core_paragraph->name == port_name)
        {
            return std::make_unique<BuiltinPortTreeRegistryEntry>(
                scf->core_paragraph->name, port_directory, scf->to_version());
        }

        return msg::format_error(msgUnexpectedPortName,
                                 msg::expected = scf->core_paragraph->name,
                                 msg::actual = port_name,
                                 msg::path = port_directory);
    }

    ExpectedL<Optional<Version>> BuiltinFilesRegistry::get_baseline_version(StringView port_name) const
    {
        // if a baseline is not specified, use the ports directory version
        const auto& maybe_maybe_scfl = get_scfl(port_name, m_builtin_ports_directory / port_name);
        auto maybe_scfl = maybe_maybe_scfl.get();
        if (!maybe_scfl)
        {
            return maybe_maybe_scfl.error();
        }

        auto scf = maybe_scfl->source_control_file.get();
        if (!scf)
        {
            return Optional<Version>();
        }

        return scf->to_version();
    }

    ExpectedL<Unit> BuiltinFilesRegistry::append_all_port_names(std::vector<std::string>& out) const
    {
        auto maybe_port_directories = m_fs.try_get_directories_non_recursive(m_builtin_ports_directory);
        if (auto port_directories = maybe_port_directories.get())
        {
            for (auto&& port_directory : *port_directories)
            {
                auto filename = port_directory.filename();
                if (filename == ".DS_Store") continue;
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
        auto maybe_maybe_version_entries = load_git_versions_file(fs, m_paths.builtin_registry_versions, port_name);
        auto maybe_version_entries = maybe_maybe_version_entries.get();
        if (!maybe_version_entries)
        {
            return std::move(maybe_maybe_version_entries).error();
        }

        auto version_entries = maybe_version_entries->get();
        if (!version_entries)
        {
            return m_files_impl->get_port_entry(port_name);
        }

        auto res = std::make_unique<BuiltinGitRegistryEntry>(m_paths);
        res->port_name.assign(port_name.data(), port_name.size());
        res->port_versions_soa.assign(std::move(*version_entries));
        return res;
    }

    ExpectedL<Optional<Version>> BuiltinGitRegistry::get_baseline_version(StringView port_name) const
    {
        const auto& maybe_baseline = m_baseline.get([this]() -> ExpectedL<Baseline> {
            return git_checkout_baseline(m_paths, m_baseline_identifier)
                .then([&](Path&& path) { return load_baseline_versions(m_paths.get_filesystem(), path); })
                .map_error([&](LocalizedString&& error) {
                    return std::move(error).append(msgWhileCheckingOutBaseline,
                                                   msg::commit_sha = m_baseline_identifier);
                });
        });

        auto baseline = maybe_baseline.get();
        if (!baseline)
        {
            return maybe_baseline.error();
        }

        auto it = baseline->find(port_name);
        if (it != baseline->end())
        {
            return it->second;
        }

        return Optional<Version>();
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
        return m_baseline
            .get([this]() {
                return load_baseline_versions(m_fs, m_path / FileVersions / FileBaselineDotJson, m_baseline_identifier);
            })
            .then([&](const Baseline& baseline) -> ExpectedL<Optional<Version>> {
                auto it = baseline.find(port_name);
                if (it != baseline.end())
                {
                    return it->second;
                }

                return Optional<Version>();
            });
    }

    ExpectedL<std::unique_ptr<RegistryEntry>> FilesystemRegistry::get_port_entry(StringView port_name) const
    {
        auto maybe_maybe_version_entries =
            load_filesystem_versions_file(m_fs, m_path / FileVersions, port_name, m_path);
        auto maybe_version_entries = maybe_maybe_version_entries.get();
        if (!maybe_version_entries)
        {
            return std::move(maybe_maybe_version_entries).error();
        }

        auto version_entries = maybe_version_entries->get();
        if (!version_entries)
        {
            return std::unique_ptr<RegistryEntry>{};
        }

        auto res = std::make_unique<FilesystemRegistryEntry>(port_name.to_string());
        for (auto&& version_entry : *version_entries)
        {
            res->port_versions.push_back(std::move(version_entry.version.version));
            res->version_paths.push_back(std::move(version_entry.p));
        }

        return res;
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
                load_git_versions_file(m_paths.get_filesystem(), stale_vtp->p, port_name);
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

        auto maybe_live_vdb = get_versions_tree_path();
        auto live_vcb = maybe_live_vdb.get();
        if (!live_vcb)
        {
            return std::move(maybe_live_vdb).error();
        }

        {
            auto maybe_maybe_version_entries = load_git_versions_file(m_paths.get_filesystem(), *live_vcb, port_name);
            auto maybe_version_entries = maybe_maybe_version_entries.get();
            if (!maybe_version_entries)
            {
                return std::move(maybe_maybe_version_entries).error();
            }

            auto version_entries = maybe_version_entries->get();
            if (!version_entries)
            {
                // data is already live but we don't know of this port
                return std::unique_ptr<RegistryEntry>();
            }

            return std::make_unique<GitRegistryEntry>(port_name, *this, false, std::move(*version_entries));
        }
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
        const auto& maybe_baseline = m_baseline.get([this, port_name]() -> ExpectedL<Baseline> {
            // We delay baseline validation until here to give better error messages and suggestions
            if (!is_git_commit_sha(m_baseline_identifier))
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
        });

        auto baseline = maybe_baseline.get();
        if (!baseline)
        {
            return maybe_baseline.error();
        }

        auto it = baseline->find(port_name);
        if (it != baseline->end())
        {
            return it->second;
        }

        return Optional<Version>();
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
                                                     const std::vector<Version>& versions)
    {
        auto error_msg =
            msg::format_error(msgVersionGitEntryMissing, msg::package_name = port_name, msg::version = expected_version)
                .append_raw('\n');
        for (auto&& version : versions)
        {
            error_msg.append_indent().append_raw(version.to_string()).append_raw('\n');
        }

        error_msg.append(msgVersionIncomparable4, msg::url = docs::versioning_url);
        return error_msg;
    }

    // { RegistryEntry

    // { BuiltinRegistryEntry::RegistryEntry
    ExpectedL<PortLocation> BuiltinGitRegistryEntry::get_version(const Version& version) const
    {
        auto& port_versions = port_versions_soa.port_versions();
        auto it = std::find(port_versions.begin(), port_versions.end(), version);
        if (it == port_versions.end())
        {
            return format_version_git_entry_missing(port_name, version, port_versions)
                .append_raw('\n')
                .append_raw(NotePrefix)
                .append(msgChecksUpdateVcpkg);
        }

        const auto& git_tree = port_versions_soa.git_trees()[it - port_versions.begin()];
        return m_paths.versions_dot_git_dir()
            .then([&, this](Path&& dot_git) { return m_paths.git_checkout_port(port_name, git_tree, dot_git); })
            .map([&git_tree](Path&& p) -> PortLocation {
                return {
                    std::move(p),
                    "git+https://github.com/Microsoft/vcpkg@" + git_tree,
                };
            });
    }
    // } BuiltinRegistryEntry::RegistryEntry

    // { FilesystemRegistryEntry::RegistryEntry
    ExpectedL<PortLocation> FilesystemRegistryEntry::get_version(const Version& version) const
    {
        auto it = std::find(port_versions.begin(), port_versions.end(), version);
        if (it == port_versions.end())
        {
            return msg::format_error(
                msgVersionDatabaseEntryMissing, msg::package_name = port_name, msg::version = version);
        }

        return PortLocation{version_paths[it - port_versions.begin()]};
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
                load_git_versions_file(parent.m_paths.get_filesystem(), *live_vdb, port_name);
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

            last_loaded.assign(std::move(*version_entries));
            stale = false;
        }

        return Unit{};
    }

    ExpectedL<View<Version>> GitRegistryEntry::get_port_versions() const
    {
        // Getting all versions that might exist must always be done with 'live' data
        auto maybe_not_stale = ensure_not_stale();
        if (maybe_not_stale)
        {
            return View<Version>{last_loaded.port_versions()};
        }

        return std::move(maybe_not_stale).error();
    }

    ExpectedL<PortLocation> GitRegistryEntry::get_version(const Version& version) const
    {
        auto it = std::find(last_loaded.port_versions().begin(), last_loaded.port_versions().end(), version);
        if (it == last_loaded.port_versions().end() && stale)
        {
            // didn't find the version, maybe a newer version database will have it
            auto maybe_not_stale = ensure_not_stale();
            if (!maybe_not_stale)
            {
                return std::move(maybe_not_stale).error();
            }

            it = std::find(last_loaded.port_versions().begin(), last_loaded.port_versions().end(), version);
        }

        if (it == last_loaded.port_versions().end())
        {
            return format_version_git_entry_missing(port_name, version, last_loaded.port_versions());
        }

        const auto& git_tree = last_loaded.git_trees()[it - last_loaded.port_versions().begin()];
        return parent.m_paths.git_extract_tree_from_remote_registry(git_tree).map(
            [this, &git_tree](Path&& p) -> PortLocation {
                return {
                    std::move(p),
                    Strings::concat("git+", parent.m_repo, "@", git_tree),
                };
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

    ExpectedL<Optional<std::vector<GitVersionDbEntry>>> load_git_versions_file(const ReadOnlyFilesystem& fs,
                                                                               const Path& registry_versions,
                                                                               StringView port_name)
    {
        auto versions_file_path = registry_versions / relative_path_to_versions(port_name);
        std::error_code ec;
        auto contents = fs.read_contents(versions_file_path, ec);
        if (ec)
        {
            if (ec == std::errc::no_such_file_or_directory)
            {
                return Optional<std::vector<GitVersionDbEntry>>{};
            }

            return format_filesystem_call_error(ec, "read_contents", {versions_file_path});
        }

        auto maybe_versions_json = Json::parse_object(contents, versions_file_path);
        auto versions_json = maybe_versions_json.get();
        if (!versions_json)
        {
            return std::move(maybe_versions_json).error();
        }

        auto maybe_versions_array = versions_json->get(JsonIdVersions);
        if (!maybe_versions_array || !maybe_versions_array->is_array())
        {
            return msg::format_error(msgFailedToParseNoVersionsArray, msg::path = versions_file_path);
        }

        std::vector<GitVersionDbEntry> db_entries;
        GitVersionDbEntryArrayDeserializer deserializer{};
        Json::Reader r(versions_file_path);
        r.visit_in_key(*maybe_versions_array, JsonIdVersions, db_entries, deserializer);
        if (!r.errors().empty())
        {
            return msg::format_error(msgFailedToParseVersionsFile, msg::path = versions_file_path)
                .append_raw(Strings::join("\n", r.errors()));
        }

        return db_entries;
    }

    ExpectedL<Optional<std::vector<FilesystemVersionDbEntry>>> load_filesystem_versions_file(
        const ReadOnlyFilesystem& fs, const Path& registry_versions, StringView port_name, const Path& registry_root)
    {
        if (registry_root.empty())
        {
            Checks::unreachable(VCPKG_LINE_INFO, "type should never = Filesystem when registry_root is empty.");
        }

        auto versions_file_path = registry_versions / relative_path_to_versions(port_name);
        std::error_code ec;
        auto contents = fs.read_contents(versions_file_path, ec);
        if (ec)
        {
            if (ec == std::errc::no_such_file_or_directory)
            {
                return Optional<std::vector<FilesystemVersionDbEntry>>{};
            }

            return format_filesystem_call_error(ec, "read_contents", {versions_file_path});
        }

        auto maybe_versions_json = Json::parse_object(contents, versions_file_path);
        auto versions_json = maybe_versions_json.get();
        if (!versions_json)
        {
            return std::move(maybe_versions_json).error();
        }

        auto maybe_versions_array = versions_json->get(JsonIdVersions);
        if (!maybe_versions_array || !maybe_versions_array->is_array())
        {
            return msg::format_error(msgFailedToParseNoVersionsArray, msg::path = versions_file_path);
        }

        std::vector<FilesystemVersionDbEntry> db_entries;
        FilesystemVersionDbEntryArrayDeserializer deserializer{registry_root};
        Json::Reader r(versions_file_path);
        r.visit_in_key(*maybe_versions_array, JsonIdVersions, db_entries, deserializer);
        if (!r.errors().empty())
        {
            return msg::format_error(msgFailedToParseVersionsFile, msg::path = versions_file_path)
                .append_raw(Strings::join("\n", r.errors()));
        }

        return db_entries;
    }

    ExpectedL<Baseline> parse_baseline_versions(StringView contents, StringView baseline, StringView origin)
    {
        auto maybe_value = Json::parse(contents, origin);
        if (!maybe_value)
        {
            return std::move(maybe_value).error();
        }

        auto& value = *maybe_value.get();
        if (!value.value.is_object())
        {
            return msg::format_error(msgFailedToParseNoTopLevelObj, msg::path = origin);
        }

        auto real_baseline = baseline.size() == 0 ? StringView{JsonIdDefault} : baseline;
        const auto& obj = value.value.object(VCPKG_LINE_INFO);
        auto baseline_value = obj.get(real_baseline);
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
        if (r.errors().empty())
        {
            return std::move(result);
        }
        else
        {
            return msg::format_error(msgFailedToParseBaseline, msg::path = origin)
                .append_raw('\n')
                .append_raw(Strings::join("\n", r.errors()));
        }
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

    ExpectedL<Optional<std::vector<GitVersionDbEntry>>> get_builtin_versions(const VcpkgPaths& paths,
                                                                             StringView port_name)
    {
        return load_git_versions_file(paths.get_filesystem(), paths.builtin_registry_versions, port_name);
    }

    ExpectedL<Baseline> get_builtin_baseline(const VcpkgPaths& paths)
    {
        return load_baseline_versions(paths.get_filesystem(), paths.builtin_registry_versions / FileBaselineDotJson);
    }

    bool is_git_commit_sha(StringView sv)
    {
        static constexpr struct
        {
            bool operator()(char ch) const { return ('0' <= ch && ch <= '9') || ('a' <= ch && ch <= 'f'); }
        } is_lcase_ascii_hex;

        return sv.size() == 40 && std::all_of(sv.begin(), sv.end(), is_lcase_ascii_hex);
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

    std::unique_ptr<Json::IDeserializer<std::vector<GitVersionDbEntry>>> make_git_version_db_deserializer()
    {
        return std::make_unique<GitVersionDbEntryArrayDeserializer>();
    }

    std::unique_ptr<Json::IDeserializer<std::vector<FilesystemVersionDbEntry>>> make_filesystem_version_db_deserializer(
        const Path& root)
    {
        return std::make_unique<FilesystemVersionDbEntryArrayDeserializer>(root);
    }
}
