#include <vcpkg/base/cache.h>
#include <vcpkg/base/delayed-init.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/json.h>
#include <vcpkg/base/jsonreader.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/util.h>

#include <vcpkg/documentation.h>
#include <vcpkg/metrics.h>
#include <vcpkg/paragraphs.h>
#include <vcpkg/registries.h>
#include <vcpkg/registries.private.h>
#include <vcpkg/sourceparagraph.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>
#include <vcpkg/versiondeserializers.h>
#include <vcpkg/versions.h>

#include <map>

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

    struct VersionDbEntryDeserializer final : Json::IDeserializer<VersionDbEntry>
    {
        static constexpr StringLiteral GIT_TREE = "git-tree";
        static constexpr StringLiteral PATH = "path";

        LocalizedString type_name() const override;
        View<StringView> valid_fields() const override;
        Optional<VersionDbEntry> visit_object(Json::Reader& r, const Json::Object& obj) const override;
        VersionDbEntryDeserializer(VersionDbType type, const Path& root) : type(type), registry_root(root) { }

    private:
        VersionDbType type;
        Path registry_root;
    };
    constexpr StringLiteral VersionDbEntryDeserializer::GIT_TREE;
    constexpr StringLiteral VersionDbEntryDeserializer::PATH;
    LocalizedString VersionDbEntryDeserializer::type_name() const { return msg::format(msgAVersionDatabaseEntry); }
    View<StringView> VersionDbEntryDeserializer::valid_fields() const
    {
        static constexpr StringView u_git[] = {GIT_TREE};
        static constexpr StringView u_path[] = {PATH};
        static const auto t_git = vcpkg::Util::Vectors::concat<StringView>(schemed_deserializer_fields(), u_git);
        static const auto t_path = vcpkg::Util::Vectors::concat<StringView>(schemed_deserializer_fields(), u_path);

        return type == VersionDbType::Git ? t_git : t_path;
    }

    Optional<VersionDbEntry> VersionDbEntryDeserializer::visit_object(Json::Reader& r, const Json::Object& obj) const
    {
        VersionDbEntry ret;

        auto schemed_version = visit_required_schemed_deserializer(type_name(), r, obj);
        ret.scheme = schemed_version.scheme;
        ret.version = std::move(schemed_version.version);
        switch (type)
        {
            case VersionDbType::Git:
            {
                r.required_object_field(type_name(), obj, GIT_TREE, ret.git_tree, GitTreeStringDeserializer::instance);
                break;
            }
            case VersionDbType::Filesystem:
            {
                std::string path_res;
                r.required_object_field(type_name(), obj, PATH, path_res, RegistryPathStringDeserializer::instance);
                if (!Strings::starts_with(path_res, "$/"))
                {
                    r.add_generic_error(msg::format(msgARegistryPath),
                                        msg::format(msgARegistryPathMustStartWithDollar));
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
                        r.add_generic_error(msg::format(msgARegistryPath),
                                            msg::format(msgARegistryPathMustNotHaveDots));
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
                        r.add_generic_error(msg::format(msgARegistryPath),
                                            msg::format(msgARegistryPathMustNotHaveDots));
                        return nullopt;
                    }
                }

                ret.p = registry_root / StringView{path_res}.substr(2);
                break;
            }
        }

        return ret;
    }

    struct VersionDbEntryArrayDeserializer final : Json::IDeserializer<std::vector<VersionDbEntry>>
    {
        virtual LocalizedString type_name() const override;
        virtual Optional<std::vector<VersionDbEntry>> visit_array(Json::Reader& r,
                                                                  const Json::Array& arr) const override;
        VersionDbEntryArrayDeserializer(VersionDbType type, const Path& root) : underlying{type, root} { }

    private:
        VersionDbEntryDeserializer underlying;
    };
    LocalizedString VersionDbEntryArrayDeserializer::type_name() const { return msg::format(msgAnArrayOfVersions); }

    Optional<std::vector<VersionDbEntry>> VersionDbEntryArrayDeserializer::visit_array(Json::Reader& r,
                                                                                       const Json::Array& arr) const
    {
        return r.array_elements(arr, underlying);
    }

    using Baseline = std::map<std::string, Version, std::less<>>;

    static constexpr StringLiteral registry_versions_dir_name = "versions";

    struct GitRegistry;

    struct GitRegistryEntry final : RegistryEntry
    {
        GitRegistryEntry(const GitRegistry& reg, StringView name);

        View<Version> get_port_versions() const override;
        ExpectedL<PathAndLocation> get_version(const Version& version) const override;

    private:
        void fill_data_from_path(const ReadOnlyFilesystem& fs, const Path& port_versions_path) const;

        std::string port_name;

        const GitRegistry& parent;

        // Indicates whether port_versions and git_trees were filled in with stale (i.e. lock) data.
        mutable bool stale;

        // these two map port versions to git trees
        // these shall have the same size, and git_trees[i] shall be the git tree for port_versions[i]
        mutable std::vector<Version> port_versions;
        mutable std::vector<std::string> git_trees;
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

        std::unique_ptr<RegistryEntry> get_port_entry(StringView) const override;

        void get_all_port_names(std::vector<std::string>&) const override;

        ExpectedL<Version> get_baseline_version(StringView) const override;

    private:
        friend struct GitRegistryEntry;

        LockFile::Entry get_lock_entry() const
        {
            return m_lock_entry.get(
                [this]() { return m_paths.get_installed_lockfile().get_or_fetch(m_paths, m_repo, m_reference); });
        }

        Path get_versions_tree_path() const
        {
            return m_versions_tree.get([this]() -> Path {
                auto e = get_lock_entry();
                e.ensure_up_to_date(m_paths);
                auto maybe_tree = m_paths.git_find_object_id_for_remote_registry_path(
                    e.commit_id(), registry_versions_dir_name.to_string());
                if (!maybe_tree)
                {
                    get_global_metrics_collector().track_define(DefineMetric::RegistriesErrorNoVersionsAtCommit);
                    Checks::msg_exit_with_error(VCPKG_LINE_INFO,
                                                msg::format(msgCouldNotFindGitTreeAtCommit,
                                                            msg::package_name = m_repo,
                                                            msg::commit_sha = e.commit_id())
                                                    .append_raw('\n')
                                                    .append_raw(maybe_tree.error()));
                }
                auto maybe_path = m_paths.git_checkout_object_from_remote_registry(*maybe_tree.get());
                if (!maybe_path)
                {
                    msg::println_error(msgFailedToCheckoutRepo, msg::package_name = m_repo);
                    msg::println_error(LocalizedString::from_raw(maybe_path.error()));
                    Checks::exit_fail(VCPKG_LINE_INFO);
                }
                return std::move(*maybe_path.get());
            });
        }

        struct VersionsTreePathResult
        {
            Path p;
            bool stale;
        };

        VersionsTreePathResult get_stale_versions_tree_path() const
        {
            auto e = get_lock_entry();
            if (!e.stale())
            {
                return {get_versions_tree_path(), false};
            }
            if (!m_stale_versions_tree.has_value())
            {
                auto maybe_tree = m_paths.git_find_object_id_for_remote_registry_path(
                    e.commit_id(), registry_versions_dir_name.to_string());
                if (!maybe_tree)
                {
                    // This could be caused by git gc or otherwise -- fall back to full fetch
                    return {get_versions_tree_path(), false};
                }
                auto maybe_path = m_paths.git_checkout_object_from_remote_registry(*maybe_tree.get());
                if (!maybe_path)
                {
                    // This could be caused by git gc or otherwise -- fall back to full fetch
                    return {get_versions_tree_path(), false};
                }
                m_stale_versions_tree = std::move(*maybe_path.get());
            }
            return {*m_stale_versions_tree.get(), true};
        }

        const VcpkgPaths& m_paths;

        std::string m_repo;
        std::string m_reference;
        std::string m_baseline_identifier;
        DelayedInit<LockFile::Entry> m_lock_entry;
        mutable Optional<Path> m_stale_versions_tree;
        DelayedInit<Path> m_versions_tree;
        DelayedInit<Baseline> m_baseline;
    };

    struct BuiltinPortTreeRegistryEntry final : RegistryEntry
    {
        BuiltinPortTreeRegistryEntry(StringView name_, Path root_, Version version_)
            : name(name_.to_string()), root(root_), version(version_)
        {
        }

        View<Version> get_port_versions() const override { return {&version, 1}; }
        ExpectedL<PathAndLocation> get_version(const Version& v) const override
        {
            if (v == version)
            {
                return PathAndLocation{root, "git+https://github.com/Microsoft/vcpkg#ports/" + name};
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

        View<Version> get_port_versions() const override { return port_versions; }
        ExpectedL<PathAndLocation> get_version(const Version& version) const override;

        const VcpkgPaths& m_paths;

        std::string port_name;

        // these two map port versions to git trees
        // these shall have the same size, and git_trees[i] shall be the git tree for port_versions[i]
        std::vector<Version> port_versions;
        std::vector<std::string> git_trees;
    };

    struct FilesystemRegistryEntry final : RegistryEntry
    {
        explicit FilesystemRegistryEntry(std::string&& port_name) : port_name(port_name) { }

        View<Version> get_port_versions() const override { return port_versions; }

        ExpectedL<PathAndLocation> get_version(const Version& version) const override;

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
        static constexpr StringLiteral s_kind = "builtin-files";

        BuiltinFilesRegistry(const VcpkgPaths& paths)
            : m_fs(paths.get_filesystem()), m_builtin_ports_directory(paths.builtin_ports_directory())
        {
        }

        StringLiteral kind() const override { return s_kind; }

        std::unique_ptr<RegistryEntry> get_port_entry(StringView port_name) const override;

        void get_all_port_names(std::vector<std::string>&) const override;

        ExpectedL<Version> get_baseline_version(StringView port_name) const override;

        ~BuiltinFilesRegistry() = default;

        DelayedInit<Baseline> m_baseline;

    private:
        const ParseExpected<SourceControlFile>& get_scf(const Path& path) const
        {
            return m_scfs.get_lazy(path, [this, &path]() { return Paragraphs::try_load_port(m_fs, path); });
        }

        const ReadOnlyFilesystem& m_fs;
        const Path m_builtin_ports_directory;
        Cache<Path, ParseExpected<SourceControlFile>> m_scfs;
    };
    constexpr StringLiteral BuiltinFilesRegistry::s_kind;

    // This registry implementation is a builtin registry with a provided
    // baseline that will perform git operations on the root git repo
    struct BuiltinGitRegistry final : RegistryImplementation
    {
        static constexpr StringLiteral s_kind = "builtin-git";

        BuiltinGitRegistry(const VcpkgPaths& paths, std::string&& baseline)
            : m_baseline_identifier(std::move(baseline))
            , m_files_impl(std::make_unique<BuiltinFilesRegistry>(paths))
            , m_paths(paths)
        {
        }

        StringLiteral kind() const override { return s_kind; }

        std::unique_ptr<RegistryEntry> get_port_entry(StringView port_name) const override;

        void get_all_port_names(std::vector<std::string>&) const override;

        ExpectedL<Version> get_baseline_version(StringView port_name) const override;

        ~BuiltinGitRegistry() = default;

        std::string m_baseline_identifier;
        DelayedInit<Baseline> m_baseline;

    private:
        std::unique_ptr<BuiltinFilesRegistry> m_files_impl;

        const VcpkgPaths& m_paths;
    };
    constexpr StringLiteral BuiltinGitRegistry::s_kind;

    // This registry entry is a stub that fails on all APIs; this is used in
    // read-only vcpkg if the user has not provided a baseline.
    struct BuiltinErrorRegistry final : RegistryImplementation
    {
        static constexpr StringLiteral s_kind = "builtin-error";

        StringLiteral kind() const override { return s_kind; }

        std::unique_ptr<RegistryEntry> get_port_entry(StringView) const override
        {
            Checks::msg_exit_with_error(VCPKG_LINE_INFO, msgErrorRequireBaseline);
        }

        void get_all_port_names(std::vector<std::string>&) const override
        {
            Checks::msg_exit_with_error(VCPKG_LINE_INFO, msgErrorRequireBaseline);
        }

        ExpectedL<Version> get_baseline_version(StringView) const override
        {
            Checks::msg_exit_with_error(VCPKG_LINE_INFO, msgErrorRequireBaseline);
        }

        ~BuiltinErrorRegistry() = default;
    };
    constexpr StringLiteral BuiltinErrorRegistry::s_kind;

    struct FilesystemRegistry final : RegistryImplementation
    {
        FilesystemRegistry(const ReadOnlyFilesystem& fs, Path&& path, std::string&& baseline)
            : m_fs(fs), m_path(std::move(path)), m_baseline_identifier(std::move(baseline))
        {
        }

        StringLiteral kind() const override { return "filesystem"; }

        std::unique_ptr<RegistryEntry> get_port_entry(StringView) const override;

        void get_all_port_names(std::vector<std::string>&) const override;

        ExpectedL<Version> get_baseline_version(StringView) const override;

    private:
        const ReadOnlyFilesystem& m_fs;

        Path m_path;
        std::string m_baseline_identifier;
        DelayedInit<Baseline> m_baseline;
    };

    Path relative_path_to_versions(StringView port_name);
    ExpectedL<std::vector<VersionDbEntry>> load_versions_file(const ReadOnlyFilesystem& fs,
                                                              VersionDbType vdb,
                                                              const Path& port_versions,
                                                              StringView port_name,
                                                              const Path& registry_root = {});

    // returns nullopt if the baseline is valid, but doesn't contain the specified baseline,
    // or (equivalently) if the baseline does not exist.
    ExpectedL<Optional<Baseline>> parse_baseline_versions(StringView contents, StringView baseline, StringView origin);
    ExpectedL<Optional<Baseline>> load_baseline_versions(const ReadOnlyFilesystem& fs,
                                                         const Path& baseline_path,
                                                         StringView identifier = {});

    void load_all_port_names_from_registry_versions(std::vector<std::string>& out,
                                                    const ReadOnlyFilesystem& fs,
                                                    const Path& port_versions_path)
    {
        for (auto&& super_directory : fs.get_directories_non_recursive(port_versions_path, VCPKG_LINE_INFO))
        {
            for (auto&& file : fs.get_regular_files_non_recursive(super_directory, VCPKG_LINE_INFO))
            {
                auto filename = file.filename();
                if (!Strings::case_insensitive_ascii_ends_with(filename, ".json")) continue;

                if (!Strings::ends_with(filename, ".json"))
                {
                    Checks::msg_exit_with_message(VCPKG_LINE_INFO, msgJsonFileMissingExtension, msg::path = file);
                }

                auto port_name = filename.substr(0, filename.size() - 5);
                if (!Json::IdentifierDeserializer::is_ident(port_name))
                {
                    Checks::msg_exit_maybe_upgrade(VCPKG_LINE_INFO, msgInvalidPortVersonName, msg::path = file);
                }

                out.push_back(port_name.to_string());
            }
        }
    }

    static ExpectedL<Path> git_checkout_baseline(const VcpkgPaths& paths, StringView commit_sha)
    {
        const Filesystem& fs = paths.get_filesystem();
        const auto destination_parent = paths.baselines_output() / commit_sha;
        auto destination = destination_parent / "baseline.json";
        if (!fs.exists(destination, IgnoreErrors{}))
        {
            const auto destination_tmp = destination_parent / "baseline.json.tmp";
            auto treeish = Strings::concat(commit_sha, ":versions/baseline.json");
            auto maybe_contents = paths.git_show(treeish, paths.root / ".git");
            if (auto contents = maybe_contents.get())
            {
                std::error_code ec;
                fs.create_directories(destination_parent, ec);
                if (ec)
                {
                    return {msg::format(msg::msgErrorMessage)
                                .append(format_filesystem_call_error(ec, "create_directories", {destination_parent}))
                                .append_raw('\n')
                                .append(msg::msgNoteMessage)
                                .append(msgWhileCheckingOutBaseline, msg::commit_sha = commit_sha),
                            expected_right_tag};
                }
                fs.write_contents(destination_tmp, *contents, ec);
                if (ec)
                {
                    return {msg::format(msg::msgErrorMessage)
                                .append(format_filesystem_call_error(ec, "write_contents", {destination_tmp}))
                                .append_raw('\n')
                                .append(msg::msgNoteMessage)
                                .append(msgWhileCheckingOutBaseline, msg::commit_sha = commit_sha),
                            expected_right_tag};
                }
                fs.rename(destination_tmp, destination, ec);
                if (ec)
                {
                    return {msg::format(msg::msgErrorMessage)
                                .append(format_filesystem_call_error(ec, "rename", {destination_tmp, destination}))
                                .append_raw('\n')
                                .append(msg::msgNoteMessage)
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
    std::unique_ptr<RegistryEntry> BuiltinFilesRegistry::get_port_entry(StringView port_name) const
    {
        auto port_directory = m_builtin_ports_directory / port_name;
        if (m_fs.exists(port_directory, IgnoreErrors{}))
        {
            const auto& found_scf = get_scf(port_directory);
            if (auto scfp = found_scf.get())
            {
                auto& scf = *scfp;
                if (scf->core_paragraph->name == port_name)
                {
                    return std::make_unique<BuiltinPortTreeRegistryEntry>(
                        scf->core_paragraph->name, port_directory, scf->to_version());
                }
                msg::println_error(msgUnexpectedPortName,
                                   msg::expected = scf->core_paragraph->name,
                                   msg::actual = port_name,
                                   msg::path = port_directory);
                Checks::exit_fail(VCPKG_LINE_INFO);
            }
        }

        return nullptr;
    }

    ExpectedL<Version> BuiltinFilesRegistry::get_baseline_version(StringView port_name) const
    {
        // if a baseline is not specified, use the ports directory version
        auto port_path = m_builtin_ports_directory / port_name;
        const auto& maybe_scf = get_scf(port_path);
        if (auto pscf = maybe_scf.get())
        {
            return (*pscf)->to_version();
        }
        return LocalizedString::from_raw(ParseControlErrorInfo::format_errors({&maybe_scf.error(), 1}));
    }

    void BuiltinFilesRegistry::get_all_port_names(std::vector<std::string>& out) const
    {
        std::error_code ec;
        auto port_directories = m_fs.get_directories_non_recursive(m_builtin_ports_directory, VCPKG_LINE_INFO);

        for (auto&& port_directory : port_directories)
        {
            auto filename = port_directory.filename();
            if (filename == ".DS_Store") continue;
            out.push_back(filename.to_string());
        }
    }
    // } BuiltinFilesRegistry::RegistryImplementation

    // { BuiltinGitRegistry::RegistryImplementation
    std::unique_ptr<RegistryEntry> BuiltinGitRegistry::get_port_entry(StringView port_name) const
    {
        const auto& fs = m_paths.get_filesystem();

        auto versions_path = m_paths.builtin_registry_versions / relative_path_to_versions(port_name);
        if (fs.exists(versions_path, IgnoreErrors{}))
        {
            auto maybe_version_entries =
                load_versions_file(fs, VersionDbType::Git, m_paths.builtin_registry_versions, port_name);
            if (!maybe_version_entries)
            {
                Checks::msg_exit_maybe_upgrade(VCPKG_LINE_INFO, maybe_version_entries.error());
            }

            auto version_entries = std::move(maybe_version_entries).value_or_exit(VCPKG_LINE_INFO);
            auto res = std::make_unique<BuiltinGitRegistryEntry>(m_paths);
            res->port_name = port_name.to_string();
            for (auto&& version_entry : version_entries)
            {
                res->port_versions.push_back(version_entry.version);
                res->git_trees.push_back(version_entry.git_tree);
            }
            return res;
        }

        return m_files_impl->get_port_entry(port_name);
    }

    ExpectedL<Version> BuiltinGitRegistry::get_baseline_version(StringView port_name) const
    {
        const auto& baseline = m_baseline.get([this]() -> Baseline {
            auto maybe_path = git_checkout_baseline(m_paths, m_baseline_identifier);
            if (!maybe_path)
            {
                msg::println(Color::error, LocalizedString::from_raw(maybe_path.error()));
                msg::println(Color::error, LocalizedString::from_raw(m_paths.get_current_git_sha_baseline_message()));
                Checks::exit_fail(VCPKG_LINE_INFO);
            }
            auto b = load_baseline_versions(m_paths.get_filesystem(), *maybe_path.get()).value_or_exit(VCPKG_LINE_INFO);
            if (auto p = b.get())
            {
                return std::move(*p);
            }
            Checks::msg_exit_with_message(
                VCPKG_LINE_INFO, msgBaselineFileNoDefaultField, msg::commit_sha = m_baseline_identifier);
        });

        auto it = baseline.find(port_name);
        if (it != baseline.end())
        {
            return it->second;
        }
        return msg::format(msg::msgErrorMessage).append(msgPortNotInBaseline, msg::package_name = port_name);
    }

    void BuiltinGitRegistry::get_all_port_names(std::vector<std::string>& out) const
    {
        const auto& fs = m_paths.get_filesystem();

        if (fs.exists(m_paths.builtin_registry_versions, IgnoreErrors{}))
        {
            load_all_port_names_from_registry_versions(out, fs, m_paths.builtin_registry_versions);
        }

        m_files_impl->get_all_port_names(out);
    }
    // } BuiltinGitRegistry::RegistryImplementation

    // { FilesystemRegistry::RegistryImplementation
    ExpectedL<Version> FilesystemRegistry::get_baseline_version(StringView port_name) const
    {
        const auto& baseline = m_baseline.get([this]() -> Baseline {
            auto path_to_baseline = m_path / registry_versions_dir_name / "baseline.json";
            auto res_baseline = load_baseline_versions(m_fs, path_to_baseline, m_baseline_identifier);
            if (auto opt_baseline = res_baseline.get())
            {
                if (auto p = opt_baseline->get())
                {
                    return std::move(*p);
                }

                if (m_baseline_identifier.size() == 0)
                {
                    return {};
                }

                Checks::msg_exit_with_error(VCPKG_LINE_INFO,
                                            msgCouldNotFindBaseline,
                                            msg::commit_sha = m_baseline_identifier,
                                            msg::path = path_to_baseline);
            }

            Checks::msg_exit_maybe_upgrade(VCPKG_LINE_INFO, res_baseline.error());
        });

        auto it = baseline.find(port_name);
        if (it != baseline.end())
        {
            return it->second;
        }
        else
        {
            return msg::format(msg::msgErrorMessage).append(msgPortNotInBaseline, msg::package_name = port_name);
        }
    }

    std::unique_ptr<RegistryEntry> FilesystemRegistry::get_port_entry(StringView port_name) const
    {
        auto maybe_version_entries =
            load_versions_file(m_fs, VersionDbType::Filesystem, m_path / registry_versions_dir_name, port_name, m_path);

        auto version_entries = std::move(maybe_version_entries).value_or_exit(VCPKG_LINE_INFO);

        auto res = std::make_unique<FilesystemRegistryEntry>(port_name.to_string());
        for (auto&& version_entry : version_entries)
        {
            res->port_versions.push_back(std::move(version_entry.version));
            res->version_paths.push_back(std::move(version_entry.p));
        }
        return res;
    }

    void FilesystemRegistry::get_all_port_names(std::vector<std::string>& out) const
    {
        load_all_port_names_from_registry_versions(out, m_fs, m_path / registry_versions_dir_name);
    }
    // } FilesystemRegistry::RegistryImplementation

    // { GitRegistry::RegistryImplementation
    std::unique_ptr<RegistryEntry> GitRegistry::get_port_entry(StringView port_name) const
    {
        return std::make_unique<GitRegistryEntry>(*this, port_name);
    }

    GitRegistryEntry::GitRegistryEntry(const GitRegistry& reg, StringView name)
        : port_name(name.to_string()), parent(reg)
    {
        auto vtp = parent.get_stale_versions_tree_path();
        stale = vtp.stale;
        fill_data_from_path(parent.m_paths.get_filesystem(), vtp.p);
    }

    ExpectedL<Version> GitRegistry::get_baseline_version(StringView port_name) const
    {
        const auto& baseline = m_baseline.get([this]() -> Baseline {
            // We delay baseline validation until here to give better error messages and suggestions
            if (!is_git_commit_sha(m_baseline_identifier))
            {
                auto e = get_lock_entry();
                e.ensure_up_to_date(m_paths);
                Checks::msg_exit_maybe_upgrade(VCPKG_LINE_INFO,
                                               msgGitRegistryMustHaveBaseline,
                                               msg::package_name = m_repo,
                                               msg::value = e.commit_id());
            }

            auto path_to_baseline = Path(registry_versions_dir_name.to_string()) / "baseline.json";
            auto maybe_contents = m_paths.git_show_from_remote_registry(m_baseline_identifier, path_to_baseline);
            if (!maybe_contents)
            {
                get_lock_entry().ensure_up_to_date(m_paths);
                maybe_contents = m_paths.git_show_from_remote_registry(m_baseline_identifier, path_to_baseline);
            }
            if (!maybe_contents)
            {
                msg::println(msgFetchingBaselineInfo, msg::package_name = m_repo);
                auto maybe_err = m_paths.git_fetch(m_repo, m_baseline_identifier);
                if (!maybe_err)
                {
                    get_global_metrics_collector().track_define(DefineMetric::RegistriesErrorCouldNotFindBaseline);

                    msg::println_error(msgCouldNotFindBaselineForRepo,
                                       msg::commit_sha = m_baseline_identifier,
                                       msg::package_name = m_repo);

                    msg::println_error(msg::format(msgFailedToFetchError,
                                                   msg::error_msg = maybe_contents.error(),
                                                   msg::package_name = m_repo)
                                           .append_raw('\n')
                                           .append_raw(maybe_err.error()));

                    Checks::exit_fail(VCPKG_LINE_INFO);
                }

                maybe_contents = m_paths.git_show_from_remote_registry(m_baseline_identifier, path_to_baseline);
            }

            if (!maybe_contents)
            {
                get_global_metrics_collector().track_define(DefineMetric::RegistriesErrorCouldNotFindBaseline);
                Checks::msg_exit_with_message(VCPKG_LINE_INFO,
                                              msg::format(msgCouldNotFindBaselineInCommit,
                                                          msg::commit_sha = m_baseline_identifier,
                                                          msg::package_name = m_repo)
                                                  .append_raw('\n')
                                                  .append_raw(maybe_contents.error()));
            }

            auto contents = maybe_contents.get();
            auto res_baseline = parse_baseline_versions(*contents, "default", path_to_baseline);
            if (auto opt_baseline = res_baseline.get())
            {
                if (auto p = opt_baseline->get())
                {
                    return std::move(*p);
                }
                else
                {
                    get_global_metrics_collector().track_define(DefineMetric::RegistriesErrorCouldNotFindBaseline);
                    Checks::msg_exit_maybe_upgrade(VCPKG_LINE_INFO,
                                                   msgBaselineMissingDefault,
                                                   msg::commit_sha = m_baseline_identifier,
                                                   msg::url = m_repo);
                }
            }
            else
            {
                msg::println_error(msg::format(msgErrorWhileFetchingBaseline,
                                               msg::value = m_baseline_identifier,
                                               msg::package_name = m_repo)
                                       .append_raw('\n')
                                       .append(LocalizedString::from_raw(res_baseline.error())));
                Checks::exit_fail(VCPKG_LINE_INFO);
            }
        });

        auto it = baseline.find(port_name);
        if (it != baseline.end())
        {
            return it->second;
        }

        return msg::format(msg::msgErrorMessage).append(msgPortNotInBaseline, msg::package_name = port_name);
    }

    void GitRegistry::get_all_port_names(std::vector<std::string>& out) const
    {
        auto versions_path = get_stale_versions_tree_path();
        load_all_port_names_from_registry_versions(out, m_paths.get_filesystem(), versions_path.p);
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
    ExpectedL<PathAndLocation> BuiltinGitRegistryEntry::get_version(const Version& version) const
    {
        auto it = std::find(port_versions.begin(), port_versions.end(), version);
        if (it == port_versions.end())
        {
            return format_version_git_entry_missing(port_name, version, port_versions)
                .append_raw('\n')
                .append(msg::msgNoteMessage)
                .append(msgChecksUpdateVcpkg);
        }

        const auto& git_tree = git_trees[it - port_versions.begin()];
        return m_paths.git_checkout_port(port_name, git_tree, m_paths.root / ".git")
            .map([&git_tree](Path&& p) -> PathAndLocation {
                return {
                    std::move(p),
                    "git+https://github.com/Microsoft/vcpkg@" + git_tree,
                };
            });
    }
    // } BuiltinRegistryEntry::RegistryEntry

    // { FilesystemRegistryEntry::RegistryEntry
    ExpectedL<PathAndLocation> FilesystemRegistryEntry::get_version(const Version& version) const
    {
        auto it = std::find(port_versions.begin(), port_versions.end(), version);
        if (it == port_versions.end())
        {
            return msg::format_error(
                msgVersionDatabaseEntryMissing, msg::package_name = port_name, msg::version = version);
        }
        return PathAndLocation{
            version_paths[it - port_versions.begin()],
            "",
        };
    }
    // } FilesystemRegistryEntry::RegistryEntry

    // { GitRegistryEntry::RegistryEntry
    View<Version> GitRegistryEntry::get_port_versions() const
    {
        if (stale)
        {
            fill_data_from_path(parent.m_paths.get_filesystem(), parent.get_versions_tree_path());
            stale = false;
        }
        return port_versions;
    }

    ExpectedL<PathAndLocation> GitRegistryEntry::get_version(const Version& version) const
    {
        auto it = std::find(port_versions.begin(), port_versions.end(), version);
        if (it == port_versions.end() && stale)
        {
            fill_data_from_path(parent.m_paths.get_filesystem(), parent.get_versions_tree_path());
            stale = false;
            it = std::find(port_versions.begin(), port_versions.end(), version);
        }
        if (it == port_versions.end())
        {
            return format_version_git_entry_missing(port_name, version, port_versions);
        }

        const auto& git_tree = git_trees[it - port_versions.begin()];
        return parent.m_paths.git_checkout_object_from_remote_registry(git_tree).map(
            [this, &git_tree](Path&& p) -> PathAndLocation {
                return {
                    std::move(p),
                    Strings::concat("git+", parent.m_repo, "@", git_tree),
                };
            });
    }

    void GitRegistryEntry::fill_data_from_path(const ReadOnlyFilesystem& fs, const Path& port_versions_path) const
    {
        auto maybe_version_entries = load_versions_file(fs, VersionDbType::Git, port_versions_path, port_name);
        auto version_entries = std::move(maybe_version_entries).value_or_exit(VCPKG_LINE_INFO);

        for (auto&& version_entry : version_entries)
        {
            port_versions.push_back(version_entry.version);
            git_trees.push_back(version_entry.git_tree);
        }
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
                r.visit_in_key(version_value, pr.first, version, get_versiontag_deserializer_instance());

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

    ExpectedL<std::vector<VersionDbEntry>> load_versions_file(const ReadOnlyFilesystem& fs,
                                                              VersionDbType type,
                                                              const Path& registry_versions,
                                                              StringView port_name,
                                                              const Path& registry_root)
    {
        if (type == VersionDbType::Filesystem && registry_root.empty())
        {
            Checks::unreachable(VCPKG_LINE_INFO, "type should never = Filesystem when registry_root is empty.");
        }

        auto versions_file_path = registry_versions / relative_path_to_versions(port_name);

        if (!fs.exists(versions_file_path, IgnoreErrors{}))
        {
            return msg::format_error(msgCouldNotFindVersionDatabaseFile, msg::path = versions_file_path);
        }

        std::error_code ec;
        auto contents = fs.read_contents(versions_file_path, ec);
        if (ec)
        {
            return format_filesystem_call_error(ec, "read_contents", {versions_file_path});
        }

        auto maybe_versions_json = Json::parse(contents);
        auto versions_json = maybe_versions_json.get();
        if (!versions_json)
        {
            return LocalizedString::from_raw(maybe_versions_json.error()->to_string());
        }

        if (!versions_json->value.is_object())
        {
            return msg::format_error(msgFailedToParseNoTopLevelObj, msg::path = versions_file_path);
        }

        const auto& versions_object = versions_json->value.object(VCPKG_LINE_INFO);
        auto maybe_versions_array = versions_object.get("versions");
        if (!maybe_versions_array || !maybe_versions_array->is_array())
        {
            return msg::format_error(msgFailedToParseNoVersionsArray, msg::path = versions_file_path);
        }

        std::vector<VersionDbEntry> db_entries;
        VersionDbEntryArrayDeserializer deserializer{type, registry_root};
        // Avoid warning treated as error.
        if (maybe_versions_array != nullptr)
        {
            Json::Reader r;
            r.visit_in_key(*maybe_versions_array, "versions", db_entries, deserializer);
            if (!r.errors().empty())
            {
                return msg::format_error(msgFailedToParseVersionsFile, msg::path = versions_file_path)
                    .append_raw(Strings::join("\n", r.errors()));
            }
        }
        return db_entries;
    }

    ExpectedL<Optional<Baseline>> parse_baseline_versions(StringView contents, StringView baseline, StringView origin)
    {
        auto maybe_value = Json::parse(contents, origin);
        if (!maybe_value)
        {
            return LocalizedString::from_raw(maybe_value.error()->to_string());
        }

        auto& value = *maybe_value.get();
        if (!value.value.is_object())
        {
            return msg::format_error(msgFailedToParseNoTopLevelObj, msg::path = origin);
        }

        auto real_baseline = baseline.size() == 0 ? "default" : baseline;

        const auto& obj = value.value.object(VCPKG_LINE_INFO);
        auto baseline_value = obj.get(real_baseline);
        if (!baseline_value)
        {
            return {nullopt, expected_left_tag};
        }

        Json::Reader r;
        std::map<std::string, Version, std::less<>> result;
        r.visit_in_key(*baseline_value, real_baseline, result, BaselineDeserializer::instance);
        if (r.errors().empty())
        {
            return {std::move(result), expected_left_tag};
        }
        else
        {
            return msg::format_error(msgFailedToParseBaseline, msg::path = origin)
                .append_raw('\n')
                .append_raw(Strings::join("\n", r.errors()));
        }
    }

    ExpectedL<Optional<Baseline>> load_baseline_versions(const ReadOnlyFilesystem& fs,
                                                         const Path& baseline_path,
                                                         StringView baseline)
    {
        std::error_code ec;
        auto contents = fs.read_contents(baseline_path, ec);
        if (ec)
        {
            if (ec == std::errc::no_such_file_or_directory)
            {
                msg::println(msgFailedToFindBaseline);
                return {nullopt, expected_left_tag};
            }

            return format_filesystem_call_error(ec, "read_contents", {baseline_path});
        }

        return parse_baseline_versions(contents, baseline, baseline_path);
    }
}

namespace vcpkg
{
    LockFile::Entry LockFile::get_or_fetch(const VcpkgPaths& paths, StringView repo, StringView reference)
    {
        auto range = lockdata.equal_range(repo);
        auto it = std::find_if(range.first, range.second, [&reference](const LockDataType::value_type& repo2entry) {
            return repo2entry.second.reference == reference;
        });

        if (it == range.second)
        {
            msg::println(msgFetchingRegistryInfo, msg::url = repo, msg::value = reference);
            auto x = paths.git_fetch_from_remote_registry(repo, reference);
            it = lockdata.emplace(repo.to_string(),
                                  EntryData{reference.to_string(), x.value_or_exit(VCPKG_LINE_INFO), false});
            modified = true;
        }

        return {this, it};
    }
    void LockFile::Entry::ensure_up_to_date(const VcpkgPaths& paths) const
    {
        if (data->second.stale)
        {
            StringView repo(data->first);
            StringView reference(data->second.reference);
            msg::println(msgFetchingRegistryInfo, msg::url = repo, msg::value = reference);

            data->second.commit_id =
                paths.git_fetch_from_remote_registry(repo, reference).value_or_exit(VCPKG_LINE_INFO);
            data->second.stale = false;
            lockfile->modified = true;
        }
    }

    Registry::Registry(std::vector<std::string>&& packages, std::unique_ptr<RegistryImplementation>&& impl)
        : packages_(std::move(packages)), implementation_(std::move(impl))
    {
        Util::sort_unique_erase(packages_);
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

    size_t package_match_prefix(StringView name, StringView prefix)
    {
        if (name == prefix)
        {
            // exact match is like matching "infinity" prefix
            return SIZE_MAX;
        }

        // Note that the * is included in the match so that 0 means no match
        const auto prefix_size = prefix.size();
        if (prefix_size != 0)
        {
            const auto star_index = prefix_size - 1;
            if (prefix[star_index] == '*' && name.size() >= star_index &&
                name.substr(0, star_index) == prefix.substr(0, star_index))
            {
                return prefix_size;
            }
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
            for (auto&& package : registry.packages())
            {
                longest_prefix = std::max(longest_prefix, package_match_prefix(name, package));
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

    ExpectedL<Version> RegistrySet::baseline_for_port(StringView port_name) const
    {
        auto impl = registry_for_port(port_name);
        if (!impl) return msg::format(msg::msgErrorMessage).append(msgNoRegistryForPort, msg::package_name = port_name);
        return impl->get_baseline_version(port_name);
    }

    bool RegistrySet::is_default_builtin_registry() const
    {
        return default_registry_ && default_registry_->kind() == BuiltinFilesRegistry::s_kind;
    }
    bool RegistrySet::has_modifications() const { return !registries_.empty() || !is_default_builtin_registry(); }

    ExpectedL<std::vector<std::pair<SchemedVersion, std::string>>> get_builtin_versions(const VcpkgPaths& paths,
                                                                                        StringView port_name)
    {
        return load_versions_file(
                   paths.get_filesystem(), VersionDbType::Git, paths.builtin_registry_versions, port_name)
            .map([&](std::vector<VersionDbEntry>&& versions) {
                return Util::fmap(
                    versions, [](const VersionDbEntry& entry) -> auto{
                        return std::make_pair(SchemedVersion{entry.scheme, entry.version}, entry.git_tree);
                    });
            });
    }

    ExpectedL<Baseline> get_builtin_baseline(const VcpkgPaths& paths)
    {
        auto baseline_path = paths.builtin_registry_versions / "baseline.json";
        return load_baseline_versions(paths.get_filesystem(), baseline_path)
            .then([&](Optional<Baseline>&& b) -> ExpectedL<Baseline> {
                if (auto p = b.get())
                {
                    return std::move(*p);
                }

                return msg::format_error(msgBaselineFileNoDefaultFieldPath, msg::path = baseline_path);
            });
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

    std::unique_ptr<Json::IDeserializer<std::vector<VersionDbEntry>>> make_version_db_deserializer(VersionDbType type,
                                                                                                   const Path& root)
    {
        return std::make_unique<VersionDbEntryArrayDeserializer>(type, root);
    }
}
