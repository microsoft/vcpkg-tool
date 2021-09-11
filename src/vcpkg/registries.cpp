#include <vcpkg/base/delayed_init.h>
#include <vcpkg/base/json.h>
#include <vcpkg/base/jsonreader.h>
#include <vcpkg/base/system.debug.h>

#include <vcpkg/paragraphs.h>
#include <vcpkg/registries.h>
#include <vcpkg/sourceparagraph.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>
#include <vcpkg/versiondeserializers.h>
#include <vcpkg/versiont.h>

#include <map>

namespace
{
    using namespace vcpkg;

    using Baseline = std::map<std::string, VersionT, std::less<>>;

    static constexpr StringView registry_versions_dir_name = StringLiteral{"versions"};

    struct GitRegistry;

    struct GitRegistryEntry final : RegistryEntry
    {
        GitRegistryEntry(const GitRegistry& reg, StringView name);

        View<VersionT> get_port_versions() const override;
        ExpectedS<Path> get_path_to_version(const VcpkgPaths&, const VersionT& version) const override;

    private:
        void fill_data_from_path(const Filesystem& fs, const Path& port_versions_path) const;

        std::string port_name;

        const GitRegistry& parent;

        // Indicates whether port_versions and git_trees were filled in with stale (i.e. lock) data.
        mutable bool stale;

        // these two map port versions to git trees
        // these shall have the same size, and git_trees[i] shall be the git tree for port_versions[i]
        mutable std::vector<VersionT> port_versions;
        mutable std::vector<std::string> git_trees;
    };

    struct GitRegistry final : RegistryImplementation
    {
        GitRegistry(std::string&& repo, std::string&& baseline)
            : m_repo(std::move(repo)), m_baseline_identifier(std::move(baseline))
        {
        }

        StringLiteral kind() const override { return "git"; }

        std::unique_ptr<RegistryEntry> get_port_entry(const VcpkgPaths&, StringView) const override;

        void get_all_port_names(std::vector<std::string>&, const VcpkgPaths&) const override;

        Optional<VersionT> get_baseline_version(const VcpkgPaths&, StringView) const override;

    private:
        friend struct GitRegistryEntry;

        LockFile::Entry get_lock_entry(const VcpkgPaths& paths) const
        {
            return m_lock_entry.get(
                [this, &paths]() { return paths.get_installed_lockfile().get_or_fetch(paths, m_repo); });
        }

