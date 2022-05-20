#include <vcpkg/base/downloads.h>
#include <vcpkg/base/expected.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/git.h>
#include <vcpkg/base/hash.h>
#include <vcpkg/base/jsonreader.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.print.h>
#include <vcpkg/base/system.process.h>
#include <vcpkg/base/util.h>

#include <vcpkg/archives.h>
#include <vcpkg/binarycaching.h>
#include <vcpkg/binaryparagraph.h>
#include <vcpkg/build.h>
#include <vcpkg/commands.h>
#include <vcpkg/commands.version.h>
#include <vcpkg/configuration.h>
#include <vcpkg/documentation.h>
#include <vcpkg/globalstate.h>
#include <vcpkg/installedpaths.h>
#include <vcpkg/metrics.h>
#include <vcpkg/packagespec.h>
#include <vcpkg/registries.h>
#include <vcpkg/sourceparagraph.h>
#include <vcpkg/tools.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>
#include <vcpkg/visualstudio.h>

namespace
{
    using namespace vcpkg;

    static Path process_input_directory_impl(
        Filesystem& filesystem, const Path& root, std::string* option, StringLiteral name, LineInfo li)
    {
        if (option)
        {
            return filesystem.almost_canonical(*option, li);
        }
        else
        {
            return root / name;
        }
    }

    static Path process_input_directory(
        Filesystem& filesystem, const Path& root, std::string* option, StringLiteral name, LineInfo li)
    {
        auto result = process_input_directory_impl(filesystem, root, option, name, li);
        Debug::print("Using ", name, "-root: ", result, '\n');
        return result;
    }

    static Path process_output_directory(Filesystem& fs, const std::string* option, const Path& default_path)
    {
        return fs.almost_canonical(option ? Path(*option) : default_path, VCPKG_LINE_INFO);
    }
} // unnamed namespace

namespace vcpkg
{
    static ManifestAndPath load_manifest(const Filesystem& fs, const Path& manifest_dir)
    {
        std::error_code ec;
        auto manifest_path = manifest_dir / "vcpkg.json";
        auto manifest_opt = Json::parse_file(fs, manifest_path, ec);
        if (ec)
        {
            Checks::exit_maybe_upgrade(
                VCPKG_LINE_INFO, "Failed to load manifest from directory %s: %s", manifest_dir, ec.message());
        }

        if (!manifest_opt.has_value())
        {
            Checks::exit_maybe_upgrade(
                VCPKG_LINE_INFO, "Failed to parse manifest at %s:\n%s", manifest_path, manifest_opt.error()->format());
        }
        auto manifest_value = std::move(manifest_opt).value_or_exit(VCPKG_LINE_INFO);

        if (!manifest_value.first.is_object())
        {
            print2(Color::error,
                   "Failed to parse manifest at ",
                   manifest_path,
                   ": Manifest files must have a top-level object\n");
            Checks::exit_fail(VCPKG_LINE_INFO);
        }
        return {std::move(manifest_value.first.object()), std::move(manifest_path)};
    }

    static Optional<ManifestConfiguration> config_from_manifest(const Path& manifest_path,
                                                                const Optional<ManifestAndPath>& manifest_doc)
    {
        if (auto manifest = manifest_doc.get())
        {
            return parse_manifest_configuration(manifest_path, manifest->manifest).value_or_exit(VCPKG_LINE_INFO);
        }
        return nullopt;
    }

