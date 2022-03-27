#include <vcpkg/base/delayed_init.h>
#include <vcpkg/base/json.h>
#include <vcpkg/base/jsonreader.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.print.h>

#include <vcpkg/metrics.h>
#include <vcpkg/paragraphs.h>
#include <vcpkg/registries.h>
#include <vcpkg/sourceparagraph.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>
#include <vcpkg/versiondeserializers.h>
#include <vcpkg/versions.h>

#include <map>

namespace
{
    using namespace vcpkg;

    using Baseline = std::map<std::string, Version, std::less<>>;

    static constexpr StringLiteral registry_versions_dir_name = "versions";

    struct GitRegistry;

    struct GitRegistryEntry final : RegistryEntry
    {
        GitRegistryEntry(const GitRegistry& reg, StringView name);

        View<Version> get_port_versions() const override;
        ExpectedS<Path> get_path_to_version(const Version& version) const override;

    private:
        void fill_data_from_path(const Filesystem& fs, const Path& port_versions_path) const;

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

        Optional<Version> get_baseline_version(StringView) const override;

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
                    LockGuardPtr<Metrics>(g_metrics)->track_property("registries-error-no-versions-at-commit",
                                                                     "defined");
                    Checks::exit_with_message(
                        VCPKG_LINE_INFO,
                        "Error: could not find the git tree for `versions` in repo `%s` at commit `%s`: %s",
                        m_repo,
                        e.commit_id(),
                        maybe_tree.error());
                }
                auto maybe_path = m_paths.git_checkout_object_from_remote_registry(*maybe_tree.get());
                if (!maybe_path)
                {
                    Checks::exit_with_message(VCPKG_LINE_INFO,
                                              "Error: failed to check out `versions` from repo %s: %s",
                                              m_repo,
                                              maybe_path.error());
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
        ExpectedS<Path> get_path_to_version(const Version& v) const override
        {
            if (v == version)
            {
                return root;
            }

            return {Strings::format("Error: no version entry for %s at version %s.\n"
                                    "We are currently using the version in the ports tree (%s).",
                                    name,
                                    v.to_string(),
                                    version.to_string()),
                    expected_right_tag};
        }

        std::string name;
        Path root;
        Version version;
    };

    struct BuiltinGitRegistryEntry final : RegistryEntry
    {
        BuiltinGitRegistryEntry(const VcpkgPaths& paths) : m_paths(paths) { }

        View<Version> get_port_versions() const override { return port_versions; }
        ExpectedS<Path> get_path_to_version(const Version& version) const override;

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

        ExpectedS<Path> get_path_to_version(const Version& version) const override;

        std::string port_name;
        // these two map port versions to paths
        // these shall have the same size, and paths[i] shall be the path for port_versions[i]
        std::vector<Version> port_versions;
        std::vector<Path> version_paths;
    };

    DECLARE_AND_REGISTER_MESSAGE(ErrorRequireBaseline,
                                 (),
                                 "",
                                 "Error: this vcpkg instance requires a manifest with a specified baseline in order to "
                                 "interact with ports. Please add 'builtin-baseline' to the manifest or add a "
                                 "'vcpkg-configuration.json' that redefines the default registry.\n");

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

        Optional<Version> get_baseline_version(StringView port_name) const override;

        Optional<Path> get_path_to_baseline_version(StringView port_name) const override
        {
            return m_builtin_ports_directory / port_name;
        }

        ~BuiltinFilesRegistry() = default;

        DelayedInit<Baseline> m_baseline;

    private:
        [[noreturn]] static void fail_require_baseline(LineInfo li)
        {
            msg::println(Color::error, msgErrorRequireBaseline);
            Checks::exit_fail(li);
        }

        const ParseExpected<SourceControlFile>& get_scf(const Path& path) const
        {
            return m_scfs.get_lazy(path, [this, &path]() { return Paragraphs::try_load_port(m_fs, path); });
        }

        const Filesystem& m_fs;
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