        Path get_versions_tree_path(const VcpkgPaths& paths) const
        {
            return m_versions_tree.get([this, &paths]() -> Path {
                auto e = get_lock_entry(paths);
                e.ensure_up_to_date(paths);
                auto maybe_tree =
                    paths.git_find_object_id_for_remote_registry_path(e.value(), registry_versions_dir_name);
                if (!maybe_tree)
                {
                    Checks::exit_with_message(
                        VCPKG_LINE_INFO,
                        "Error: could not find the git tree for `versions` in repo `%s` at commit `%s`: %s",
                        m_repo,
                        e.value(),
                        maybe_tree.error());
                }
                auto maybe_path = paths.git_checkout_object_from_remote_registry(*maybe_tree.get());
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

        VersionsTreePathResult get_stale_versions_tree_path(const VcpkgPaths& paths) const
        {
            auto e = get_lock_entry(paths);
            if (!e.stale())
            {
                return {get_versions_tree_path(paths), false};
            }
            if (!m_stale_versions_tree.has_value())
            {
                auto maybe_tree =
                    paths.git_find_object_id_for_remote_registry_path(e.value(), registry_versions_dir_name);
                if (!maybe_tree)
                {
                    // This could be caused by git gc or otherwise -- fall back to full fetch
                    return {get_versions_tree_path(paths), false};
                }
                auto maybe_path = paths.git_checkout_object_from_remote_registry(*maybe_tree.get());
                if (!maybe_path)
                {
                    // This could be caused by git gc or otherwise -- fall back to full fetch
                    return {get_versions_tree_path(paths), false};
                }
                m_stale_versions_tree = std::move(*maybe_path.get());
            }
            return {*m_stale_versions_tree.get(), true};
        }

        std::string m_repo;
        std::string m_baseline_identifier;
        DelayedInit<LockFile::Entry> m_lock_entry;
        mutable Optional<Path> m_stale_versions_tree;
        DelayedInit<Path> m_versions_tree;
        DelayedInit<Baseline> m_baseline;

        // Shared by all child GitRegistryEntry's
        mutable const VcpkgPaths* m_paths = nullptr;
    };

    struct BuiltinPortTreeRegistryEntry final : RegistryEntry
    {
        BuiltinPortTreeRegistryEntry(StringView name_, Path root_, VersionT version_)
            : name(name_.to_string()), root(root_), version(version_)
        {
        }

        View<VersionT> get_port_versions() const override { return {&version, 1}; }
        ExpectedS<Path> get_path_to_version(const VcpkgPaths&, const VersionT& v) const override
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
        VersionT version;
    };

    struct BuiltinGitRegistryEntry final : RegistryEntry
    {
        View<VersionT> get_port_versions() const override { return port_versions; }
        ExpectedS<Path> get_path_to_version(const VcpkgPaths&, const VersionT& version) const override;

        std::string port_name;

        // these two map port versions to git trees
        // these shall have the same size, and git_trees[i] shall be the git tree for port_versions[i]
        std::vector<VersionT> port_versions;
        std::vector<std::string> git_trees;
    };

    struct FilesystemRegistryEntry final : RegistryEntry
    {
        explicit FilesystemRegistryEntry(std::string&& port_name) : port_name(port_name) { }

        View<VersionT> get_port_versions() const override { return port_versions; }

        ExpectedS<Path> get_path_to_version(const VcpkgPaths& paths, const VersionT& version) const override;

        std::string port_name;

        // these two map port versions to paths
        // these shall have the same size, and paths[i] shall be the path for port_versions[i]
        std::vector<VersionT> port_versions;
        std::vector<Path> version_paths;
    };

    struct BuiltinRegistry final : RegistryImplementation
    {
        BuiltinRegistry(std::string&& baseline) : m_baseline_identifier(std::move(baseline))
        {
            Debug::print("BuiltinRegistry initialized with: \"", m_baseline_identifier, "\"\n");
        }

        StringLiteral kind() const override { return "builtin"; }

        std::unique_ptr<RegistryEntry> get_port_entry(const VcpkgPaths& paths, StringView port_name) const override;

        void get_all_port_names(std::vector<std::string>&, const VcpkgPaths&) const override;

        Optional<VersionT> get_baseline_version(const VcpkgPaths& paths, StringView port_name) const override;

        ~BuiltinRegistry() = default;

        std::string m_baseline_identifier;
        DelayedInit<Baseline> m_baseline;
    };

    struct FilesystemRegistry final : RegistryImplementation
    {
        FilesystemRegistry(Path&& path, std::string&& baseline)
            : m_path(std::move(path)), m_baseline_identifier(baseline)
        {
        }

        StringLiteral kind() const override { return "filesystem"; }

        std::unique_ptr<RegistryEntry> get_port_entry(const VcpkgPaths&, StringView) const override;

        void get_all_port_names(std::vector<std::string>&, const VcpkgPaths&) const override;

        Optional<VersionT> get_baseline_version(const VcpkgPaths&, StringView) const override;

    private:
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
    ExpectedS<Optional<Baseline>> load_baseline_versions(const VcpkgPaths& paths,
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

    // { RegistryImplementation

    // { BuiltinRegistry::RegistryImplementation
    std::unique_ptr<RegistryEntry> BuiltinRegistry::get_port_entry(const VcpkgPaths& paths, StringView port_name) const
    {
        const auto& fs = paths.get_filesystem();
        if (!m_baseline_identifier.empty())
        {
            auto versions_path = paths.builtin_registry_versions / relative_path_to_versions(port_name);
            if (fs.exists(versions_path, IgnoreErrors{}))
            {
                auto maybe_version_entries =
                    load_versions_file(fs, VersionDbType::Git, paths.builtin_registry_versions, port_name);
                Checks::check_maybe_upgrade(
                    VCPKG_LINE_INFO, maybe_version_entries.has_value(), "Error: " + maybe_version_entries.error());
                auto version_entries = std::move(maybe_version_entries).value_or_exit(VCPKG_LINE_INFO);

                auto res = std::make_unique<BuiltinGitRegistryEntry>();
                res->port_name = port_name.to_string();
                for (auto&& version_entry : version_entries)
                {
                    res->port_versions.push_back(version_entry.version);
                    res->git_trees.push_back(version_entry.git_tree);
                }
                return res;
            }
        }

        // Fall back to current available version
        auto port_directory = paths.builtin_ports_directory() / port_name;
        if (fs.exists(port_directory, IgnoreErrors{}))
        {
            auto found_scf = Paragraphs::try_load_port(fs, port_directory);
            if (auto scfp = found_scf.get())
            {
                auto& scf = *scfp;
                if (scf->core_paragraph->name == port_name)
                {
                    return std::make_unique<BuiltinPortTreeRegistryEntry>(
                        scf->core_paragraph->name, port_directory, scf->to_versiont());
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

    Optional<VersionT> BuiltinRegistry::get_baseline_version(const VcpkgPaths& paths, StringView port_name) const
    {
        if (!m_baseline_identifier.empty())
        {
            const auto& baseline = m_baseline.get([this, &paths]() -> Baseline {
                auto maybe_path = paths.git_checkout_baseline(m_baseline_identifier);
                if (!maybe_path.has_value())
                {
                    Checks::exit_with_message(
                        VCPKG_LINE_INFO, "%s\n\n%s", maybe_path.error(), paths.get_current_git_sha_baseline_message());
                }
                auto b = load_baseline_versions(paths, *maybe_path.get()).value_or_exit(VCPKG_LINE_INFO);
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

        // if a baseline is not specified, use the ports directory version
        auto port_path = paths.builtin_ports_directory() / port_name;
        auto maybe_scf = Paragraphs::try_load_port(paths.get_filesystem(), port_path);
        if (auto pscf = maybe_scf.get())
        {
            auto& scf = *pscf;
            return scf->to_versiont();
        }
        print_error_message(maybe_scf.error());
        Checks::exit_maybe_upgrade(VCPKG_LINE_INFO, "Error: failed to load port from %s", port_path);
    }

    void BuiltinRegistry::get_all_port_names(std::vector<std::string>& out, const VcpkgPaths& paths) const
    {
        const auto& fs = paths.get_filesystem();

        if (!m_baseline_identifier.empty() && fs.exists(paths.builtin_registry_versions, IgnoreErrors{}))
        {
            load_all_port_names_from_registry_versions(out, fs, paths.builtin_registry_versions);
        }
        std::error_code ec;
        auto port_directories = fs.get_directories_non_recursive(paths.builtin_ports_directory(), ec);
        Checks::check_exit(VCPKG_LINE_INFO,
                           !ec,
                           "Error: failed while enumerating the builtin ports directory %s: %s",
                           paths.builtin_ports_directory(),
                           ec.message());
        for (auto&& port_directory : port_directories)
        {
            auto filename = port_directory.filename();
            if (filename == ".DS_Store") continue;
            out.push_back(filename.to_string());
        }
    }
    // } BuiltinRegistry::RegistryImplementation

    // { FilesystemRegistry::RegistryImplementation
    Baseline parse_filesystem_baseline(const VcpkgPaths& paths, const Path& root, StringView baseline_identifier)
    {
        auto path_to_baseline = root / registry_versions_dir_name / "baseline.json";
        auto res_baseline = load_baseline_versions(paths, path_to_baseline, baseline_identifier);
        if (auto opt_baseline = res_baseline.get())
        {
            if (auto p = opt_baseline->get())
            {
                return std::move(*p);
            }

            if (baseline_identifier.size() == 0)
            {
                return {};
            }

            Checks::exit_maybe_upgrade(
                VCPKG_LINE_INFO,
                "Error: could not find explicitly specified baseline `\"%s\"` in baseline file `%s`.",
                baseline_identifier,
                path_to_baseline);
        }

        Checks::exit_maybe_upgrade(VCPKG_LINE_INFO, res_baseline.error());
    }
    Optional<VersionT> FilesystemRegistry::get_baseline_version(const VcpkgPaths& paths, StringView port_name) const
    {
        const auto& baseline = m_baseline.get(
            [this, &paths]() -> Baseline { return parse_filesystem_baseline(paths, m_path, m_baseline_identifier); });

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

    std::unique_ptr<RegistryEntry> FilesystemRegistry::get_port_entry(const VcpkgPaths& paths,
                                                                      StringView port_name) const
    {
        auto maybe_version_entries = load_versions_file(
            paths.get_filesystem(), VersionDbType::Filesystem, m_path / registry_versions_dir_name, port_name, m_path);
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

    void FilesystemRegistry::get_all_port_names(std::vector<std::string>& out, const VcpkgPaths& paths) const
    {
        load_all_port_names_from_registry_versions(out, paths.get_filesystem(), m_path / registry_versions_dir_name);
    }
    // } FilesystemRegistry::RegistryImplementation

    // { GitRegistry::RegistryImplementation
    std::unique_ptr<RegistryEntry> GitRegistry::get_port_entry(const VcpkgPaths& paths, StringView port_name) const
    {
        if (!m_paths) m_paths = &paths;
        return std::make_unique<GitRegistryEntry>(*this, port_name);
    }

    GitRegistryEntry::GitRegistryEntry(const GitRegistry& reg, StringView name)
        : port_name(name.to_string()), parent(reg)
    {
        // This guarantees that parent.m_paths has been filled before any entries are constructed.
        // The entries may then rely on (bool)parent.m_paths during their operation
        Checks::check_exit(VCPKG_LINE_INFO, parent.m_paths);
#if defined(_MSC_VER)
        __assume(parent.m_paths);
#endif
        auto vtp = parent.get_stale_versions_tree_path(*parent.m_paths);
        stale = vtp.stale;
        fill_data_from_path(parent.m_paths->get_filesystem(), vtp.p);
    }

    Optional<VersionT> GitRegistry::get_baseline_version(const VcpkgPaths& paths, StringView port_name) const
    {
        const auto& baseline = m_baseline.get([this, &paths]() -> Baseline {
            // We delay baseline validation until here to give better error messages and suggestions
            if (!is_git_commit_sha(m_baseline_identifier))
            {
                auto e = get_lock_entry(paths);
                e.ensure_up_to_date(paths);
                Checks::exit_maybe_upgrade(
                    VCPKG_LINE_INFO,
                    "Error: the git registry entry for \"%s\" must have a \"baseline\" field that is a valid git "
                    "commit SHA (40 lowercase hexadecimal characters).\n"
                    "The current HEAD of that repo is \"%s\".\n",
                    m_repo,
                    e.value());
            }

            auto path_to_baseline = Path(registry_versions_dir_name) / "baseline.json";
            auto maybe_contents = paths.git_show_from_remote_registry(m_baseline_identifier, path_to_baseline);
            if (!maybe_contents.has_value())
            {
                print2("Fetching baseline information from ", m_repo, "...\n");
                if (auto err = paths.git_fetch(m_repo, m_baseline_identifier))
                {
                    Checks::exit_with_message(
                        VCPKG_LINE_INFO,
                        "Error: Couldn't find baseline `\"%s\"` for repo %s:\n%s\nError: Failed to fetch %s:\n%s",
                        m_baseline_identifier,
                        m_repo,
                        maybe_contents.error(),
                        m_repo,
                        *err.get());
                }
                maybe_contents = paths.git_show_from_remote_registry(m_baseline_identifier, path_to_baseline);
            }

            if (!maybe_contents.has_value())
            {
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

    void GitRegistry::get_all_port_names(std::vector<std::string>& out, const VcpkgPaths& paths) const
    {
        auto versions_path = get_versions_tree_path(paths);
        load_all_port_names_from_registry_versions(out, paths.get_filesystem(), versions_path);
    }
    // } GitRegistry::RegistryImplementation

    // } RegistryImplementation

    // { RegistryEntry

    // { BuiltinRegistryEntry::RegistryEntry
    ExpectedS<Path> BuiltinGitRegistryEntry::get_path_to_version(const VcpkgPaths& paths, const VersionT& version) const
    {
        auto it = std::find(port_versions.begin(), port_versions.end(), version);
        if (it == port_versions.end())
        {
            return {Strings::concat("Error: No version entry for ",
                                    port_name,
                                    " at version ",
                                    version,
                                    ". This may be fixed by updating vcpkg to the latest master via `git "
                                    "pull`.\nAvailable versions:\n",
                                    Strings::join("",
                                                  port_versions,
                                                  [](const VersionT& v) { return Strings::concat("    ", v, "\n"); }),
                                    "\nSee `vcpkg help versioning` for more information."),
                    expected_right_tag};
        }

        const auto& git_tree = git_trees[it - port_versions.begin()];
        return paths.git_checkout_port(port_name, git_tree, paths.root / ".git");
    }
    // } BuiltinRegistryEntry::RegistryEntry

    // { FilesystemRegistryEntry::RegistryEntry
    ExpectedS<Path> FilesystemRegistryEntry::get_path_to_version(const VcpkgPaths&, const VersionT& version) const
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
    View<VersionT> GitRegistryEntry::get_port_versions() const
    {
        if (stale)
        {
            fill_data_from_path(parent.m_paths->get_filesystem(), parent.get_versions_tree_path(*parent.m_paths));
            stale = false;
        }
        return port_versions;
    }
    ExpectedS<Path> GitRegistryEntry::get_path_to_version(const VcpkgPaths& paths, const VersionT& version) const
    {
        auto it = std::find(port_versions.begin(), port_versions.end(), version);
        if (it == port_versions.end() && stale)
        {
            fill_data_from_path(parent.m_paths->get_filesystem(), parent.get_versions_tree_path(*parent.m_paths));
            stale = false;
            it = std::find(port_versions.begin(), port_versions.end(), version);
        }
        if (it == port_versions.end())
        {
            return {Strings::concat("Error: No version entry for ",
                                    port_name,
                                    " at version ",
                                    version,
                                    ".\nAvailable versions:\n",
                                    Strings::join("",
                                                  port_versions,
                                                  [](const VersionT& v) { return Strings::concat("    ", v, "\n"); }),
                                    "\nSee `vcpkg help versioning` for more information."),
                    expected_right_tag};
        }

        const auto& git_tree = git_trees[it - port_versions.begin()];
        return paths.git_checkout_object_from_remote_registry(git_tree);
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

    struct BaselineDeserializer final : Json::IDeserializer<std::map<std::string, VersionT, std::less<>>>
    {
        StringView type_name() const override { return "a baseline object"; }

        Optional<type> visit_object(Json::Reader& r, const Json::Object& obj) override
        {
            std::map<std::string, VersionT, std::less<>> result;

            for (auto pr : obj)
            {
                const auto& version_value = pr.second;
                VersionT version;
                r.visit_in_key(version_value, pr.first, version, get_versiontag_deserializer_instance());

                result.emplace(pr.first.to_string(), std::move(version));
            }

            return result;
        }

        static BaselineDeserializer instance;
    };
    BaselineDeserializer BaselineDeserializer::instance;

    struct RegistryImplDeserializer : Json::IDeserializer<std::unique_ptr<RegistryImplementation>>
    {
        constexpr static StringLiteral KIND = "kind";
        constexpr static StringLiteral BASELINE = "baseline";
        constexpr static StringLiteral PATH = "path";
        constexpr static StringLiteral REPO = "repository";

        constexpr static StringLiteral KIND_BUILTIN = "builtin";
        constexpr static StringLiteral KIND_FILESYSTEM = "filesystem";
        constexpr static StringLiteral KIND_GIT = "git";

        virtual StringView type_name() const override { return "a registry"; }
        virtual View<StringView> valid_fields() const override;

        virtual Optional<std::unique_ptr<RegistryImplementation>> visit_null(Json::Reader&) override;
        virtual Optional<std::unique_ptr<RegistryImplementation>> visit_object(Json::Reader&,
                                                                               const Json::Object&) override;

        RegistryImplDeserializer(const Path& configuration_directory) : config_directory(configuration_directory) { }

        Path config_directory;
    };
    constexpr StringLiteral RegistryImplDeserializer::KIND;
    constexpr StringLiteral RegistryImplDeserializer::BASELINE;
    constexpr StringLiteral RegistryImplDeserializer::PATH;
    constexpr StringLiteral RegistryImplDeserializer::REPO;
    constexpr StringLiteral RegistryImplDeserializer::KIND_BUILTIN;
    constexpr StringLiteral RegistryImplDeserializer::KIND_FILESYSTEM;
    constexpr StringLiteral RegistryImplDeserializer::KIND_GIT;

    struct RegistryDeserializer final : Json::IDeserializer<Registry>
    {
        constexpr static StringLiteral PACKAGES = "packages";

        virtual StringView type_name() const override { return "a registry"; }
        virtual View<StringView> valid_fields() const override;

        virtual Optional<Registry> visit_object(Json::Reader&, const Json::Object&) override;

        explicit RegistryDeserializer(const Path& configuration_directory) : impl_des(configuration_directory) { }

        RegistryImplDeserializer impl_des;
    };
    constexpr StringLiteral RegistryDeserializer::PACKAGES;

    View<StringView> RegistryImplDeserializer::valid_fields() const
    {
        static const StringView t[] = {KIND, BASELINE, PATH, REPO};
        return t;
    }
    View<StringView> valid_builtin_fields()
    {
        static const StringView t[] = {
            RegistryImplDeserializer::KIND,
            RegistryImplDeserializer::BASELINE,
            RegistryDeserializer::PACKAGES,
        };
        return t;
    }
    View<StringView> valid_filesystem_fields()
    {
        static const StringView t[] = {
            RegistryImplDeserializer::KIND,
            RegistryImplDeserializer::BASELINE,
            RegistryImplDeserializer::PATH,
            RegistryDeserializer::PACKAGES,
        };
        return t;
    }
    View<StringView> valid_git_fields()
    {
        static const StringView t[] = {
            RegistryImplDeserializer::KIND,
            RegistryImplDeserializer::BASELINE,
            RegistryImplDeserializer::REPO,
            RegistryDeserializer::PACKAGES,
        };
        return t;
    }

    Optional<std::unique_ptr<RegistryImplementation>> RegistryImplDeserializer::visit_null(Json::Reader&)
    {
        return nullptr;
    }

    Optional<std::unique_ptr<RegistryImplementation>> RegistryImplDeserializer::visit_object(Json::Reader& r,
                                                                                             const Json::Object& obj)
    {
        static Json::StringDeserializer kind_deserializer{"a registry implementation kind"};
        static Json::StringDeserializer baseline_deserializer{"a baseline"};
        std::string kind;

        r.required_object_field(type_name(), obj, KIND, kind, kind_deserializer);

        std::unique_ptr<RegistryImplementation> res;

        if (kind == KIND_BUILTIN)
        {
            std::string baseline;
            r.required_object_field("a builtin registry", obj, BASELINE, baseline, baseline_deserializer);
            if (!is_git_commit_sha(baseline))
            {
                r.add_generic_error(
                    "a builtin registry",
                    "The baseline field of builtin registries must be a git commit SHA (40 lowercase hex characters)");
            }
            r.check_for_unexpected_fields(obj, valid_builtin_fields(), "a builtin registry");
            res = std::make_unique<BuiltinRegistry>(std::move(baseline));
        }
        else if (kind == KIND_FILESYSTEM)
        {
            std::string baseline;
            r.optional_object_field(obj, BASELINE, baseline, baseline_deserializer);
            r.check_for_unexpected_fields(obj, valid_filesystem_fields(), "a filesystem registry");

            Path p;
            r.required_object_field("a filesystem registry", obj, PATH, p, Json::PathDeserializer::instance);

            res = std::make_unique<FilesystemRegistry>(config_directory / p, std::move(baseline));
        }
        else if (kind == KIND_GIT)
        {
            r.check_for_unexpected_fields(obj, valid_git_fields(), "a git registry");

            std::string repo;
            Json::StringDeserializer repo_des{"a git repository URL"};
            r.required_object_field("a git registry", obj, REPO, repo, repo_des);

            std::string baseline;
            r.required_object_field("a git registry", obj, BASELINE, baseline, baseline_deserializer);

            res = std::make_unique<GitRegistry>(std::move(repo), std::move(baseline));
        }
        else
        {
            StringLiteral valid_kinds[] = {KIND_BUILTIN, KIND_FILESYSTEM, KIND_GIT};
            r.add_generic_error(type_name(),
                                "Field \"kind\" did not have an expected value (expected one of: \"",
                                Strings::join("\", \"", valid_kinds),
                                "\", found \"",
                                kind,
                                "\").");
            return nullopt;
        }

        return std::move(res); // gcc-7 bug workaround redundant move
    }

    View<StringView> RegistryDeserializer::valid_fields() const
    {
        static const StringView t[] = {
            RegistryImplDeserializer::KIND,
            RegistryImplDeserializer::BASELINE,
            RegistryImplDeserializer::PATH,
            RegistryImplDeserializer::REPO,
            PACKAGES,
        };
        return t;
    }

    Optional<Registry> RegistryDeserializer::visit_object(Json::Reader& r, const Json::Object& obj)
    {
        auto impl = impl_des.visit_object(r, obj);

        if (!impl.has_value())
        {
            return nullopt;
        }

        static Json::ArrayDeserializer<Json::PackageNameDeserializer> package_names_deserializer{
            "an array of package names"};

        std::vector<std::string> packages;
        r.required_object_field(type_name(), obj, PACKAGES, packages, package_names_deserializer);

        return Registry{std::move(packages), std::move(impl).value_or_exit(VCPKG_LINE_INFO)};
    }

    Path relative_path_to_versions(StringView port_name)
    {
        char prefix[] = {port_name.byte_at_index(0), '-', '\0'};
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
        std::map<std::string, VersionT, std::less<>> result;
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

    ExpectedS<Optional<Baseline>> load_baseline_versions(const VcpkgPaths& paths,
                                                         const Path& baseline_path,
                                                         StringView baseline)
    {
        std::error_code ec;
        auto contents = paths.get_filesystem().read_contents(baseline_path, ec);
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
        ret.version = std::move(schemed_version.versiont);

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

    LockFile::Entry LockFile::get_or_fetch(const VcpkgPaths& paths, StringView key)
    {
        auto it = lockdata.find(key);
        if (it == lockdata.end())
        {
            print2("Fetching registry information from ", key, "...\n");
            auto x = paths.git_fetch_from_remote_registry(key, "HEAD");
            it = lockdata.emplace(key.to_string(), EntryData{x.value_or_exit(VCPKG_LINE_INFO), false}).first;
            modified = true;
        }
        return {this, it};
    }
    void LockFile::Entry::ensure_up_to_date(const VcpkgPaths& paths) const
    {
        if (data->second.stale)
        {
            print2("Fetching registry information from ", data->first, "...\n");
            data->second.value =
                paths.git_fetch_from_remote_registry(data->first, "HEAD").value_or_exit(VCPKG_LINE_INFO);
            data->second.stale = false;
            lockfile->modified = true;
        }
    }

    std::unique_ptr<Json::IDeserializer<std::unique_ptr<RegistryImplementation>>>
    get_registry_implementation_deserializer(const Path& configuration_directory)
    {
        return std::make_unique<RegistryImplDeserializer>(configuration_directory);
    }
    std::unique_ptr<Json::IDeserializer<std::vector<Registry>>> get_registry_array_deserializer(
        const Path& configuration_directory)
    {
        return std::make_unique<Json::ArrayDeserializer<RegistryDeserializer>>(
            "an array of registries", RegistryDeserializer(configuration_directory));
    }

    Registry::Registry(std::vector<std::string>&& packages, std::unique_ptr<RegistryImplementation>&& impl)
        : packages_(std::move(packages)), implementation_(std::move(impl))
    {
        Checks::check_exit(VCPKG_LINE_INFO, implementation_ != nullptr);
    }

    RegistrySet::RegistrySet() : default_registry_(std::make_unique<BuiltinRegistry>("")) { }

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

    Optional<VersionT> RegistrySet::baseline_for_port(const VcpkgPaths& paths, StringView port_name) const
    {
        auto impl = registry_for_port(port_name);
        if (!impl) return nullopt;
        return impl->get_baseline_version(paths, port_name);
    }

    void RegistrySet::add_registry(Registry&& r) { registries_.push_back(std::move(r)); }

    void RegistrySet::set_default_registry(std::unique_ptr<RegistryImplementation>&& r)
    {
        default_registry_ = std::move(r);
    }
    void RegistrySet::set_default_registry(std::nullptr_t) { default_registry_.reset(); }

    bool RegistrySet::is_default_builtin_registry() const
    {
        if (auto default_builtin_registry = dynamic_cast<BuiltinRegistry*>(default_registry_.get()))
        {
            return default_builtin_registry->m_baseline_identifier.empty();
        }
        return false;
    }
    void RegistrySet::set_default_builtin_registry_baseline(StringView baseline) const
    {
        if (auto default_builtin_registry = dynamic_cast<BuiltinRegistry*>(default_registry_.get()))
        {
            if (default_builtin_registry->m_baseline_identifier.empty())
            {
                default_builtin_registry->m_baseline_identifier.assign(baseline.begin(), baseline.end());
            }
            else
            {
                print2(Color::warning,
                       R"(warning: attempting to set builtin baseline in both vcpkg.json and vcpkg-configuration.json
    (only one of these should be used; the baseline from vcpkg-configuration.json will be used))");
            }
        }
        else if (auto default_registry = default_registry_.get())
        {
            vcpkg::printf(Color::warning,
                          "warning: the default registry has been replaced with a %s registry, but `builtin-baseline` "
                          "is specified in vcpkg.json. This field will have no effect.\n",
                          default_registry->kind());
        }
        else
        {
            print2(Color::warning,
                   "warning: the default registry has been disabled, but `builtin-baseline` is specified in "
                   "vcpkg.json. This field will have no effect.\n");
        }
    }

    bool RegistrySet::has_modifications() const
    {
        if (!registries_.empty())
        {
            return true;
        }
        if (auto builtin_reg = dynamic_cast<const BuiltinRegistry*>(default_registry_.get()))
        {
            if (builtin_reg->m_baseline_identifier.empty())
            {
                return false;
            }
            return true;
        }
        // default_registry_ is not a BuiltinRegistry
        return true;
    }

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
        return load_baseline_versions(paths, paths.builtin_registry_versions / "baseline.json")
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
}
