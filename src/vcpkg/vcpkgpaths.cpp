#include <vcpkg/base/downloads.h>
#include <vcpkg/base/expected.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/hash.h>
#include <vcpkg/base/jsonreader.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.process.h>
#include <vcpkg/base/util.h>

#include <vcpkg/archives.h>
#include <vcpkg/binarycaching.h>
#include <vcpkg/binaryparagraph.h>
#include <vcpkg/build.h>
#include <vcpkg/bundlesettings.h>
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

    Path process_input_directory_impl(
        Filesystem& filesystem, const Path& root, const std::string* option, StringLiteral name, LineInfo li)
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

    Path process_input_directory(
        Filesystem& filesystem, const Path& root, const std::string* option, StringLiteral name, LineInfo li)
    {
        auto result = process_input_directory_impl(filesystem, root, option, name, li);
        Debug::print("Using ", name, "-root: ", result, '\n');
        return result;
    }

    Path process_output_directory(Filesystem& fs, const std::string* option, const Path& default_path)
    {
        return fs.almost_canonical(option ? Path(*option) : default_path, VCPKG_LINE_INFO);
    }

    ManifestAndPath load_manifest(const Filesystem& fs, const Path& manifest_dir)
    {
        std::error_code ec;
        auto manifest_path = manifest_dir / "vcpkg.json";
        auto manifest_opt = Json::parse_file(fs, manifest_path, ec);
        if (ec)
        {
            Checks::msg_exit_maybe_upgrade(VCPKG_LINE_INFO,
                                           msg::format(msgFailedToLoadManifest, msg::path = manifest_dir)
                                               .append_raw('\n')
                                               .append_raw(ec.message()));
        }

        if (!manifest_opt)
        {
            Checks::msg_exit_maybe_upgrade(VCPKG_LINE_INFO,
                                           LocalizedString::from_raw(manifest_opt.error()->to_string()));
        }

        auto manifest_value = std::move(manifest_opt).value_or_exit(VCPKG_LINE_INFO).value;
        if (!manifest_value.is_object())
        {
            msg::println_error(msgFailedToParseNoTopLevelObj, msg::path = manifest_path);
            Checks::exit_fail(VCPKG_LINE_INFO);
        }
        return {std::move(manifest_value).object(VCPKG_LINE_INFO), std::move(manifest_path)};
    }

    static Optional<ManifestConfiguration> config_from_manifest(const Optional<ManifestAndPath>& manifest_doc)
    {
        if (auto manifest = manifest_doc.get())
        {
            return parse_manifest_configuration(manifest->manifest, manifest->path, stdout_sink)
                .value_or_exit(VCPKG_LINE_INFO);
        }
        return nullopt;
    }

    static std::vector<std::string> merge_overlays(const std::vector<std::string>& cli_overlays,
                                                   const std::vector<std::string>& manifest_overlays,
                                                   const std::vector<std::string>& env_overlays)
    {
        std::vector<std::string> ret = cli_overlays;

        ret.insert(std::end(ret), std::begin(manifest_overlays), std::end(manifest_overlays));
        ret.insert(std::end(ret), std::begin(env_overlays), std::end(env_overlays));

        return ret;
    }

    ConfigurationAndSource merge_validate_configs(Optional<ManifestConfiguration>&& manifest_data,
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
                msg::println_warning(msgEmbeddingVcpkgConfigInManifest);

                if (manifest->builtin_baseline && config->default_reg)
                {
                    Checks::msg_exit_with_error(VCPKG_LINE_INFO, msgBaselineConflict);
                }

                config->validate_as_active();

                if (config_data.has_value())
                {
                    msg::println_error(
                        msg::format(msgAmbiguousConfigDeleteConfigFile,
                                    msg::path = config_dir / "vcpkg-configuration.json")
                            .append_raw('\n')
                            .append(msgDeleteVcpkgConfigFromManifest, msg::path = manifest_dir / "vcpkg.json"));

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
                get_global_metrics_collector().track_define(DefineMetric::ManifestBaseline);
                if (!is_git_commit_sha(*p_baseline))
                {
                    get_global_metrics_collector().track_define(DefineMetric::VersioningErrorBaseline);
                    Checks::msg_exit_maybe_upgrade(VCPKG_LINE_INFO,
                                                   msg::format(msgInvalidBuiltInBaseline, msg::value = *p_baseline)
                                                       .append_raw(paths.get_current_git_sha_baseline_message()));
                }

                if (ret.config.default_reg)
                {
                    msg::println_warning(msgAttemptingToSetBuiltInBaseline);
                }
                else
                {
                    auto& default_reg = ret.config.default_reg.emplace();
                    default_reg.kind = "builtin";
                    default_reg.baseline = std::move(*p_baseline);
                }
            }
        }

        const auto& final_config = ret.config;
        const bool has_ports_registries =
            Util::any_of(final_config.registries, [](auto&& reg) { return reg.kind != "artifact"; });
        if (has_ports_registries)
        {
            const auto default_registry = final_config.default_reg.get();
            const bool is_null_default = (default_registry) ? !default_registry->kind.has_value() : false;
            const bool has_baseline = (default_registry) ? default_registry->baseline.has_value() : false;
            if (!is_null_default && !has_baseline)
            {
                auto origin =
                    ret.directory /
                    ((ret.source == ConfigurationSource::ManifestFile) ? "vcpkg.json" : "vcpkg-configuration.json");
                msg::println_error(msgConfigurationErrorRegistriesWithoutBaseline,
                                   msg::path = origin,
                                   msg::url = vcpkg::docs::registries_url);
                Checks::exit_fail(VCPKG_LINE_INFO);
            }
        }

        return ret;
    }

    Optional<Path> maybe_get_tmp_path(const Filesystem& fs,
                                      const Optional<InstalledPaths>& installed,
                                      const Path& root,
                                      bool root_read_only,
                                      const std::string* arg_path,
                                      StringLiteral root_subpath,
                                      StringLiteral readonly_subpath,
                                      LineInfo li)
    {
        if (arg_path)
        {
            return fs.almost_canonical(*arg_path, li);
        }
        else if (root_read_only)
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

    Path compute_manifest_dir(const Filesystem& fs, const VcpkgCmdArguments& args, const Path& original_cwd)
    {
        if (auto manifest_root_dir = args.manifest_root_dir.get())
        {
            return fs.almost_canonical(*manifest_root_dir, VCPKG_LINE_INFO);
        }
        else
        {
            return fs.find_file_recursively_up(original_cwd, "vcpkg.json", VCPKG_LINE_INFO);
        }
    }

    // This structure holds members for VcpkgPathsImpl that don't require explicit initialization/destruction
    struct VcpkgPathsImplStage0
    {
        Lazy<std::vector<TripletFile>> available_triplets;
        Lazy<ToolsetsInformation> toolsets;
        Lazy<std::map<std::string, std::string>> cmake_script_hashes;
        Lazy<std::string> ports_cmake_hash;
        Cache<Triplet, Path> m_triplets_cache;
        Optional<vcpkg::LockFile> m_installed_lock;
    };

    Path compute_registries_cache_root(const Filesystem& fs, const VcpkgCmdArguments& args)
    {
        Path ret;
        if (auto registries_cache_dir = args.registries_cache_dir.get())
        {
            get_global_metrics_collector().track_define(DefineMetric::X_VcpkgRegistriesCache);
            ret = *registries_cache_dir;
            const auto status = get_real_filesystem().status(ret, VCPKG_LINE_INFO);

            if (!vcpkg::is_directory(status))
            {
                Checks::msg_exit_with_message(
                    VCPKG_LINE_INFO, msgVcpkgRegistriesCacheIsNotDirectory, msg::path = ret.native());
            }

            if (!ret.is_absolute())
            {
                Checks::msg_exit_with_message(VCPKG_LINE_INFO, msgPathMustBeAbsolute, msg::path = ret.native());
            }
        }
        else
        {
            ret = get_platform_cache_home().value_or_exit(VCPKG_LINE_INFO) / "vcpkg" / "registries";
        }

        return fs.almost_canonical(ret, VCPKG_LINE_INFO);
    }

    // This structure holds members that
    // 1. Do not have any inter-member dependencies
    // 2. Are const (and therefore initialized in the initializer list)
    struct VcpkgPathsImplStage1 : VcpkgPathsImplStage0
    {
        VcpkgPathsImplStage1(Filesystem& fs,
                             const VcpkgCmdArguments& args,
                             const BundleSettings& bundle,
                             const Path& root,
                             const Path& original_cwd)
            : m_fs(fs)
            , m_ff_settings(args.feature_flag_settings())
            , m_manifest_dir(compute_manifest_dir(fs, args, original_cwd))
            , m_bundle(bundle)
            , m_download_manager(std::make_shared<DownloadManager>(
                  parse_download_configuration(fs, args.asset_sources_template()).value_or_exit(VCPKG_LINE_INFO)))
            , m_builtin_ports(process_output_directory(fs, args.builtin_ports_root_dir.get(), root / "ports"))
            , m_default_vs_path(args.default_visual_studio_path
                                    .map([&fs](const std::string& default_visual_studio_path) {
                                        return fs.almost_canonical(default_visual_studio_path, VCPKG_LINE_INFO);
                                    })
                                    .value_or(Path{}))
            , scripts(process_input_directory(fs, root, args.scripts_root_dir.get(), "scripts", VCPKG_LINE_INFO))
            , m_registries_cache(compute_registries_cache_root(fs, args))
        {
            Debug::println("Using builtin-ports: ", m_builtin_ports);
        }

        Filesystem& m_fs;
        const FeatureFlagSettings m_ff_settings;
        const Path m_manifest_dir;
        const BundleSettings m_bundle;
        const std::shared_ptr<const DownloadManager> m_download_manager;
        const Path m_builtin_ports;
        const Path m_default_vs_path;
        const Path scripts;
        const Path m_registries_cache;
    };

    Optional<InstalledPaths> compute_installed(
        Filesystem& fs, const VcpkgCmdArguments& args, const Path& root, bool root_read_only, const Path& manifest_dir)
    {
        if (manifest_dir.empty())
        {
            if (!root_read_only)
            {
                return InstalledPaths{process_output_directory(fs, args.install_root_dir.get(), root / "installed")};
            }
        }
        else
        {
            return InstalledPaths{
                process_output_directory(fs, args.install_root_dir.get(), manifest_dir / "vcpkg_installed")};
        }
        return nullopt;
    }

    Path compute_downloads_root(const Filesystem& fs,
                                const VcpkgCmdArguments& args,
                                const Path& root,
                                bool root_read_only)
    {
        Path ret;
        if (auto downloads_root_dir = args.downloads_root_dir.get())
        {
            ret = *downloads_root_dir;
        }
        else if (root_read_only)
        {
            ret = get_platform_cache_home().value_or_exit(VCPKG_LINE_INFO) / "vcpkg" / "downloads";
        }
        else
        {
            ret = root / "downloads";
        }

        return fs.almost_canonical(ret, VCPKG_LINE_INFO);
    }

    // Guaranteed to return non-empty
    Path determine_root(const Filesystem& fs, const Path& original_cwd, const VcpkgCmdArguments& args)
    {
        Path ret;
        if (auto vcpkg_root_dir_arg = args.vcpkg_root_dir_arg.get())
        {
            ret = fs.almost_canonical(*vcpkg_root_dir_arg, VCPKG_LINE_INFO);
        }
        else
        {
            const auto canonical_current_exe = fs.almost_canonical(get_exe_path_of_current_process(), VCPKG_LINE_INFO);
            ret = fs.find_file_recursively_up(original_cwd, ".vcpkg-root", VCPKG_LINE_INFO);
            if (ret.empty())
            {
                ret = fs.find_file_recursively_up(canonical_current_exe, ".vcpkg-root", VCPKG_LINE_INFO);
            }

            if (auto vcpkg_root_dir_env = args.vcpkg_root_dir_env.get())
            {
                auto canonical_root_dir_env = fs.almost_canonical(*vcpkg_root_dir_env, VCPKG_LINE_INFO);
                if (ret.empty())
                {
                    ret = std::move(canonical_root_dir_env);
                }
                else if (ret != canonical_root_dir_env)
                {
                    msg::println_warning(msgIgnoringVcpkgRootEnvironment,
                                         msg::path = *vcpkg_root_dir_env,
                                         msg::actual = ret,
                                         msg::value = canonical_current_exe);
                }
            }
        }

        if (ret.empty())
        {
            Checks::msg_exit_with_error(VCPKG_LINE_INFO, msgErrorMissingVcpkgRoot);
        }

        return ret;
    }

    Path preferred_current_path(const Filesystem& fs)
    {
#if defined(_WIN32)
        return vcpkg::win32_fix_path_case(fs.current_path(VCPKG_LINE_INFO));
#else
        return fs.current_path(VCPKG_LINE_INFO);
#endif
    }

    LockFile::LockDataType lockdata_from_json_object(const Json::Object& obj)
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

            for (auto&& reference_to_commit : ref_info_value.object(VCPKG_LINE_INFO))
            {
                auto reference = reference_to_commit.first;
                const auto& commit = reference_to_commit.second;

                if (!commit.is_string())
                {
                    Debug::print("Lockfile value for key '", reference, "' was not a string\n");
                    return ret;
                }
                auto sv = commit.string(VCPKG_LINE_INFO);
                if (!is_git_commit_sha(sv))
                {
                    Debug::print("Lockfile value for key '", reference, "' was not a string\n");
                    return ret;
                }
                ret.emplace(repo.to_string(), LockFile::EntryData{reference.to_string(), sv.to_string(), true});
            }
        }
        return ret;
    }

    Json::Object lockdata_to_json_object(const LockFile::LockDataType& lockdata)
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

    vcpkg::LockFile load_lockfile(const Filesystem& fs, const Path& p)
    {
        vcpkg::LockFile ret;
        std::error_code ec;
        auto maybe_lock_contents = Json::parse_file(fs, p, ec);
        if (ec)
        {
            Debug::print("Failed to load lockfile: ", ec.message(), "\n");
            return ret;
        }
        else if (auto lock_contents = maybe_lock_contents.get())
        {
            auto& doc = lock_contents->value;
            if (!doc.is_object())
            {
                Debug::print("Lockfile was not an object\n");
                return ret;
            }

            ret.lockdata = lockdata_from_json_object(doc.object(VCPKG_LINE_INFO));

            return ret;
        }
        else
        {
            Debug::print("Failed to load lockfile:\n", maybe_lock_contents.error()->to_string());
            return ret;
        }
    }
} // unnamed namespace