    static Optional<Configuration> config_from_json(const Path& config_path, const Filesystem& fs)
    {
        if (!fs.exists(config_path, VCPKG_LINE_INFO))
        {
            return nullopt;
        }

        auto parsed_config = Json::parse_file(VCPKG_LINE_INFO, fs, config_path);
        if (!parsed_config.first.is_object())
        {
            print2(
                Color::error, "Failed to parse ", config_path, ": configuration files must have a top-level object\n");
            msg::println(Color::error, msg::msgSeeURL, msg::url = docs::registries_url);
            Checks::exit_fail(VCPKG_LINE_INFO);
        }
        const auto& obj = parsed_config.first.object();

        Json::Reader reader;
        auto parsed_config_opt = reader.visit(obj, get_configuration_deserializer());
        if (!reader.errors().empty())
        {
            print2(Color::error, "Error: while parsing ", config_path, "\n");
            for (auto&& msg : reader.errors())
                print2("    ", msg, '\n');

            print2("See ", docs::registries_url, " for more information.\n");
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        return parsed_config_opt;
    }

    static ConfigurationAndSource merge_validate_configs(Optional<ManifestConfiguration>&& manifest_data,
                                                         const Path& manifest_dir,
                                                         Optional<Configuration>&& config_data,
                                                         const Path& config_dir,
                                                         const VcpkgPaths& paths)
    {
        ConfigurationAndSource ret;

        if (auto manifest = manifest_data.get())
        {
            if (auto config = manifest->config.get())
            {
                print2(Color::warning,
                       "Embedding `vcpkg-configuration` in a manifest file is an EXPERIMENTAL feature.\n");

                if (manifest->builtin_baseline && config->default_reg)
                {
                    print2(Color::error,
                           "Error: Specifying vcpkg-configuration.default-registry in a manifest file conflicts with "
                           "builtin-baseline.\nPlease remove one of these conflicting settings.\n");
                    Checks::exit_fail(VCPKG_LINE_INFO);
                }

                config->validate_as_active();

                if (config_data.has_value())
                {
                    print2(Color::error,
                           "Ambiguous vcpkg configuration provided by both manifest and configuration file.\n"
                           "-- Delete configuration file \"",
                           config_dir / "vcpkg-configuration.json",
                           "\"\n"
                           "-- Or remove \"vcpkg-configuration\" from the manifest file \"",
                           manifest_dir / "vcpkg.json",
                           "\".");
                    Checks::exit_fail(VCPKG_LINE_INFO);
                }

                ret = ConfigurationAndSource{std::move(*config), config_dir, ConfigurationSource::ManifestFile};
            }
        }

        if (auto config = config_data.get())
        {
            config->validate_as_active();

            ret = ConfigurationAndSource{std::move(*config), config_dir, ConfigurationSource::VcpkgConfigurationFile};
        }

        if (auto manifest = manifest_data.get())
        {
            if (auto p_baseline = manifest->builtin_baseline.get())
            {
                LockGuardPtr<Metrics>(g_metrics)->track_property("manifest_baseline", "defined");
                if (!is_git_commit_sha(*p_baseline))
                {
                    LockGuardPtr<Metrics>(g_metrics)->track_property("versioning-error-baseline", "defined");
                    Checks::exit_maybe_upgrade(VCPKG_LINE_INFO,
                                               "Error: the top-level builtin-baseline%s was not a valid commit sha: "
                                               "expected 40 lowercase hexadecimal characters.\n%s\n",
                                               Strings::concat(" (", *p_baseline, ')'),
                                               paths.get_git_impl().git_current_sha_message(paths.git_builtin_config(),
                                                                                            paths.git_embedded_sha()));
                }

                if (ret.config.default_reg)
                {
                    print2(Color::warning,
                           "warning: attempting to set builtin-baseline in vcpkg.json while overriding the "
                           "default-registry in vcpkg-configuration.json.\n    The default-registry from "
                           "vcpkg-configuration.json will be used.");
                }
                else
                {
                    auto& default_reg = ret.config.default_reg.emplace();
                    default_reg.kind = "builtin";
                    default_reg.baseline = std::move(*p_baseline);
                }
            }
        }

        return ret;
    }

    namespace details
    {
        namespace
        {
            const ExpectedS<Path>& default_registries_cache_path()
            {
                static auto cachepath = get_platform_cache_home().then([](Path p) -> ExpectedS<Path> {
                    auto maybe_cachepath = get_environment_variable("X_VCPKG_REGISTRIES_CACHE");
                    if (auto p_str = maybe_cachepath.get())
                    {
                        LockGuardPtr<Metrics>(g_metrics)->track_property("X_VCPKG_REGISTRIES_CACHE", "defined");
                        Path path = *p_str;
                        path.make_preferred();
                        const auto status = get_real_filesystem().status(path, VCPKG_LINE_INFO);
                        if (!vcpkg::exists(status))
                        {
                            return {"Path to X_VCPKG_REGISTRIES_CACHE does not exist: " + path.native(),
                                    expected_right_tag};
                        }

                        if (!vcpkg::is_directory(status))
                        {
                            return {"Value of environment variable X_VCPKG_REGISTRIES_CACHE is not a directory: " +
                                        path.native(),
                                    expected_right_tag};
                        }

                        if (!path.is_absolute())
                        {
                            return {"Value of environment variable X_VCPKG_REGISTRIES_CACHE is not absolute: " +
                                        path.native(),
                                    expected_right_tag};
                        }

                        return {std::move(path), expected_left_tag};
                    }

                    if (!p.is_absolute())
                    {
                        return {"default path was not absolute: " + p.native(), expected_right_tag};
                    }

                    p /= "vcpkg/registries";
                    p.make_preferred();
                    return {std::move(p), expected_left_tag};
                });
                return cachepath;
            }
        }

        struct BundleSettings
        {
            bool m_readonly = false;
            bool m_usegitregistry = false;
            Optional<std::string> m_embedded_git_sha;
        };

        static details::BundleSettings load_bundle_file(const Filesystem& fs, const Path& root)
        {
            details::BundleSettings ret;
            const auto vcpkg_bundle_file = root / "vcpkg-bundle.json";
            std::error_code ec;
            auto bundle_file = fs.read_contents(vcpkg_bundle_file, ec);
            if (!ec)
            {
                auto maybe_bundle_doc = Json::parse(bundle_file, bundle_file);
                if (auto bundle_doc = maybe_bundle_doc.get())
                {
                    const auto& first_object = bundle_doc->first.object();
                    if (auto v = first_object.get("readonly"))
                    {
                        ret.m_readonly = v->boolean();
                    }

                    if (auto v = first_object.get("usegitregistry"))
                    {
                        ret.m_usegitregistry = v->boolean();
                    }

                    if (auto v = first_object.get("embeddedsha"))
                    {
                        ret.m_embedded_git_sha = v->string().to_string();
                    }
                }
                else
                {
                    print2(Color::error, "Error: Invalid bundle definition.\n", maybe_bundle_doc.error()->format());
                    Checks::exit_fail(VCPKG_LINE_INFO);
                }
            }
            return ret;
        }

        static Optional<Path> maybe_get_tmp_path(const Filesystem& fs,
                                                 const details::BundleSettings& bundle,
                                                 const Optional<InstalledPaths>& installed,
                                                 const Path& root,
                                                 const std::string* arg_path,
                                                 StringLiteral root_subpath,
                                                 StringLiteral readonly_subpath,
                                                 LineInfo li)
        {
            if (arg_path)
            {
                return fs.almost_canonical(*arg_path, li);
            }
            else if (bundle.m_readonly)
            {
                if (auto i = installed.get())
                {
                    return fs.almost_canonical(i->vcpkg_dir() / readonly_subpath, li);
                }
                else
                {
                    return nullopt;
                }
            }
            else
            {
                return fs.almost_canonical(root / root_subpath, li);
            }
        }

        static Path compute_manifest_dir(const Filesystem& fs, const VcpkgCmdArguments& args, const Path& original_cwd)
        {
            if (args.manifests_enabled())
            {
                if (args.manifest_root_dir)
                {
                    return fs.almost_canonical(*args.manifest_root_dir, VCPKG_LINE_INFO);
                }
                else
                {
                    return fs.find_file_recursively_up(original_cwd, "vcpkg.json", VCPKG_LINE_INFO);
                }
            }
            return {};
        }

        // This structure holds members for VcpkgPathsImpl that don't require explicit initialization/destruction
        struct VcpkgPathsImplStage0
        {
            Lazy<std::vector<VcpkgPaths::TripletFile>> available_triplets;
            Lazy<ToolsetsInformation> toolsets;
            Lazy<std::map<std::string, std::string>> cmake_script_hashes;
            Lazy<std::string> ports_cmake_hash;
            Lazy<std::unique_ptr<IGit>> m_git_impl;
            Cache<Triplet, Path> m_triplets_cache;
            Optional<LockFile> m_installed_lock;
        };

        // This structure holds members that
        // 1. Do not have any inter-member dependencies
        // 2. Are const (and therefore initialized in the initializer list)
        struct VcpkgPathsImplStage1 : VcpkgPathsImplStage0
        {
            VcpkgPathsImplStage1(Filesystem& fs,
                                 const VcpkgCmdArguments& args,
                                 const Path& root,
                                 const Path& original_cwd)
                : m_fs(fs)
                , m_ff_settings(args.feature_flag_settings())
                , m_cache_root(default_registries_cache_path().value_or_exit(VCPKG_LINE_INFO))
                , m_manifest_dir(compute_manifest_dir(fs, args, original_cwd))
                , m_bundle(load_bundle_file(fs, root))
                , m_download_manager(std::make_shared<DownloadManager>(
                      parse_download_configuration(args.asset_sources_template()).value_or_exit(VCPKG_LINE_INFO)))
                , m_builtin_ports(process_output_directory(fs, args.builtin_ports_root_dir.get(), root / "ports"))
                , m_default_vs_path(args.default_visual_studio_path
                                        ? fs.almost_canonical(*args.default_visual_studio_path, VCPKG_LINE_INFO)
                                        : Path{})
                , scripts(process_input_directory(fs, root, args.scripts_root_dir.get(), "scripts", VCPKG_LINE_INFO))
            {
                Debug::print("Bundle config: readonly=",
                             m_bundle.m_readonly,
                             ", usegitregistry=",
                             m_bundle.m_usegitregistry,
                             ", embeddedsha=",
                             m_bundle.m_embedded_git_sha.value_or("nullopt"),
                             "\n");

                Debug::print("Using builtin-ports: ", m_builtin_ports, '\n');
            }

            Filesystem& m_fs;
            const FeatureFlagSettings m_ff_settings;
            const Path m_cache_root;
            const Path m_manifest_dir;
            const BundleSettings m_bundle;
            const std::shared_ptr<const DownloadManager> m_download_manager;
            const Path m_builtin_ports;
            const Path m_default_vs_path;
            const Path scripts;
        };

        static Optional<InstalledPaths> compute_installed(Filesystem& fs,
                                                          const VcpkgCmdArguments& args,
                                                          const Path& root,
                                                          const Path& manifest_dir,
                                                          const BundleSettings& bundle)
        {
            if (manifest_dir.empty())
            {
                if (!bundle.m_readonly)
                {
                    return InstalledPaths{
                        process_output_directory(fs, args.install_root_dir.get(), root / "installed")};
                }
            }
            else
            {
                return InstalledPaths{
                    process_output_directory(fs, args.install_root_dir.get(), manifest_dir / "vcpkg_installed")};
            }
            return nullopt;
        }

        static Path compute_downloads_root(const Filesystem& fs,
                                           const VcpkgCmdArguments& args,
                                           const Path& root,
                                           const details::BundleSettings& bundle)
        {
            Path ret;
            if (args.downloads_root_dir)
            {
                ret = *args.downloads_root_dir;
            }
            else if (bundle.m_readonly)
            {
                ret = get_platform_cache_home().value_or_exit(VCPKG_LINE_INFO) / "vcpkg" / "downloads";
            }
            else
            {
                ret = root / "downloads";
            }
            return fs.almost_canonical(ret, VCPKG_LINE_INFO);
        }

        struct VcpkgPathsImpl : VcpkgPathsImplStage1
        {
            VcpkgPathsImpl(Filesystem& fs, const VcpkgCmdArguments& args, const Path& root, const Path& original_cwd)
                : VcpkgPathsImplStage1(fs, args, root, original_cwd)
                , m_config_dir(m_manifest_dir.empty() ? root : m_manifest_dir)
                , m_has_configuration_file(fs.exists(m_config_dir / "vcpkg-configuration.json", VCPKG_LINE_INFO))
                , m_manifest_path(m_manifest_dir.empty() ? Path{} : m_manifest_dir / "vcpkg.json")
                , m_registries_work_tree_dir(m_cache_root / "git")
                , m_registries_dot_git_dir(m_cache_root / "git" / ".git")
                , m_registries_git_trees(m_cache_root / "git-trees")
                , downloads(compute_downloads_root(fs, args, root, m_bundle))
                , tools(downloads / "tools")
                , m_installed(compute_installed(fs, args, root, m_manifest_dir, m_bundle))
                , buildtrees(maybe_get_tmp_path(fs,
                                                m_bundle,
                                                m_installed,
                                                root,
                                                args.buildtrees_root_dir.get(),
                                                "buildtrees",
                                                "blds",
                                                VCPKG_LINE_INFO))
                , packages(maybe_get_tmp_path(fs,
                                              m_bundle,
                                              m_installed,
                                              root,
                                              args.packages_root_dir.get(),
                                              "packages",
                                              "pkgs",
                                              VCPKG_LINE_INFO))
                , m_tool_cache(get_tool_cache(fs,
                                              m_download_manager,
                                              downloads,
                                              scripts / "vcpkgTools.xml",
                                              tools,
                                              args.exact_abi_tools_versions.value_or(false) ? RequireExactVersions::YES
                                                                                            : RequireExactVersions::NO))
                , m_env_cache(m_ff_settings.compiler_tracking)
                , triplets_dirs(Util::fmap(args.overlay_triplets, [&fs](const std::string& p) {
                    return fs.almost_canonical(p, VCPKG_LINE_INFO);
                }))
            {
                if (auto i = m_installed.get())
                {
                    Debug::print("Using installed-root: ", i->root(), '\n');
                }
                Debug::print("Using buildtrees-root: ", buildtrees.value_or("nullopt"), '\n');
                Debug::print("Using packages-root: ", packages.value_or("nullopt"), '\n');

                if (!m_manifest_dir.empty())
                {
                    Debug::print("Using manifest-root: ", m_manifest_dir, '\n');

                    std::error_code ec;
                    const auto vcpkg_root_file = root / ".vcpkg-root";
                    if (args.wait_for_lock.value_or(false))
                    {
                        file_lock_handle = fs.take_exclusive_file_lock(vcpkg_root_file, ec);
                    }
                    else
                    {
                        file_lock_handle = fs.try_take_exclusive_file_lock(vcpkg_root_file, ec);
                    }

                    if (ec)
                    {
                        bool is_already_locked = ec == std::errc::device_or_resource_busy;
                        bool allow_errors = args.ignore_lock_failures.value_or(false);
                        if (is_already_locked || !allow_errors)
                        {
                            vcpkg::printf(Color::error, "Failed to take the filesystem lock on %s:\n", vcpkg_root_file);
                            vcpkg::printf(Color::error, "    %s\n", ec.message());
                            Checks::exit_fail(VCPKG_LINE_INFO);
                        }
                    }

                    m_manifest_doc = load_manifest(fs, m_manifest_dir);
                }
            }

            const Path m_config_dir;
            const bool m_has_configuration_file;
            const Path m_manifest_path;
            const Path m_registries_work_tree_dir;
            const Path m_registries_dot_git_dir;
            const Path m_registries_git_trees;
            const Path downloads;
            const Path tools;
            const Optional<InstalledPaths> m_installed;
            const Optional<Path> buildtrees;
            const Optional<Path> packages;
            const std::unique_ptr<ToolCache> m_tool_cache;
            Build::EnvCache m_env_cache;
            std::vector<Path> triplets_dirs;

            std::unique_ptr<IExclusiveFileLock> file_lock_handle;

            Optional<ManifestAndPath> m_manifest_doc;
            ConfigurationAndSource m_config;
            std::unique_ptr<RegistrySet> m_registry_set;
        };
    }

    DECLARE_AND_REGISTER_MESSAGE(VcpkgDisallowedClassicMode,
                                 (),
                                 "",
                                 "Error: Could not locate a manifest (vcpkg.json) above the current working "
                                 "directory.\nThis vcpkg distribution does not have a classic mode instance.");

    const InstalledPaths& VcpkgPaths::installed() const
    {
        if (auto i = m_pimpl->m_installed.get())
        {
            return *i;
        }
        msg::println(Color::error, msgVcpkgDisallowedClassicMode);
        Checks::exit_fail(VCPKG_LINE_INFO);
    }

    const Path& VcpkgPaths::buildtrees() const
    {
        if (auto i = m_pimpl->buildtrees.get())
        {
            return *i;
        }
        msg::println(Color::error, msgVcpkgDisallowedClassicMode);
        Checks::exit_fail(VCPKG_LINE_INFO);
    }

    const Path& VcpkgPaths::packages() const
    {
        if (auto i = m_pimpl->packages.get())
        {
            return *i;
        }
        msg::println(Color::error, msgVcpkgDisallowedClassicMode);
        Checks::exit_fail(VCPKG_LINE_INFO);
    }
    const Path& VcpkgPaths::builtin_ports_directory() const { return m_pimpl->m_builtin_ports; }

    const Optional<InstalledPaths>& VcpkgPaths::maybe_installed() const { return m_pimpl->m_installed; }
    const Optional<Path>& VcpkgPaths::maybe_buildtrees() const { return m_pimpl->buildtrees; }
    const Optional<Path>& VcpkgPaths::maybe_packages() const { return m_pimpl->packages; }

    DECLARE_AND_REGISTER_MESSAGE(
        ErrorMissingVcpkgRoot,
        (msg::url),
        "",
        "Error: Could not detect vcpkg-root. If you are trying to use a copy of vcpkg that you've built, you must "
        "define the VCPKG_ROOT environment variable to point to a cloned copy of {url}.");

    // Guaranteed to return non-empty
    static Path determine_root(const Filesystem& fs, const Path& original_cwd, const VcpkgCmdArguments& args)
    {
        Path ret;
        if (args.vcpkg_root_dir)
        {
            ret = fs.almost_canonical(*args.vcpkg_root_dir, VCPKG_LINE_INFO);
        }
        else
        {
            ret = fs.find_file_recursively_up(original_cwd, ".vcpkg-root", VCPKG_LINE_INFO);
            if (ret.empty())
            {
                ret =
                    fs.find_file_recursively_up(fs.almost_canonical(get_exe_path_of_current_process(), VCPKG_LINE_INFO),
                                                ".vcpkg-root",
                                                VCPKG_LINE_INFO);
            }
        }

        if (ret.empty())
        {
            msg::println(Color::error, msgErrorMissingVcpkgRoot, msg::url = "https://github.com/Microsoft/vcpkg");
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        return ret;
    }

    static Path preferred_current_path(const Filesystem& fs)
    {
#if defined(_WIN32)
        return vcpkg::win32_fix_path_case(fs.current_path(VCPKG_LINE_INFO));
#else
        return fs.current_path(VCPKG_LINE_INFO);
#endif
    }

    VcpkgPaths::VcpkgPaths(Filesystem& filesystem, const VcpkgCmdArguments& args)
        : original_cwd(preferred_current_path(filesystem))
        , root(determine_root(filesystem, original_cwd, args))
        // this is used during the initialization of the below public members
        , m_pimpl(std::make_unique<details::VcpkgPathsImpl>(filesystem, args, root, original_cwd))
        , scripts(m_pimpl->scripts)
        , downloads(m_pimpl->downloads)
        , tools(m_pimpl->tools)
        , builtin_registry_versions(
              process_output_directory(filesystem, args.builtin_registry_versions_dir.get(), root / "versions"))
        , prefab(root / "prefab")
        , buildsystems(scripts / "buildsystems")
        , buildsystems_msbuild_targets(buildsystems / "msbuild" / "vcpkg.targets")
        , buildsystems_msbuild_props(buildsystems / "msbuild" / "vcpkg.props")
        , ports_cmake(filesystem.almost_canonical(scripts / "ports.cmake", VCPKG_LINE_INFO))
        , triplets(filesystem.almost_canonical(root / "triplets", VCPKG_LINE_INFO))
        , community_triplets(filesystem.almost_canonical(triplets / "community", VCPKG_LINE_INFO))
    {
        Debug::print("Using vcpkg-root: ", root, '\n');
        Debug::print("Using scripts-root: ", scripts, '\n');
        Debug::print("Using builtin-registry: ", builtin_registry_versions, '\n');
        Debug::print("Using downloads-root: ", downloads, '\n');

        m_pimpl->triplets_dirs.emplace_back(triplets);
        m_pimpl->triplets_dirs.emplace_back(community_triplets);

        {
            auto maybe_manifest_config = config_from_manifest(m_pimpl->m_manifest_path, m_pimpl->m_manifest_doc);
            auto maybe_config_json = config_from_json(m_pimpl->m_config_dir / "vcpkg-configuration.json", filesystem);

            m_pimpl->m_config = merge_validate_configs(std::move(maybe_manifest_config),
                                                       m_pimpl->m_manifest_dir,
                                                       std::move(maybe_config_json),
                                                       m_pimpl->m_config_dir,
                                                       *this);

            m_pimpl->m_registry_set = m_pimpl->m_config.instantiate_registry_set(*this);
        }

        // metrics from configuration
        {
            auto default_registry = m_pimpl->m_registry_set->default_registry();
            auto other_registries = m_pimpl->m_registry_set->registries();
            LockGuardPtr<Metrics> metrics(g_metrics);
            if (default_registry)
            {
                metrics->track_property("registries-default-registry-kind", default_registry->kind().to_string());
            }
            else
            {
                metrics->track_property("registries-default-registry-kind", "disabled");
            }

            if (other_registries.size() != 0)
            {
                std::vector<StringLiteral> registry_kinds;
                for (const auto& reg : other_registries)
                {
                    registry_kinds.push_back(reg.implementation().kind());
                }
                Util::sort_unique_erase(registry_kinds);
                metrics->track_property("registries-kinds-used", Strings::join(",", registry_kinds));
            }
        }
    }

    Path VcpkgPaths::package_dir(const PackageSpec& spec) const { return this->packages() / spec.dir(); }
    Path VcpkgPaths::build_dir(const PackageSpec& spec) const { return this->buildtrees() / spec.name(); }
    Path VcpkgPaths::build_dir(const std::string& package_name) const { return this->buildtrees() / package_name; }

    Path VcpkgPaths::build_info_file_path(const PackageSpec& spec) const
    {
        return this->package_dir(spec) / "BUILD_INFO";
    }

    Path VcpkgPaths::baselines_output() const { return buildtrees() / "versioning_" / "baselines"; }
    Path VcpkgPaths::versions_output() const { return buildtrees() / "versioning_" / "versions"; }
    Path VcpkgPaths::registries_output() const { return m_pimpl->m_registries_work_tree_dir; }

    Path InstalledPaths::listfile_path(const BinaryParagraph& pgh) const
    {
        return this->vcpkg_dir_info() / (pgh.fullstem() + ".list");
    }

    bool VcpkgPaths::is_valid_triplet(Triplet t) const
    {
        const auto it = Util::find_if(this->get_available_triplets(), [&](auto&& available_triplet) {
            return t.canonical_name() == available_triplet.name;
        });
        return it != this->get_available_triplets().cend();
    }

    const std::vector<std::string> VcpkgPaths::get_available_triplets_names() const
    {
        return vcpkg::Util::fmap(this->get_available_triplets(),
                                 [](auto&& triplet_file) -> std::string { return triplet_file.name; });
    }

    const std::vector<VcpkgPaths::TripletFile>& VcpkgPaths::get_available_triplets() const
    {
        return m_pimpl->available_triplets.get_lazy([this]() -> std::vector<TripletFile> {
            std::vector<TripletFile> output;
            Filesystem& fs = this->get_filesystem();
            for (auto&& triplets_dir : m_pimpl->triplets_dirs)
            {
                for (auto&& path : fs.get_regular_files_non_recursive(triplets_dir, VCPKG_LINE_INFO))
                {
                    if (Strings::case_insensitive_ascii_equals(path.extension(), ".cmake"))
                    {
                        output.emplace_back(TripletFile(path.stem(), triplets_dir));
                    }
                }
            }

            return output;
        });
    }

    const std::map<std::string, std::string>& VcpkgPaths::get_cmake_script_hashes() const
    {
        return m_pimpl->cmake_script_hashes.get_lazy([this]() -> std::map<std::string, std::string> {
            auto& fs = this->get_filesystem();
            std::map<std::string, std::string> helpers;
            auto files = fs.get_regular_files_non_recursive(this->scripts / "cmake", VCPKG_LINE_INFO);
            for (auto&& file : files)
            {
                if (file.filename() == ".DS_Store")
                {
                    continue;
                }
                helpers.emplace(file.stem().to_string(),
                                Hash::get_file_hash(fs, file, Hash::Algorithm::Sha256).value_or_exit(VCPKG_LINE_INFO));
            }
            return helpers;
        });
    }

    StringView VcpkgPaths::get_ports_cmake_hash() const
    {
        return m_pimpl->ports_cmake_hash.get_lazy([this]() -> std::string {
            return Hash::get_file_hash(get_filesystem(), ports_cmake, Hash::Algorithm::Sha256)
                .value_or_exit(VCPKG_LINE_INFO);
        });
    }

    static LockFile::LockDataType lockdata_from_json_object(const Json::Object& obj)
    {
        LockFile::LockDataType ret;
        for (auto&& repo_to_ref_info_value : obj)
        {
            auto repo = repo_to_ref_info_value.first;
            const auto& ref_info_value = repo_to_ref_info_value.second;

            if (!ref_info_value.is_object())
            {
                Debug::print("Lockfile value for key '", repo, "' was not an object\n");
                return ret;
            }

            for (auto&& reference_to_commit : ref_info_value.object())
            {
                auto reference = reference_to_commit.first;
                const auto& commit = reference_to_commit.second;

                if (!commit.is_string())
                {
                    Debug::print("Lockfile value for key '", reference, "' was not a string\n");
                    return ret;
                }
                auto sv = commit.string();
                if (!is_git_commit_sha(sv))
                {
                    Debug::print("Lockfile value for key '", reference, "' was not a git commit sha\n");
                    return ret;
                }
                ret.emplace(repo.to_string(), LockFile::EntryData{reference.to_string(), sv.to_string(), true});
            }
        }
        return ret;
    }

    static Json::Object lockdata_to_json_object(const LockFile::LockDataType& lockdata)
    {
        Json::Object obj;
        for (auto it = lockdata.begin(); it != lockdata.end();)
        {
            const auto& repo = it->first;
            auto repo_info_range = lockdata.equal_range(repo);

            Json::Object repo_info;
            for (auto repo_it = repo_info_range.first; repo_it != repo_info_range.second; ++repo_it)
            {
                repo_info.insert(repo_it->second.reference, Json::Value::string(repo_it->second.commit_id));
            }
            repo_info.sort_keys();
            obj.insert(repo, std::move(repo_info));
            it = repo_info_range.second;
        }

        return obj;
    }

    static LockFile load_lockfile(const Filesystem& fs, const Path& p)
    {
        LockFile ret;
        std::error_code ec;
        auto maybe_lock_contents = Json::parse_file(fs, p, ec);
        if (ec)
        {
            Debug::print("Failed to load lockfile: ", ec.message(), "\n");
            return ret;
        }
        else if (auto lock_contents = maybe_lock_contents.get())
        {
            auto& doc = lock_contents->first;
            if (!doc.is_object())
            {
                Debug::print("Lockfile was not an object\n");
                return ret;
            }

            ret.lockdata = lockdata_from_json_object(doc.object());

            return ret;
        }
        else
        {
            Debug::print("Failed to load lockfile:\n", maybe_lock_contents.error()->format());
            return ret;
        }
    }

    LockFile& VcpkgPaths::get_installed_lockfile() const
    {
        if (!m_pimpl->m_installed_lock.has_value())
        {
            m_pimpl->m_installed_lock = load_lockfile(get_filesystem(), installed().lockfile_path());
        }
        return *m_pimpl->m_installed_lock.get();
    }

    void VcpkgPaths::flush_lockfile() const
    {
        // If the lock file was not loaded, no need to flush it.
        if (!m_pimpl->m_installed_lock.has_value()) return;
        // lockfile was not modified, no need to write anything to disk.
        const auto& lockfile = *m_pimpl->m_installed_lock.get();
        if (!lockfile.modified) return;

        auto obj = lockdata_to_json_object(lockfile.lockdata);

        get_filesystem().write_rename_contents(
            installed().lockfile_path(), "vcpkg-lock.json.tmp", Json::stringify(obj, {}), VCPKG_LINE_INFO);
    }

    const Path VcpkgPaths::get_triplet_file_path(Triplet triplet) const
    {
        return m_pimpl->m_triplets_cache.get_lazy(
            triplet, [&]() -> auto {
                for (const auto& triplet_dir : m_pimpl->triplets_dirs)
                {
                    auto path = triplet_dir / (triplet.canonical_name() + ".cmake");
                    if (this->get_filesystem().exists(path, IgnoreErrors{}))
                    {
                        return path;
                    }
                }

                Checks::exit_with_message(
                    VCPKG_LINE_INFO, "Error: Triplet file %s.cmake not found", triplet.canonical_name());
            });
    }

    const ToolCache& VcpkgPaths::get_tool_cache() const { return *m_pimpl->m_tool_cache; }
    const Path& VcpkgPaths::get_tool_exe(StringView tool) const { return m_pimpl->m_tool_cache->get_tool_path(tool); }
    const std::string& VcpkgPaths::get_tool_version(StringView tool) const
    {
        return m_pimpl->m_tool_cache->get_tool_version(tool);
    }

    const IGit& VcpkgPaths::get_git_impl() const
    {
        return *m_pimpl->m_git_impl.get_lazy([this]() { return make_git_from_exe(get_tool_exe(Tools::GIT)); }).get();
    }

    GitConfig VcpkgPaths::git_builtin_config() const
    {
        GitConfig conf;
        conf.git_dir = this->root / ".git";
        conf.git_work_tree = this->root;
        return conf;
    }

    GitConfig VcpkgPaths::git_registries_config() const
    {
        GitConfig conf;
        conf.git_dir = m_pimpl->m_registries_dot_git_dir;
        conf.git_work_tree = m_pimpl->m_registries_work_tree_dir;
        return conf;
    }

    Optional<std::string> VcpkgPaths::git_embedded_sha() const { return m_pimpl->m_bundle.m_embedded_git_sha; }

    std::string VcpkgPaths::get_toolver_diagnostics() const
    {
        std::string ret;
        Strings::append(ret, "    vcpkg-tool version: ", Commands::Version::version, "\n");
        if (m_pimpl->m_bundle.m_readonly)
        {
            Strings::append(ret, "    vcpkg-readonly: true\n");
        }

        if (m_pimpl->m_bundle.m_readonly && m_pimpl->m_bundle.m_embedded_git_sha)
        {
            Strings::append(ret, "    vcpkg-scripts version: ", *m_pimpl->m_bundle.m_embedded_git_sha.get(), "\n");
        }
        else
        {
            auto pretty_commit = get_git_impl().show_pretty_commit(git_builtin_config(), "HEAD");
            if (auto output = pretty_commit.get())
            {
                Strings::append(ret, "    vcpkg-scripts version: ", *output, "\n");
            }
            else
            {
                Strings::append(ret, "    vcpkg-scripts version: unknown\n");
            }
        }
        return ret;
    }

    Optional<const ManifestAndPath&> VcpkgPaths::get_manifest() const
    {
        if (auto p = m_pimpl->m_manifest_doc.get())
        {
            return *p;
        }
        return nullopt;
    }

    const ConfigurationAndSource& VcpkgPaths::get_configuration() const { return m_pimpl->m_config; }

    const RegistrySet& VcpkgPaths::get_registry_set() const
    {
        Checks::check_exit(VCPKG_LINE_INFO, m_pimpl->m_registry_set != nullptr);
        return *m_pimpl->m_registry_set;
    }
    const DownloadManager& VcpkgPaths::get_download_manager() const { return *m_pimpl->m_download_manager.get(); }

    DECLARE_AND_REGISTER_MESSAGE(ErrorVcvarsUnsupported,
                                 (msg::triplet),
                                 "",
                                 "Error: in triplet {triplet}: Use of Visual Studio's Developer Prompt is unsupported "
                                 "on non-Windows hosts.\nDefine 'VCPKG_CMAKE_SYSTEM_NAME' or "
                                 "'VCPKG_CHAINLOAD_TOOLCHAIN_FILE' in the triplet file.");

    DECLARE_AND_REGISTER_MESSAGE(ErrorNoVSInstance,
                                 (msg::triplet),
                                 "",
                                 "Error: in triplet {triplet}: Unable to find a valid Visual Studio instance");

    DECLARE_AND_REGISTER_MESSAGE(ErrorNoVSInstanceVersion, (msg::version), "", "    with toolset version {version}");

    DECLARE_AND_REGISTER_MESSAGE(ErrorNoVSInstanceFullVersion,
                                 (msg::version),
                                 "",
                                 "    with toolset version prefix {version}");

    DECLARE_AND_REGISTER_MESSAGE(ErrorNoVSInstanceAt, (msg::path), "", "     at \"{path}\"");

#if defined(_WIN32)
    static const ToolsetsInformation& get_all_toolsets(details::VcpkgPathsImpl& impl, const Filesystem& fs)
    {
        return impl.toolsets.get_lazy(
            [&fs]() -> ToolsetsInformation { return VisualStudio::find_toolset_instances_preferred_first(fs); });
    }

    static bool toolset_matches_full_version(const Toolset& t, StringView fv)
    {
        // User specification can be a prefix. Example:
        // fv = "14.25", t.full_version = "14.25.28610"
        if (!Strings::starts_with(t.full_version, fv))
        {
            return false;
        }
        return fv.size() == t.full_version.size() || t.full_version[fv.size()] == '.';
    }
#endif

    const Toolset& VcpkgPaths::get_toolset(const Build::PreBuildInfo& prebuildinfo) const
    {
        if (!prebuildinfo.using_vcvars())
        {
            static Toolset external_toolset = []() -> Toolset {
                Toolset ret;
                ret.dumpbin.clear();
                ret.supported_architectures = {ToolsetArchOption{"", get_host_processor(), get_host_processor()}};
                ret.vcvarsall.clear();
                ret.vcvarsall_options = {};
                ret.version = "external";
                ret.visual_studio_root_path.clear();
                return ret;
            }();
            return external_toolset;
        }

#if !defined(WIN32)
        msg::println(Color::error, msgErrorVcvarsUnsupported, msg::triplet = prebuildinfo.triplet);
        Checks::exit_fail(VCPKG_LINE_INFO);
#else
        const auto& toolsets_info = get_all_toolsets(*m_pimpl, get_filesystem());
        View<Toolset> vs_toolsets = toolsets_info.toolsets;

        const auto tsv = prebuildinfo.platform_toolset.get();
        const auto tsvf = prebuildinfo.platform_toolset_version.get();
        auto vsp = prebuildinfo.visual_studio_path.get();
        if (!vsp && !m_pimpl->m_default_vs_path.empty())
        {
            vsp = &m_pimpl->m_default_vs_path;
        }

        auto candidate = Util::find_if(vs_toolsets, [&](const Toolset& t) {
            return (!tsv || *tsv == t.version) && (!vsp || *vsp == t.visual_studio_root_path) &&
                   (!tsvf || toolset_matches_full_version(t, *tsvf));
        });
        if (candidate == vs_toolsets.end())
        {
            msg::println(Color::error, msgErrorNoVSInstance, msg::triplet = prebuildinfo.triplet);
            if (vsp)
            {
                msg::println(Color::error, msgErrorNoVSInstanceAt, msg::path = *vsp);
            }
            if (tsv)
            {
                msg::println(Color::error, msgErrorNoVSInstanceVersion, msg::version = *tsv);
            }
            if (tsvf)
            {
                msg::println(Color::error, msgErrorNoVSInstanceFullVersion, msg::version = *tsvf);
            }

            msg::print(Color::error, toolsets_info.get_localized_debug_info());
            Checks::exit_fail(VCPKG_LINE_INFO);
        }
        return *candidate;
#endif
    }

    const Environment& VcpkgPaths::get_action_env(const Build::AbiInfo& abi_info) const
    {
        return m_pimpl->m_env_cache.get_action_env(*this, abi_info);
    }

    const std::string& VcpkgPaths::get_triplet_info(const Build::AbiInfo& abi_info) const
    {
        return m_pimpl->m_env_cache.get_triplet_info(*this, abi_info);
    }

    const Build::CompilerInfo& VcpkgPaths::get_compiler_info(const Build::AbiInfo& abi_info) const
    {
        return m_pimpl->m_env_cache.get_compiler_info(*this, abi_info);
    }

    Filesystem& VcpkgPaths::get_filesystem() const { return m_pimpl->m_fs; }

    bool VcpkgPaths::use_git_default_registry() const { return m_pimpl->m_bundle.m_usegitregistry; }

    const FeatureFlagSettings& VcpkgPaths::get_feature_flags() const { return m_pimpl->m_ff_settings; }

    void VcpkgPaths::track_feature_flag_metrics() const
    {
        struct
        {
            StringView flag;
            bool enabled;
        } flags[] = {{VcpkgCmdArguments::MANIFEST_MODE_FEATURE, manifest_mode_enabled()}};

        LockGuardPtr<Metrics> metrics(g_metrics);
        for (const auto& flag : flags)
        {
            metrics->track_feature(flag.flag.to_string(), flag.enabled);
        }
    }

    VcpkgPaths::~VcpkgPaths() = default;
}