        Optional<Version> get_baseline_version(StringView port_name) const override;

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
            fail_require_baseline(VCPKG_LINE_INFO);
        }

        void get_all_port_names(std::vector<std::string>&) const override { fail_require_baseline(VCPKG_LINE_INFO); }

        Optional<Version> get_baseline_version(StringView) const override { fail_require_baseline(VCPKG_LINE_INFO); }

        Optional<Path> get_path_to_baseline_version(StringView) const override
        {
            fail_require_baseline(VCPKG_LINE_INFO);
        }

        ~BuiltinErrorRegistry() = default;

    private:
        [[noreturn]] static void fail_require_baseline(LineInfo li)
        {
            msg::println(Color::error, msgErrorRequireBaseline);
            Checks::exit_fail(li);
        }
    };
    constexpr StringLiteral BuiltinErrorRegistry::s_kind;

    struct FilesystemRegistry final : RegistryImplementation
    {
        FilesystemRegistry(const Filesystem& fs, Path&& path, std::string&& baseline)
            : m_fs(fs), m_path(std::move(path)), m_baseline_identifier(std::move(baseline))
        {
        }

        StringLiteral kind() const override { return "filesystem"; }

        std::unique_ptr<RegistryEntry> get_port_entry(StringView) const override;

        void get_all_port_names(std::vector<std::string>&) const override;

        Optional<Version> get_baseline_version(StringView) const override;

    private:
        const Filesystem& m_fs;

        Path m_path;
        std::string m_baseline_identifier;
        DelayedInit<Baseline> m_baseline;
    };

    Path relative_path_to_versions(StringView port_name);
    ExpectedS<std::vector<VersionDbEntry>> load_versions_file(const Filesystem& fs,
                                                              VersionDbType vdb,
                                                              const Path& port_versions,
                                                              StringView port_name,
                                                              const Path& registry_root = {});

    // returns nullopt if the baseline is valid, but doesn't contain the specified baseline,
    // or (equivalently) if the baseline does not exist.
    ExpectedS<Optional<Baseline>> parse_baseline_versions(StringView contents, StringView baseline, StringView origin);
    ExpectedS<Optional<Baseline>> load_baseline_versions(const Filesystem& fs,
                                                         const Path& baseline_path,
                                                         StringView identifier = {});

    void load_all_port_names_from_registry_versions(std::vector<std::string>& out,
                                                    const Filesystem& fs,
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
                    Checks::exit_with_message(
                        VCPKG_LINE_INFO, "Error: the JSON file %s must have a .json (all lowercase) extension.", file);
                }

                auto port_name = filename.substr(0, filename.size() - 5);
                if (!Json::PackageNameDeserializer::is_package_name(port_name))
                {
                    Checks::exit_maybe_upgrade(
                        VCPKG_LINE_INFO, "Error: found invalid port version file name: `%s`.", file);
                }

                out.push_back(port_name.to_string());
            }
        }
    }

    static ExpectedS<Path> git_checkout_baseline(const VcpkgPaths& paths, StringView commit_sha)
    {
        Filesystem& fs = paths.get_filesystem();
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
                    return {Strings::format(
                                "Error: while checking out baseline %s\nError: while creating directories %s: %s",
                                commit_sha,
                                destination_parent,
                                ec.message()),
                            expected_right_tag};
                }
                fs.write_contents(destination_tmp, *contents, ec);
                if (ec)
                {
                    return {Strings::format("Error: while checking out baseline %s\nError: while writing %s: %s",
                                            commit_sha,
                                            destination_tmp,
                                            ec.message()),
                            expected_right_tag};
                }
                fs.rename(destination_tmp, destination, ec);
                if (ec)
                {
                    return {Strings::format("Error: while checking out baseline %s\nError: while renaming %s to %s: %s",
                                            commit_sha,
                                            destination_tmp,
                                            destination,
                                            ec.message()),
                            expected_right_tag};
                }
            }
            else
            {
                return {Strings::format("Error: while checking out baseline from commit '%s' at subpath "
                                        "'versions/baseline.json':\n%s\nThis may be fixed by updating vcpkg to the "
                                        "latest master via `git pull` or fetching commits via `git fetch`.",
                                        commit_sha,
                                        maybe_contents.error()),
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

                Checks::exit_maybe_upgrade(VCPKG_LINE_INFO,
                                           "Error: Failed to load port from %s: names did not match: '%s' != '%s'",
                                           port_directory,
                                           port_name,
                                           scf->core_paragraph->name);
            }
        }

        return nullptr;
    }

    Optional<Version> BuiltinFilesRegistry::get_baseline_version(StringView port_name) const
    {
        // if a baseline is not specified, use the ports directory version
        auto port_path = m_builtin_ports_directory / port_name;
        const auto& maybe_scf = get_scf(port_path);
        if (auto pscf = maybe_scf.get())
        {
            return (*pscf)->to_version();
        }
        print_error_message(maybe_scf.error());
        Checks::exit_maybe_upgrade(VCPKG_LINE_INFO, "Error: failed to load port from %s", port_path);
    }

    void BuiltinFilesRegistry::get_all_port_names(std::vector<std::string>& out) const
    {
        std::error_code ec;
        auto port_directories = m_fs.get_directories_non_recursive(m_builtin_ports_directory, ec);
        Checks::check_exit(VCPKG_LINE_INFO,
                           !ec,
                           "Error: failed while enumerating the builtin ports directory %s: %s",
                           m_builtin_ports_directory,
                           ec.message());
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
            Checks::check_maybe_upgrade(
                VCPKG_LINE_INFO, maybe_version_entries.has_value(), "Error: " + maybe_version_entries.error());
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

    Optional<Version> BuiltinGitRegistry::get_baseline_version(StringView port_name) const
    {
        const auto& baseline = m_baseline.get([this]() -> Baseline {
            auto maybe_path = git_checkout_baseline(m_paths, m_baseline_identifier);
            if (!maybe_path.has_value())
            {
                Checks::exit_with_message(
                    VCPKG_LINE_INFO, "%s\n\n%s", maybe_path.error(), m_paths.get_current_git_sha_baseline_message());
            }
            auto b = load_baseline_versions(m_paths.get_filesystem(), *maybe_path.get()).value_or_exit(VCPKG_LINE_INFO);
            if (auto p = b.get())
            {
                return std::move(*p);
            }
            Checks::exit_with_message(VCPKG_LINE_INFO,
                                      "Error: The baseline file at commit %s was invalid (no \"default\" field)",
                                      m_baseline_identifier);
        });

        auto it = baseline.find(port_name);
        if (it != baseline.end())
        {
            return it->second;
        }
        return nullopt;
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
    Optional<Version> FilesystemRegistry::get_baseline_version(StringView port_name) const
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

                Checks::exit_maybe_upgrade(
                    VCPKG_LINE_INFO,
                    "Error: could not find explicitly specified baseline `\"%s\"` in baseline file `%s`.",
                    m_baseline_identifier,
                    path_to_baseline);
            }

            Checks::exit_maybe_upgrade(VCPKG_LINE_INFO, res_baseline.error());
        });

        auto it = baseline.find(port_name);
        if (it != baseline.end())
        {
            return it->second;
        }
        else
        {
            return nullopt;
        }
    }

    std::unique_ptr<RegistryEntry> FilesystemRegistry::get_port_entry(StringView port_name) const
    {
        auto maybe_version_entries =
            load_versions_file(m_fs, VersionDbType::Filesystem, m_path / registry_versions_dir_name, port_name, m_path);
        Checks::check_maybe_upgrade(
            VCPKG_LINE_INFO, maybe_version_entries.has_value(), "Error: %s", maybe_version_entries.error());
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

    Optional<Version> GitRegistry::get_baseline_version(StringView port_name) const
    {
        const auto& baseline = m_baseline.get([this]() -> Baseline {
            // We delay baseline validation until here to give better error messages and suggestions
            if (!is_git_commit_sha(m_baseline_identifier))
            {
                auto e = get_lock_entry();
                e.ensure_up_to_date(m_paths);
                Checks::exit_maybe_upgrade(
                    VCPKG_LINE_INFO,
                    "Error: the git registry entry for \"%s\" must have a \"baseline\" field that is a valid git "
                    "commit SHA (40 lowercase hexadecimal characters).\n"
                    "The current HEAD of that repo is \"%s\".\n",
                    m_repo,
                    e.commit_id());
            }

            auto path_to_baseline = Path(registry_versions_dir_name.to_string()) / "baseline.json";
            auto maybe_contents = m_paths.git_show_from_remote_registry(m_baseline_identifier, path_to_baseline);
            if (!maybe_contents.has_value())
            {
                get_lock_entry().ensure_up_to_date(m_paths);
                maybe_contents = m_paths.git_show_from_remote_registry(m_baseline_identifier, path_to_baseline);
            }
            if (!maybe_contents.has_value())
            {
                print2("Fetching baseline information from ", m_repo, "...\n");
                if (auto err = m_paths.git_fetch(m_repo, m_baseline_identifier))
                {
                    LockGuardPtr<Metrics>(g_metrics)->track_property("registries-error-could-not-find-baseline",
                                                                     "defined");
                    Checks::exit_with_message(
                        VCPKG_LINE_INFO,
                        "Error: Couldn't find baseline `\"%s\"` for repo %s:\n%s\nError: Failed to fetch %s:\n%s",
                        m_baseline_identifier,
                        m_repo,
                        maybe_contents.error(),
                        m_repo,
                        *err.get());
                }
                maybe_contents = m_paths.git_show_from_remote_registry(m_baseline_identifier, path_to_baseline);
            }

            if (!maybe_contents.has_value())
            {
                LockGuardPtr<Metrics>(g_metrics)->track_property("registries-error-could-not-find-baseline", "defined");
                Checks::exit_with_message(VCPKG_LINE_INFO,
                                          "Error: Couldn't find baseline in commit `\"%s\"` from repo %s:\n%s\n",
                                          m_baseline_identifier,
                                          m_repo,
                                          maybe_contents.error());
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
                    LockGuardPtr<Metrics>(g_metrics)->track_property("registries-error-could-not-find-baseline",
                                                                     "defined");
                    Checks::exit_maybe_upgrade(
                        VCPKG_LINE_INFO,
                        "The baseline.json from commit `\"%s\"` in the repo %s did not contain a \"default\" field.",
                        m_baseline_identifier,
                        m_repo);
                }
            }
            else
            {
                Checks::exit_with_message(VCPKG_LINE_INFO,
                                          "Error while fetching baseline `\"%s\"` from repo %s:\n%s",
                                          m_baseline_identifier,
                                          m_repo,
                                          res_baseline.error());
            }
        });

        auto it = baseline.find(port_name);
        if (it != baseline.end())
        {
            return it->second;
        }

        return nullopt;
    }

    void GitRegistry::get_all_port_names(std::vector<std::string>& out) const
    {
        auto versions_path = get_stale_versions_tree_path();
        load_all_port_names_from_registry_versions(out, m_paths.get_filesystem(), versions_path.p);
    }
    // } GitRegistry::RegistryImplementation

    // } RegistryImplementation

    // { RegistryEntry

    // { BuiltinRegistryEntry::RegistryEntry
    ExpectedS<Path> BuiltinGitRegistryEntry::get_path_to_version(const Version& version) const
    {
        auto it = std::find(port_versions.begin(), port_versions.end(), version);
        if (it == port_versions.end())
        {
            return {
                Strings::concat(
                    "Error: No version entry for ",
                    port_name,
                    " at version ",
                    version,
                    ". This may be fixed by updating vcpkg to the latest master via `git "
                    "pull`.\nAvailable versions:\n",
                    Strings::join("", port_versions, [](const Version& v) { return Strings::concat("    ", v, "\n"); }),
                    "\nSee `vcpkg help versioning` for more information."),
                expected_right_tag};
        }

        const auto& git_tree = git_trees[it - port_versions.begin()];
        return m_paths.git_checkout_port(port_name, git_tree, m_paths.root / ".git");
    }
    // } BuiltinRegistryEntry::RegistryEntry

    // { FilesystemRegistryEntry::RegistryEntry
    ExpectedS<Path> FilesystemRegistryEntry::get_path_to_version(const Version& version) const
    {
        auto it = std::find(port_versions.begin(), port_versions.end(), version);
        if (it == port_versions.end())
        {
            return Strings::concat("Error: No version entry for ", port_name, " at version ", version, ".");
        }
        return version_paths[it - port_versions.begin()];
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
    ExpectedS<Path> GitRegistryEntry::get_path_to_version(const Version& version) const
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
            return {
                Strings::concat(
                    "Error: No version entry for ",
                    port_name,
                    " at version ",
                    version,
                    ".\nAvailable versions:\n",
                    Strings::join("", port_versions, [](const Version& v) { return Strings::concat("    ", v, "\n"); }),
                    "\nSee `vcpkg help versioning` for more information."),
                expected_right_tag};
        }

        const auto& git_tree = git_trees[it - port_versions.begin()];
        return parent.m_paths.git_checkout_object_from_remote_registry(git_tree);
    }

    void GitRegistryEntry::fill_data_from_path(const Filesystem& fs, const Path& port_versions_path) const
    {
        auto maybe_version_entries = load_versions_file(fs, VersionDbType::Git, port_versions_path, port_name);
        Checks::check_maybe_upgrade(
            VCPKG_LINE_INFO, maybe_version_entries.has_value(), "Error: " + maybe_version_entries.error());
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
        StringView type_name() const override { return "a baseline object"; }

        Optional<type> visit_object(Json::Reader& r, const Json::Object& obj) override
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

        static BaselineDeserializer instance;
    };
    BaselineDeserializer BaselineDeserializer::instance;

    Path relative_path_to_versions(StringView port_name)
    {
        char prefix[] = {port_name[0], '-', '\0'};
        return Path(prefix) / port_name.to_string() + ".json";
    }

    ExpectedS<std::vector<VersionDbEntry>> load_versions_file(const Filesystem& fs,
                                                              VersionDbType type,
                                                              const Path& registry_versions,
                                                              StringView port_name,
                                                              const Path& registry_root)
    {
        Checks::check_exit(VCPKG_LINE_INFO,
                           !(type == VersionDbType::Filesystem && registry_root.empty()),
                           "Bug in vcpkg; type should never = Filesystem when registry_root is empty.");

        auto versions_file_path = registry_versions / relative_path_to_versions(port_name);

        if (!fs.exists(versions_file_path, IgnoreErrors{}))
        {
            return Strings::format("Couldn't find the versions database file: %s", versions_file_path);
        }

        std::error_code ec;
        auto contents = fs.read_contents(versions_file_path, ec);
        if (ec)
        {
            return Strings::format(
                "Error: Failed to load the versions database file %s: %s", versions_file_path, ec.message());
        }

        auto maybe_versions_json = Json::parse(std::move(contents));
        if (!maybe_versions_json.has_value())
        {
            return Strings::format(
                "Error: failed to parse versions file for `%s`: %s", port_name, maybe_versions_json.error()->format());
        }
        if (!maybe_versions_json.get()->first.is_object())
        {
            return Strings::format("Error: versions file for `%s` does not have a top level object.", port_name);
        }

        const auto& versions_object = maybe_versions_json.get()->first.object();
        auto maybe_versions_array = versions_object.get("versions");
        if (!maybe_versions_array || !maybe_versions_array->is_array())
        {
            return Strings::format("Error: versions file for `%s` does not contain a versions array.", port_name);
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
                return Strings::format(
                    "Error: failed to parse versions file for `%s`:\n%s", port_name, Strings::join("\n", r.errors()));
            }
        }
        return db_entries;
    }

    ExpectedS<Optional<Baseline>> parse_baseline_versions(StringView contents, StringView baseline, StringView origin)
    {
        auto maybe_value = Json::parse(contents, origin);
        if (!maybe_value.has_value())
        {
            return Strings::format(
                "Error: failed to parse baseline file: %s\n%s", origin, maybe_value.error()->format());
        }

        auto& value = *maybe_value.get();

        if (!value.first.is_object())
        {
            return Strings::concat("Error: baseline file ", origin, " does not have a top-level object");
        }

        auto real_baseline = baseline.size() == 0 ? "default" : baseline;

        const auto& obj = value.first.object();
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
            return Strings::format("Error: failed to parse baseline: %s\n%s", origin, Strings::join("\n", r.errors()));
        }
    }

    ExpectedS<Optional<Baseline>> load_baseline_versions(const Filesystem& fs,
                                                         const Path& baseline_path,
                                                         StringView baseline)
    {
        std::error_code ec;
        auto contents = fs.read_contents(baseline_path, ec);
        if (ec)
        {
            if (ec == std::errc::no_such_file_or_directory)
            {
                Debug::print("Failed to find baseline.json\n");
                return {nullopt, expected_left_tag};
            }

            return Strings::format("Error: failed to read baseline file \"%s\": %s", baseline_path, ec.message());
        }

        return parse_baseline_versions(std::move(contents), baseline, baseline_path);
    }
}