namespace vcpkg
{
    Path InstalledPaths::listfile_path(const BinaryParagraph& pgh) const
    {
        return this->vcpkg_dir_info() / (pgh.fullstem() + ".list");
    }

    struct VcpkgPathsImpl : VcpkgPathsImplStage1
    {
        VcpkgPathsImpl(Filesystem& fs,
                       const VcpkgCmdArguments& args,
                       const BundleSettings bundle,
                       const Path& root,
                       const Path& original_cwd)
            : VcpkgPathsImplStage1(fs, args, bundle, root, original_cwd)
            , m_config_dir(m_manifest_dir.empty() ? root : m_manifest_dir)
            , m_manifest_path(m_manifest_dir.empty() ? Path{} : m_manifest_dir / "vcpkg.json")
            , m_registries_work_tree_dir(m_registries_cache / "git")
            , m_registries_dot_git_dir(m_registries_cache / "git" / ".git")
            , m_registries_git_trees(m_registries_cache / "git-trees")
            , downloads(compute_downloads_root(fs, args, root, bundle.read_only))
            , tools(downloads / "tools")
            , m_installed(compute_installed(fs, args, root, bundle.read_only, m_manifest_dir))
            , buildtrees(maybe_get_tmp_path(fs,
                                            m_installed,
                                            root,
                                            m_bundle.read_only,
                                            args.buildtrees_root_dir.get(),
                                            "buildtrees",
                                            "blds",
                                            VCPKG_LINE_INFO))
            , packages(maybe_get_tmp_path(fs,
                                          m_installed,
                                          root,
                                          m_bundle.read_only,
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
            , triplets_dirs()
            , m_artifacts_dir(downloads / "artifacts")
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
                if (!args.do_not_take_lock)
                {
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
                            msg::println_error(msgFailedToTakeFileSystemLock, msg::path = vcpkg_root_file);
                            msg::write_unlocalized_text_to_stdout(Color::error, fmt::format("    {}\n", ec.message()));
                            Checks::exit_fail(VCPKG_LINE_INFO);
                        }
                    }
                }