Optional<Path> RegistryImplementation::get_path_to_baseline_version(StringView port_name) const
{
    // This code does not defend against the files in the baseline not matching the declared baseline version.
    // However, this is only used by `Paragraphs::try_load_all_registry_ports` so it is not high-impact
    const auto baseline_version = this->get_baseline_version(port_name);
    if (auto b = baseline_version.get())
    {
        const auto port_entry = this->get_port_entry(port_name);
        if (auto p = port_entry.get())
        {
            if (auto port_path = p->get_path_to_version(*b))
            {
                return std::move(*port_path.get());
            }
        }
    }
    return nullopt;
}

namespace vcpkg
{
    constexpr StringLiteral VersionDbEntryDeserializer::GIT_TREE;
    constexpr StringLiteral VersionDbEntryDeserializer::PATH;
    StringView VersionDbEntryDeserializer::type_name() const { return "a version database entry"; }
    View<StringView> VersionDbEntryDeserializer::valid_fields() const
    {
        static const StringView u_git[] = {GIT_TREE};
        static const StringView u_path[] = {PATH};
        static const auto t_git = vcpkg::Util::Vectors::concat<StringView>(schemed_deserializer_fields(), u_git);
        static const auto t_path = vcpkg::Util::Vectors::concat<StringView>(schemed_deserializer_fields(), u_path);

        return type == VersionDbType::Git ? t_git : t_path;
    }

    Optional<VersionDbEntry> VersionDbEntryDeserializer::visit_object(Json::Reader& r, const Json::Object& obj)
    {
        VersionDbEntry ret;

        auto schemed_version = visit_required_schemed_deserializer(type_name(), r, obj);
        ret.scheme = schemed_version.scheme;
        ret.version = std::move(schemed_version.version);

        static Json::StringDeserializer git_tree_deserializer("a git object SHA");
        static Json::StringDeserializer path_deserializer("a registry path");

        switch (type)
        {
            case VersionDbType::Git:
            {
                r.required_object_field(type_name(), obj, GIT_TREE, ret.git_tree, git_tree_deserializer);
                break;
            }
            case VersionDbType::Filesystem:
            {
                std::string path_res;
                r.required_object_field(type_name(), obj, PATH, path_res, path_deserializer);
                if (!Strings::starts_with(path_res, "$/"))
                {
                    r.add_generic_error(
                        "a registry path",
                        "A registry path must start with `$` to mean the registry root; e.g., `$/foo/bar`.");
                    return nullopt;
                }

                if (Strings::contains(path_res, '\\'))
                {
                    r.add_generic_error("a registry path",
                                        "A registry path must use forward slashes as path separators.");
                    return nullopt;
                }

                if (Strings::contains(path_res, "//"))
                {
                    r.add_generic_error("a registry path", "A registry path must not have multiple slashes.");
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
                        r.add_generic_error("a registry path", "A registry path must not have 'dot' path elements.");
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
                        r.add_generic_error("a registry path",
                                            "A registry path must not have 'dot dot' path elements.");
                        return nullopt;
                    }
                }

                ret.p = registry_root / StringView{path_res}.substr(2);
                break;
            }
        }