                m_manifest_doc = load_manifest(fs, m_manifest_dir);
            }
        }

        const Path m_config_dir;
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
        EnvCache m_env_cache;
        std::vector<Path> triplets_dirs;
        const Path m_artifacts_dir;

        std::unique_ptr<IExclusiveFileLock> file_lock_handle;

        Optional<ManifestAndPath> m_manifest_doc;
        ConfigurationAndSource m_config;
    };

    VcpkgPaths::VcpkgPaths(Filesystem& filesystem, const VcpkgCmdArguments& args, const BundleSettings& bundle)
        : original_cwd(preferred_current_path(filesystem))
        , root(determine_root(filesystem, original_cwd, args))
        // this is used during the initialization of the below public members
        , m_pimpl(std::make_unique<VcpkgPathsImpl>(filesystem, args, bundle, root, original_cwd))
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

        {
            const auto config_path = m_pimpl->m_config_dir / "vcpkg-configuration.json";
            auto maybe_manifest_config = config_from_manifest(m_pimpl->m_manifest_doc);
            auto maybe_json_config = !filesystem.exists(config_path, IgnoreErrors{})
                                         ? nullopt
                                         : parse_configuration(filesystem.read_contents(config_path, IgnoreErrors{}),
                                                               config_path,
                                                               stdout_sink);

            m_pimpl->m_config = merge_validate_configs(std::move(maybe_manifest_config),
                                                       m_pimpl->m_manifest_dir,
                                                       std::move(maybe_json_config),
                                                       m_pimpl->m_config_dir,
                                                       *this);

            auto resolve_relative_to_config = [&](const std::string& overlay_path) {
                return (m_pimpl->m_config.directory / overlay_path).native();
            };

            if (!m_pimpl->m_config.directory.empty())
            {
                auto& config = m_pimpl->m_config.config;
                Util::transform(config.overlay_ports, resolve_relative_to_config);
                Util::transform(config.overlay_triplets, resolve_relative_to_config);
            }

            overlay_ports =
                merge_overlays(args.cli_overlay_ports, m_pimpl->m_config.config.overlay_ports, args.env_overlay_ports);
            overlay_triplets = merge_overlays(
                args.cli_overlay_triplets, m_pimpl->m_config.config.overlay_triplets, args.env_overlay_triplets);
        }

        for (const std::string& triplet : this->overlay_triplets)
        {
            m_pimpl->triplets_dirs.emplace_back(filesystem.almost_canonical(triplet, VCPKG_LINE_INFO));
        }
        m_pimpl->triplets_dirs.emplace_back(triplets);
        m_pimpl->triplets_dirs.emplace_back(community_triplets);
    }

    VcpkgPaths::~VcpkgPaths() = default;

    Path VcpkgPaths::package_dir(const PackageSpec& spec) const { return this->packages() / spec.dir(); }
    Path VcpkgPaths::build_dir(const PackageSpec& spec) const { return this->buildtrees() / spec.name(); }
    Path VcpkgPaths::build_dir(StringView package_name) const { return this->buildtrees() / package_name.to_string(); }
    Path VcpkgPaths::build_info_file_path(const PackageSpec& spec) const
    {
        return this->package_dir(spec) / "BUILD_INFO";
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

    const std::vector<TripletFile>& VcpkgPaths::get_available_triplets() const
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
                        output.emplace_back(path.stem(), triplets_dir);
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

    const Path VcpkgPaths::get_triplet_file_path(Triplet triplet) const
    {
        return m_pimpl->m_triplets_cache.get_lazy(
            triplet, [&]() -> auto{
                for (const auto& triplet_dir : m_pimpl->triplets_dirs)
                {
                    auto path = triplet_dir / (triplet.canonical_name() + ".cmake");
                    if (this->get_filesystem().exists(path, IgnoreErrors{}))
                    {
                        return path;
                    }
                }

                Checks::msg_exit_with_message(
                    VCPKG_LINE_INFO, msgTripletFileNotFound, msg::triplet = triplet.canonical_name());
            });
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
            installed().lockfile_path(), "vcpkg-lock.json.tmp", Json::stringify(obj), VCPKG_LINE_INFO);
    }
    const Optional<InstalledPaths>& VcpkgPaths::maybe_installed() const { return m_pimpl->m_installed; }
    const Optional<Path>& VcpkgPaths::maybe_buildtrees() const { return m_pimpl->buildtrees; }
    const Optional<Path>& VcpkgPaths::maybe_packages() const { return m_pimpl->packages; }

    const InstalledPaths& VcpkgPaths::installed() const
    {
        if (auto i = m_pimpl->m_installed.get())
        {
            return *i;
        }

        Checks::msg_exit_with_error(VCPKG_LINE_INFO, msgVcpkgDisallowedClassicMode);
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

    Path VcpkgPaths::baselines_output() const { return buildtrees() / "versioning_" / "baselines"; }
    Path VcpkgPaths::versions_output() const { return buildtrees() / "versioning_" / "versions"; }

    std::string VcpkgPaths::get_toolver_diagnostics() const
    {
        std::string ret;
        Strings::append(ret, "    vcpkg-tool version: ", Commands::Version::version, "\n");
        if (m_pimpl->m_bundle.read_only)
        {
            Strings::append(ret, "    vcpkg-readonly: true\n");
            const auto sha = get_current_git_sha();
            Strings::append(ret, "    vcpkg-scripts version: ", sha ? StringView(*sha.get()) : "unknown", "\n");
        }
        else
        {
            const auto dot_git_dir = root / ".git";
            Command showcmd = git_cmd_builder(dot_git_dir, dot_git_dir)
                                  .string_arg("show")
                                  .string_arg("--pretty=format:%h %cd (%cr)")
                                  .string_arg("-s")
                                  .string_arg("--date=short")
                                  .string_arg("HEAD");

            const auto maybe_output = flatten_out(cmd_execute_and_capture_output(showcmd), Tools::GIT);
            if (const auto output = maybe_output.get())
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

    Filesystem& VcpkgPaths::get_filesystem() const { return m_pimpl->m_fs; }
    const DownloadManager& VcpkgPaths::get_download_manager() const { return *m_pimpl->m_download_manager; }
    const ToolCache& VcpkgPaths::get_tool_cache() const { return *m_pimpl->m_tool_cache; }
    const Path& VcpkgPaths::get_tool_exe(StringView tool, MessageSink& status_messages) const
    {
        return m_pimpl->m_tool_cache->get_tool_path(tool, status_messages);
    }
    const std::string& VcpkgPaths::get_tool_version(StringView tool, MessageSink& status_messages) const
    {
        return m_pimpl->m_tool_cache->get_tool_version(tool, status_messages);
    }

    GitConfig VcpkgPaths::git_builtin_config() const
    {
        GitConfig conf;
        conf.git_exe = get_tool_exe(Tools::GIT, stdout_sink);
        conf.git_dir = this->root / ".git";
        conf.git_work_tree = this->root;
        return conf;
    }

    Command VcpkgPaths::git_cmd_builder(const Path& dot_git_dir, const Path& work_tree) const
    {
        Command ret(get_tool_exe(Tools::GIT, stdout_sink));
        if (!dot_git_dir.empty())
        {
            ret.string_arg(Strings::concat("--git-dir=", dot_git_dir));
        }
        if (!work_tree.empty())
        {
            ret.string_arg(Strings::concat("--work-tree=", work_tree));
        }
        ret.string_arg("-c").string_arg("core.autocrlf=false");
        return ret;
    }

    ExpectedL<std::string> VcpkgPaths::get_current_git_sha() const
    {
        if (auto sha = m_pimpl->m_bundle.embedded_git_sha.get())
        {
            return {*sha, expected_left_tag};
        }
        auto cmd = git_cmd_builder(this->root / ".git", this->root);
        cmd.string_arg("rev-parse").string_arg("HEAD");
        return flatten_out(cmd_execute_and_capture_output(cmd), Tools::GIT).map([](std::string&& output) {
            return Strings::trim(std::move(output));
        });
    }

    LocalizedString VcpkgPaths::get_current_git_sha_baseline_message() const
    {
        const auto& git_config = git_builtin_config();
        if (is_shallow_clone(git_config).value_or(false))
        {
            return msg::format(msgShallowRepositoryDetected, msg::path = git_config.git_dir);
        }

        auto maybe_cur_sha = get_current_git_sha();
        if (auto p_sha = maybe_cur_sha.get())
        {
            return msg::format(msgCurrentCommitBaseline, msg::commit_sha = *p_sha);
        }
        else
        {
            return msg::format(msgFailedToDetermineCurrentCommit).append_raw('\n').append_raw(maybe_cur_sha.error());
        }
    }

    ExpectedL<Path> VcpkgPaths::git_checkout_port(StringView port_name,
                                                  StringView git_tree,
                                                  const Path& dot_git_dir) const
    {
        /* Check out a git tree into the versioned port recipes folder
         *
         * Since we are checking a git tree object, all files will be checked out to the root of `work-tree`.
         * Because of that, it makes sense to use the git hash as the name for the directory.
         */
        Filesystem& fs = get_filesystem();
        auto destination = this->versions_output() / port_name / git_tree;
        if (fs.exists(destination, IgnoreErrors{}))
        {
            return destination;
        }

        const auto destination_tmp = this->versions_output() / port_name / Strings::concat(git_tree, ".tmp");
        const auto destination_tar = this->versions_output() / port_name / Strings::concat(git_tree, ".tar");
        std::error_code ec;
        Path failure_point;
        fs.remove_all(destination_tmp, ec, failure_point);
        if (ec)
        {
            return msg::format(msg::msgErrorMessage)
                .append(format_filesystem_call_error(ec, "remove_all", {destination_tmp}))
                .append_raw('\n')
                .append(msg::msgNoteMessage)
                .append(msgWhileCheckingOutPortTreeIsh, msg::package_name = port_name, msg::commit_sha = git_tree);
        }
        fs.create_directories(destination_tmp, ec);
        if (ec)
        {
            return msg::format(msg::msgErrorMessage)
                .append(format_filesystem_call_error(ec, "create_directories", {destination_tmp}))
                .append_raw('\n')
                .append(msg::msgNoteMessage)
                .append(msgWhileCheckingOutPortTreeIsh, msg::package_name = port_name, msg::commit_sha = git_tree);
        }

        auto tar_cmd_builder = git_cmd_builder(dot_git_dir, dot_git_dir)
                                   .string_arg("archive")
                                   .string_arg(git_tree)
                                   .string_arg("-o")
                                   .string_arg(destination_tar);
        auto maybe_tar_output = flatten(cmd_execute_and_capture_output(tar_cmd_builder), Tools::TAR);
        if (!maybe_tar_output)
        {
            auto message = msg::format_error(msgGitCommandFailed, msg::command_line = tar_cmd_builder.command_line());
            const auto& git_config = git_builtin_config();
            if (is_shallow_clone(git_config).value_or(false))
            {
                message.append_raw('\n').append(msgShallowRepositoryDetected, msg::path = git_config.git_dir);
            }

            message.append_raw('\n')
                .append(msg::msgNoteMessage)
                .append(msgWhileCheckingOutPortTreeIsh, msg::package_name = port_name, msg::commit_sha = git_tree)
                .append_raw('\n')
                .append(maybe_tar_output.error());

            return message;
        }

        extract_tar_cmake(this->get_tool_exe(Tools::CMAKE, stdout_sink), destination_tar, destination_tmp);
        fs.remove(destination_tar, ec);
        if (ec)
        {
            return msg::format(msg::msgErrorMessage)
                .append(format_filesystem_call_error(ec, "remove", {destination_tar}))
                .append_raw('\n')
                .append(msg::msgNoteMessage)
                .append(msgWhileCheckingOutPortTreeIsh, msg::package_name = port_name, msg::commit_sha = git_tree);
        }
        fs.rename_with_retry(destination_tmp, destination, ec);
        if (ec)
        {
            return msg::format(msg::msgErrorMessage)
                .append(format_filesystem_call_error(ec, "rename_with_retry", {destination_tmp, destination}))
                .append_raw('\n')
                .append(msg::msgNoteMessage)
                .append(msgWhileCheckingOutPortTreeIsh, msg::package_name = port_name, msg::commit_sha = git_tree);
        }

        return destination;
    }

    ExpectedL<std::string> VcpkgPaths::git_show(StringView treeish, const Path& dot_git_dir) const
    {
        // All git commands are run with: --git-dir={dot_git_dir} --work-tree={work_tree_temp}
        // git clone --no-checkout --local {vcpkg_root} {dot_git_dir}
        Command showcmd = git_cmd_builder(dot_git_dir, dot_git_dir).string_arg("show").string_arg(treeish);
        return flatten_out(cmd_execute_and_capture_output(showcmd), Tools::GIT);
    }

    ExpectedL<std::map<std::string, std::string, std::less<>>> VcpkgPaths::git_get_local_port_treeish_map() const
    {
        const auto local_repo = this->root / ".git";
        const auto git_cmd = git_cmd_builder({}, {})
                                 .string_arg("-C")
                                 .string_arg(this->builtin_ports_directory())
                                 .string_arg("ls-tree")
                                 .string_arg("-d")
                                 .string_arg("HEAD")
                                 .string_arg("--");

        auto maybe_output = flatten_out(cmd_execute_and_capture_output(git_cmd), Tools::GIT);
        if (const auto output = maybe_output.get())
        {
            std::map<std::string, std::string, std::less<>> ret;
            const auto lines = Strings::split(std::move(*output), '\n');
            // The first line of the output is always the parent directory itself.
            for (auto&& line : lines)
            {
                // The default output comes in the format:
                // <mode> SP <type> SP <object> TAB <file>
                auto split_line = Strings::split(line, '\t');
                if (split_line.size() != 2)
                {
                    Debug::println("couldn't split by tabs");
                    return msg::format_error(msgGitUnexpectedCommandOutputCmd,
                                             msg::command_line = git_cmd.command_line())
                        .append_raw('\n')
                        .append_raw(line);
                }

                auto file_info_section = Strings::split(split_line[0], ' ');
                if (file_info_section.size() != 3)
                {
                    Debug::println("couldn't split by spaces");
                    return msg::format_error(msgGitUnexpectedCommandOutputCmd,
                                             msg::command_line = git_cmd.command_line())
                        .append_raw('\n')
                        .append_raw(line);
                }

                ret.emplace(split_line[1], file_info_section.back());
            }
            return ret;
        }

        return msg::format(msgGitCommandFailed, msg::command_line = git_cmd.command_line())
            .append_raw('\n')
            .append(std::move(maybe_output).error())
            .append_raw('\n')
            .append(msg::msgNoteMessage)
            .append(msgWhileGettingLocalTreeIshObjectsForPorts);
    }

    ExpectedL<std::string> VcpkgPaths::git_fetch_from_remote_registry(StringView repo, StringView treeish) const
    {
        auto& fs = get_filesystem();

        const auto& work_tree = m_pimpl->m_registries_work_tree_dir;
        fs.create_directories(work_tree, VCPKG_LINE_INFO);
        const auto& dot_git_dir = m_pimpl->m_registries_dot_git_dir;

        Command init_registries_git_dir = git_cmd_builder(dot_git_dir, work_tree).string_arg("init");
        auto maybe_init_output = flatten(cmd_execute_and_capture_output(init_registries_git_dir), Tools::GIT);
        if (!maybe_init_output)
        {
            return msg::format_error(msgGitCommandFailed, msg::command_line = init_registries_git_dir.command_line())
                .append_raw('\n')
                .append(maybe_init_output.error());
        }

        auto lock_file = work_tree / ".vcpkg-lock";

        auto guard = fs.take_exclusive_file_lock(lock_file, IgnoreErrors{});
        Command fetch_git_ref = git_cmd_builder(dot_git_dir, work_tree)
                                    .string_arg("fetch")
                                    .string_arg("--update-shallow")
                                    .string_arg("--")
                                    .string_arg(repo)
                                    .string_arg(treeish);

        auto maybe_fetch_output = flatten(cmd_execute_and_capture_output(fetch_git_ref), Tools::GIT);
        if (!maybe_fetch_output)
        {
            return msg::format_error(msgGitFailedToFetch, msg::value = treeish, msg::url = repo)
                .append_raw('\n')
                .append(msgGitCommandFailed, msg::command_line = fetch_git_ref.command_line())
                .append_raw('\n')
                .append(std::move(maybe_fetch_output).error());
        }

        Command get_fetch_head =
            git_cmd_builder(dot_git_dir, work_tree).string_arg("rev-parse").string_arg("FETCH_HEAD");
        return flatten_out(cmd_execute_and_capture_output(get_fetch_head), Tools::GIT)
            .map([](std::string&& output) { return Strings::trim(output).to_string(); })
            .map_error([&](LocalizedString&& err) {
                return msg::format_error(msgGitCommandFailed, msg::command_line = get_fetch_head.command_line())
                    .append_raw('\n')
                    .append(std::move(err));
            });
    }

    ExpectedL<Unit> VcpkgPaths::git_fetch(StringView repo, StringView treeish) const
    {
        auto& fs = get_filesystem();

        const auto& work_tree = m_pimpl->m_registries_work_tree_dir;
        fs.create_directories(work_tree, VCPKG_LINE_INFO);

        auto lock_file = work_tree / ".vcpkg-lock";

        auto guard = fs.take_exclusive_file_lock(lock_file, IgnoreErrors{});

        const auto& dot_git_dir = m_pimpl->m_registries_dot_git_dir;

        Command init_registries_git_dir = git_cmd_builder(dot_git_dir, work_tree).string_arg("init");
        auto maybe_init_output = flatten(cmd_execute_and_capture_output(init_registries_git_dir), Tools::GIT);
        if (!maybe_init_output)
        {
            return msg::format_error(msgGitFailedToInitializeLocalRepository, msg::path = work_tree)
                .append_raw('\n')
                .append(msgGitCommandFailed, msg::command_line = init_registries_git_dir.command_line())
                .append_raw('\n')
                .append(std::move(maybe_init_output).error());
        }

        Command fetch_git_ref = git_cmd_builder(dot_git_dir, work_tree)
                                    .string_arg("fetch")
                                    .string_arg("--update-shallow")
                                    .string_arg("--")
                                    .string_arg(repo)
                                    .string_arg(treeish);

        auto maybe_fetch_output = flatten(cmd_execute_and_capture_output(fetch_git_ref), Tools::GIT);
        if (!maybe_fetch_output)
        {
            return msg::format_error(msgGitFailedToFetch, msg::value = treeish, msg::url = repo)
                .append_raw('\n')
                .append(msgGitCommandFailed, msg::command_line = fetch_git_ref.command_line())
                .append_raw('\n')
                .append(std::move(maybe_fetch_output).error());
        }

        return {Unit{}};
    }

    // returns an error if there was an unexpected error; returns nullopt if the file doesn't exist at the specified
    // hash
    ExpectedL<std::string> VcpkgPaths::git_show_from_remote_registry(StringView hash, const Path& relative_path) const
    {
        auto revision = fmt::format("{}:{}", hash, relative_path.generic_u8string());
        Command git_show = git_cmd_builder(m_pimpl->m_registries_dot_git_dir, m_pimpl->m_registries_work_tree_dir)
                               .string_arg("show")
                               .string_arg(revision);

        return flatten_out(cmd_execute_and_capture_output(git_show), Tools::GIT);
    }
    ExpectedL<std::string> VcpkgPaths::git_find_object_id_for_remote_registry_path(StringView hash,
                                                                                   const Path& relative_path) const
    {
        auto revision = fmt::format("{}:{}", hash, relative_path.generic_u8string());
        Command git_rev_parse = git_cmd_builder(m_pimpl->m_registries_dot_git_dir, m_pimpl->m_registries_work_tree_dir)
                                    .string_arg("rev-parse")
                                    .string_arg(revision);

        return flatten_out(cmd_execute_and_capture_output(git_rev_parse), Tools::GIT).map([](std::string&& output) {
            return Strings::trim(std::move(output));
        });
    }
    ExpectedL<Path> VcpkgPaths::git_checkout_object_from_remote_registry(StringView object) const
    {
        auto& fs = get_filesystem();
        fs.create_directories(m_pimpl->m_registries_git_trees, VCPKG_LINE_INFO);

        auto git_tree_final = m_pimpl->m_registries_git_trees / object;
        if (fs.exists(git_tree_final, IgnoreErrors{}))
        {
            return git_tree_final;
        }

        auto pid = get_process_id();

        Path git_tree_temp = fmt::format("{}.tmp{}", git_tree_final, pid);
        Path git_tree_temp_tar = git_tree_temp.native() + ".tar";
        fs.remove_all(git_tree_temp, VCPKG_LINE_INFO);
        fs.create_directory(git_tree_temp, VCPKG_LINE_INFO);

        const auto& dot_git_dir = m_pimpl->m_registries_dot_git_dir;
        Command git_archive = git_cmd_builder(dot_git_dir, m_pimpl->m_registries_work_tree_dir)
                                  .string_arg("archive")
                                  .string_arg("--format")
                                  .string_arg("tar")
                                  .string_arg(object)
                                  .string_arg("--output")
                                  .string_arg(git_tree_temp_tar);
        auto maybe_git_archive_output = flatten(cmd_execute_and_capture_output(git_archive), Tools::GIT);
        if (!maybe_git_archive_output)
        {
            return msg::format_error(msgGitCommandFailed, msg::command_line = git_archive.command_line())
                .append_raw('\n')
                .append(std::move(maybe_git_archive_output).error());
        }

        extract_tar_cmake(get_tool_exe(Tools::CMAKE, stdout_sink), git_tree_temp_tar, git_tree_temp);
        // Attempt to remove temporary files, though non-critical.
        fs.remove(git_tree_temp_tar, IgnoreErrors{});

        std::error_code ec;
        fs.rename_with_retry(git_tree_temp, git_tree_final, ec);
        if (ec)
        {
            return msg::format(msg::msgErrorMessage)
                .append(format_filesystem_call_error(ec, "rename_with_retry", {git_tree_temp, git_tree_final}));
        }

        return git_tree_final;
    }

    Optional<const ManifestAndPath&> VcpkgPaths::get_manifest() const
    {
        if (auto p = m_pimpl->m_manifest_doc.get())
        {
            return *p;
        }
        return nullopt;
    }

    bool VcpkgPaths::manifest_mode_enabled() const { return !m_pimpl->m_manifest_dir.empty(); }

    const ConfigurationAndSource& VcpkgPaths::get_configuration() const { return m_pimpl->m_config; }

    std::unique_ptr<RegistrySet> VcpkgPaths::make_registry_set() const
    {
        auto registry_set = m_pimpl->m_config.instantiate_registry_set(*this);
        // metrics from configuration
        auto default_registry = registry_set->default_registry();
        auto other_registries = registry_set->registries();
        MetricsSubmission metrics;
        if (default_registry)
        {
            metrics.track_string(StringMetric::RegistriesDefaultRegistryKind, default_registry->kind().to_string());
        }
        else
        {
            metrics.track_string(StringMetric::RegistriesDefaultRegistryKind, "disabled");
        }

        if (other_registries.size() != 0)
        {
            std::vector<StringLiteral> registry_kinds;
            for (const auto& reg : other_registries)
            {
                registry_kinds.push_back(reg.implementation().kind());
            }
            Util::sort_unique_erase(registry_kinds);
            metrics.track_string(StringMetric::RegistriesKindsUsed, Strings::join(",", registry_kinds));
        }

        get_global_metrics_collector().track_submission(std::move(metrics));
        return registry_set;
    }

#if defined(_WIN32)
    static const ToolsetsInformation& get_all_toolsets(VcpkgPathsImpl& impl, const Filesystem& fs)
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

    const Toolset& VcpkgPaths::get_toolset(const PreBuildInfo& prebuildinfo) const
    {
        if (!prebuildinfo.using_vcvars())
        {
            static const Toolset external_toolset{
                Path{},
                Path{},
                std::vector<std::string>{},
                "external",
                std::string{},
                std::vector<ToolsetArchOption>{ToolsetArchOption{"", get_host_processor(), get_host_processor()}}};
            return external_toolset;
        }

#if !defined(WIN32)
        Checks::msg_exit_with_error(VCPKG_LINE_INFO, msgErrorVcvarsUnsupported, msg::triplet = prebuildinfo.triplet);
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
            auto error_message = msg::format(msgErrorNoVSInstance, msg::triplet = prebuildinfo.triplet);
            if (vsp)
            {
                error_message.append_raw('\n').append_indent().append(msgErrorNoVSInstanceAt, msg::path = *vsp);
            }
            if (tsv)
            {
                error_message.append_raw('\n').append_indent().append(msgErrorNoVSInstanceVersion, msg::version = *tsv);
            }
            if (tsvf)
            {
                error_message.append_raw('\n').append_indent().append(msgErrorNoVSInstanceFullVersion,
                                                                      msg::version = *tsvf);
            }

            error_message.append_raw('\n').append(toolsets_info.get_localized_debug_info());
            Checks::msg_exit_with_error(VCPKG_LINE_INFO, error_message);
        }
        return *candidate;
#endif
    }

    const Environment& VcpkgPaths::get_action_env(const AbiInfo& abi_info) const
    {
        return m_pimpl->m_env_cache.get_action_env(*this, abi_info);
    }

    const std::string& VcpkgPaths::get_triplet_info(const AbiInfo& abi_info) const
    {
        return m_pimpl->m_env_cache.get_triplet_info(*this, abi_info);
    }

    const CompilerInfo& VcpkgPaths::get_compiler_info(const AbiInfo& abi_info) const
    {
        return m_pimpl->m_env_cache.get_compiler_info(*this, abi_info);
    }

    const FeatureFlagSettings& VcpkgPaths::get_feature_flags() const { return m_pimpl->m_ff_settings; }

    const Path& VcpkgPaths::builtin_ports_directory() const { return m_pimpl->m_builtin_ports; }

    bool VcpkgPaths::use_git_default_registry() const { return m_pimpl->m_bundle.use_git_registry; }

    const Path& VcpkgPaths::artifacts() const { return m_pimpl->m_artifacts_dir; }
    const Path& VcpkgPaths::registries_cache() const { return m_pimpl->m_registries_cache; }
} // namespace vcpkg