        return ret;
    }

    StringView VersionDbEntryArrayDeserializer::type_name() const { return "an array of versions"; }

    Optional<std::vector<VersionDbEntry>> VersionDbEntryArrayDeserializer::visit_array(Json::Reader& r,
                                                                                       const Json::Array& arr)
    {
        return r.array_elements(arr, underlying);
    }

    LockFile::Entry LockFile::get_or_fetch(const VcpkgPaths& paths, StringView repo, StringView reference)
    {
        auto range = lockdata.equal_range(repo);
        auto it = std::find_if(range.first, range.second, [&reference](const LockDataType::value_type& repo2entry) {
            return repo2entry.second.reference == reference;
        });

        if (it == range.second)
        {
            print2("Fetching registry information from ", repo, " (", reference, ")...\n");
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
            print2("Fetching registry information from ", repo, " (", reference, ")...\n");

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
        for (const auto& registry : registries())
        {
            const auto& packages = registry.packages();
            if (std::find(packages.begin(), packages.end(), name) != packages.end())
            {
                return &registry.implementation();
            }
        }
        return default_registry();
    }

    Optional<Version> RegistrySet::baseline_for_port(StringView port_name) const
    {
        auto impl = registry_for_port(port_name);
        if (!impl) return nullopt;
        return impl->get_baseline_version(port_name);
    }

    bool RegistrySet::is_default_builtin_registry() const
    {
        return default_registry_ && default_registry_->kind() == BuiltinFilesRegistry::s_kind;
    }
    bool RegistrySet::has_modifications() const { return !registries_.empty() || !is_default_builtin_registry(); }

    ExpectedS<std::vector<std::pair<SchemedVersion, std::string>>> get_builtin_versions(const VcpkgPaths& paths,
                                                                                        StringView port_name)
    {
        auto maybe_versions =
            load_versions_file(paths.get_filesystem(), VersionDbType::Git, paths.builtin_registry_versions, port_name);
        if (auto pversions = maybe_versions.get())
        {
            return Util::fmap(
                *pversions, [](auto&& entry) -> auto {
                    return std::make_pair(SchemedVersion{entry.scheme, entry.version}, entry.git_tree);
                });
        }

        return maybe_versions.error();
    }

    ExpectedS<Baseline> get_builtin_baseline(const VcpkgPaths& paths)
    {
        return load_baseline_versions(paths.get_filesystem(), paths.builtin_registry_versions / "baseline.json")
            .then([&](Optional<Baseline>&& b) -> ExpectedS<Baseline> {
                if (auto p = b.get())
                {
                    return std::move(*p);
                }
                return Strings::concat(
                    "Error: The baseline file at versions/baseline.json was invalid (no \"default\" field)");
            });
    }

    bool is_git_commit_sha(StringView sv)
    {
        static constexpr struct
        {
            bool operator()(char ch) { return ('0' <= ch && ch <= '9') || ('a' <= ch && ch <= 'f'); }
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
                paths, "https://github.com/Microsoft/vcpkg", "HEAD", std::move(baseline));
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
    std::unique_ptr<RegistryImplementation> make_filesystem_registry(const Filesystem& fs,
                                                                     Path path,
                                                                     std::string baseline)
    {
        return std::make_unique<FilesystemRegistry>(fs, std::move(path), std::move(baseline));
    }
}
