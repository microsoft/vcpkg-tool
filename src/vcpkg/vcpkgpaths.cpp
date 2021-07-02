#include <vcpkg/base/downloads.h>
#include <vcpkg/base/expected.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/hash.h>
#include <vcpkg/base/jsonreader.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/util.h>

#include <vcpkg/binarycaching.h>
#include <vcpkg/binaryparagraph.h>
#include <vcpkg/build.h>
#include <vcpkg/commands.h>
#include <vcpkg/configuration.h>
#include <vcpkg/globalstate.h>
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
    path process_input_directory_impl(
        Filesystem& filesystem, const path& root, std::string* option, StringLiteral name, LineInfo li)
    {
        if (option)
        {
            // input directories must exist, so we use canonical
            return filesystem.almost_canonical(li, vcpkg::u8path(*option));
        }
        else
        {
            return root / vcpkg::u8path(name.begin(), name.end());
        }
    }

    path process_input_directory(
        Filesystem& filesystem, const path& root, std::string* option, StringLiteral name, LineInfo li)
    {
        auto result = process_input_directory_impl(filesystem, root, option, name, li);
        Debug::print("Using ", name, "-root: ", vcpkg::u8string(result), '\n');
        return result;
    }

    path process_output_directory_impl(
        Filesystem& filesystem, const path& root, std::string* option, StringLiteral name, LineInfo li)
    {
        if (option)
        {
            // output directories might not exist, so we use merely absolute
            return filesystem.absolute(li, vcpkg::u8path(*option));
        }
        else
        {
            return root / vcpkg::u8path(name.begin(), name.end());
        }
    }

    path process_output_directory(
        Filesystem& filesystem, const path& root, std::string* option, StringLiteral name, LineInfo li)
    {
        auto result = process_output_directory_impl(filesystem, root, option, name, li);
#if defined(_WIN32)
        result = vcpkg::win32_fix_path_case(result);
#endif // _WIN32
        Debug::print("Using ", name, "-root: ", vcpkg::u8string(result), '\n');
        return result;
    }

} // unnamed namespace

namespace vcpkg
{
    static Configuration deserialize_configuration(const Json::Object& obj,
                                                   const VcpkgCmdArguments& args,
                                                   const path& filepath)
    {
        Json::Reader reader;
        auto deserializer = make_configuration_deserializer(filepath.parent_path());

        auto parsed_config_opt = reader.visit(obj, *deserializer);
        if (!reader.errors().empty())
        {
            print2(Color::error, "Errors occurred while parsing ", vcpkg::u8string(filepath), "\n");
            for (auto&& msg : reader.errors())
                print2("    ", msg, '\n');

            print2("See https://github.com/Microsoft/vcpkg/tree/master/docs/users/registries.md for "
                   "more information.\n");
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        parsed_config_opt.get()->validate_feature_flags(args.feature_flag_settings());

        return std::move(parsed_config_opt).value_or_exit(VCPKG_LINE_INFO);
    }

    struct ManifestAndConfig
    {
        path config_directory;
        Configuration config;
    };

    static std::pair<Json::Object, Json::JsonStyle> load_manifest(const Filesystem& fs, const path& manifest_dir)
    {
        std::error_code ec;
        auto manifest_path = manifest_dir / vcpkg::u8path("vcpkg.json");
        auto manifest_opt = Json::parse_file(fs, manifest_path, ec);
        if (ec)
        {
            Checks::exit_maybe_upgrade(VCPKG_LINE_INFO,
                                       "Failed to load manifest from directory %s: %s",
                                       vcpkg::u8string(manifest_dir),
                                       ec.message());
        }

        if (!manifest_opt.has_value())
        {
            Checks::exit_maybe_upgrade(VCPKG_LINE_INFO,
                                       "Failed to parse manifest at %s:\n%s",
                                       vcpkg::u8string(manifest_path),
                                       manifest_opt.error()->format());
        }
        auto manifest_value = std::move(manifest_opt).value_or_exit(VCPKG_LINE_INFO);

        if (!manifest_value.first.is_object())
        {
            print2(Color::error,
                   "Failed to parse manifest at ",
                   vcpkg::u8string(manifest_path),
                   ": Manifest files must have a top-level object\n");
            Checks::exit_fail(VCPKG_LINE_INFO);
        }
        return {std::move(manifest_value.first.object()), std::move(manifest_value.second)};
    }

    struct ConfigAndPath
    {
        path config_directory;
        Configuration config;
    };

    // doesn't yet implement searching upwards for configurations, nor inheritance of configurations
    static ConfigAndPath load_configuration(const Filesystem& fs,
                                            const VcpkgCmdArguments& args,
                                            const path& vcpkg_root,
                                            const path& manifest_dir)
    {
        path config_dir;
        if (manifest_dir.empty())
        {
            // classic mode
            config_dir = vcpkg_root;
        }
        else
        {
            // manifest mode
            config_dir = manifest_dir;
        }

        auto path_to_config = config_dir / vcpkg::u8path("vcpkg-configuration.json");
        if (!fs.exists(path_to_config))
        {
            return {};
        }

        auto parsed_config = Json::parse_file(VCPKG_LINE_INFO, fs, path_to_config);
        if (!parsed_config.first.is_object())
        {
            print2(Color::error,
                   "Failed to parse ",
                   vcpkg::u8string(path_to_config),
                   ": configuration files must have a top-level object\n");
            Checks::exit_fail(VCPKG_LINE_INFO);
        }
        auto config_obj = std::move(parsed_config.first.object());

        return {std::move(config_dir), deserialize_configuration(config_obj, args, path_to_config)};
    }

    namespace details
    {
        namespace
        {
            const ExpectedS<path>& default_registries_cache_path()
            {
                static auto cachepath = get_platform_cache_home().then([](path p) -> ExpectedS<path> {
                    auto maybe_cachepath = get_environment_variable("X_VCPKG_REGISTRIES_CACHE");
                    if (auto p_str = maybe_cachepath.get())
                    {
                        Metrics::g_metrics.lock()->track_property("X_VCPKG_REGISTRIES_CACHE", "defined");
                        auto path = vcpkg::u8path(*p_str);
                        path.make_preferred();
                        const auto status = stdfs::status(path);
                        if (!stdfs::exists(status))
                        {
                            return {"Path to X_VCPKG_REGISTRIES_CACHE does not exist: " + vcpkg::u8string(path),
                                    expected_right_tag};
                        }

                        if (!stdfs::is_directory(status))
                        {
                            return {"Value of environment variable X_VCPKG_REGISTRIES_CACHE is not a directory: " +
                                        vcpkg::u8string(path),
                                    expected_right_tag};
                        }

                        if (!path.is_absolute())
                        {
                            return {"Value of environment variable X_VCPKG_REGISTRIES_CACHE is not absolute: " +
                                        vcpkg::u8string(path),
                                    expected_right_tag};
                        }

                        return {std::move(path), expected_left_tag};
                    }
                    p /= vcpkg::u8path("vcpkg/registries");
                    p.make_preferred();
                    if (p.is_absolute())
                    {
                        return {std::move(p), expected_left_tag};
                    }
                    else
                    {
                        return {"default path was not absolute: " + vcpkg::u8string(p), expected_right_tag};
                    }
                });
                return cachepath;
            }
        }

        struct VcpkgPathsImpl
        {
            VcpkgPathsImpl(Filesystem& fs, FeatureFlagSettings ff_settings)
                : fs_ptr(&fs)
                , m_tool_cache(get_tool_cache())
                , m_env_cache(ff_settings.compiler_tracking)
                , m_ff_settings(ff_settings)
            {
                const auto& cache_root = default_registries_cache_path().value_or_exit(VCPKG_LINE_INFO);
                registries_work_tree_dir = cache_root / vcpkg::u8path("git");
                registries_dot_git_dir = registries_work_tree_dir / vcpkg::u8path(".git");
                registries_git_trees = cache_root / vcpkg::u8path("git-trees");
            }

            Lazy<std::vector<VcpkgPaths::TripletFile>> available_triplets;
            Lazy<std::vector<Toolset>> toolsets;
            Lazy<std::map<std::string, std::string>> cmake_script_hashes;
            Lazy<std::string> ports_cmake_hash;

            Filesystem* fs_ptr;

            path default_vs_path;
            std::vector<path> triplets_dirs;

            std::unique_ptr<ToolCache> m_tool_cache;
            Cache<Triplet, path> m_triplets_cache;
            Build::EnvCache m_env_cache;

            vcpkg::SystemHandle file_lock_handle;

            Optional<std::pair<Json::Object, Json::JsonStyle>> m_manifest_doc;
            path m_manifest_path;
            Configuration m_config;

            Downloads::DownloadManager m_download_manager;

            FeatureFlagSettings m_ff_settings;

            path registries_work_tree_dir;
            path registries_dot_git_dir;
            path registries_git_trees;

            Optional<LockFile> m_installed_lock;
        };
    }

    static path lockfile_path(const VcpkgPaths& p) { return p.vcpkg_dir / vcpkg::u8path("vcpkg-lock.json"); }

    VcpkgPaths::VcpkgPaths(Filesystem& filesystem, const VcpkgCmdArguments& args)
        : m_pimpl(std::make_unique<details::VcpkgPathsImpl>(filesystem, args.feature_flag_settings()))
    {
        original_cwd = filesystem.current_path(VCPKG_LINE_INFO);
#if defined(_WIN32)
        original_cwd = vcpkg::win32_fix_path_case(original_cwd);
#endif // _WIN32

        if (args.vcpkg_root_dir)
        {
            root = filesystem.almost_canonical(VCPKG_LINE_INFO, vcpkg::u8path(*args.vcpkg_root_dir));
        }
        else
        {
            root = filesystem.find_file_recursively_up(original_cwd, ".vcpkg-root");
            if (root.empty())
            {
                root = filesystem.find_file_recursively_up(
                    filesystem.almost_canonical(VCPKG_LINE_INFO, get_exe_path_of_current_process()), ".vcpkg-root");
            }
        }

        Checks::check_exit(VCPKG_LINE_INFO, !root.empty(), "Error: Could not detect vcpkg-root.");
        Debug::print("Using vcpkg-root: ", vcpkg::u8string(root), '\n');

        std::error_code ec;
        if (args.manifests_enabled())
        {
            if (args.manifest_root_dir)
            {
                manifest_root_dir =
                    filesystem.almost_canonical(VCPKG_LINE_INFO, vcpkg::u8path(*args.manifest_root_dir));
            }
            else
            {
                manifest_root_dir = filesystem.find_file_recursively_up(original_cwd, vcpkg::u8path("vcpkg.json"));
            }
        }

        if (manifest_root_dir.empty())
        {
            installed =
                process_output_directory(filesystem, root, args.install_root_dir.get(), "installed", VCPKG_LINE_INFO);
        }
        else
        {
            Debug::print("Using manifest-root: ", vcpkg::u8string(manifest_root_dir), '\n');

            installed = process_output_directory(
                filesystem, manifest_root_dir, args.install_root_dir.get(), "vcpkg_installed", VCPKG_LINE_INFO);

            const auto vcpkg_lock = root / ".vcpkg-root";
            if (args.wait_for_lock.value_or(false))
            {
                m_pimpl->file_lock_handle = filesystem.take_exclusive_file_lock(vcpkg_lock, ec);
            }
            else
            {
                m_pimpl->file_lock_handle = filesystem.try_take_exclusive_file_lock(vcpkg_lock, ec);
            }

            if (ec)
            {
                if (ec == std::errc::device_or_resource_busy || args.ignore_lock_failures.value_or(false))
                {
                    vcpkg::printf(
                        Color::error, "Failed to take the filesystem lock on %s:\n", vcpkg::u8string(vcpkg_lock));
                    vcpkg::printf(Color::error, "    %s\n", ec.message());
                    Checks::exit_fail(VCPKG_LINE_INFO);
                }
            }

            m_pimpl->m_manifest_doc = load_manifest(filesystem, manifest_root_dir);
            m_pimpl->m_manifest_path = manifest_root_dir / vcpkg::u8path("vcpkg.json");
        }

        auto config_file = load_configuration(filesystem, args, root, manifest_root_dir);

        // metrics from configuration
        {
            auto default_registry = config_file.config.registry_set.default_registry();
            auto other_registries = config_file.config.registry_set.registries();
            auto metrics = Metrics::g_metrics.lock();

            if (default_registry)
            {
                metrics->track_property("registries-default-registry-kind", default_registry->kind());
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

        config_root_dir = std::move(config_file.config_directory);
        m_pimpl->m_config = std::move(config_file.config);

        buildtrees =
            process_output_directory(filesystem, root, args.buildtrees_root_dir.get(), "buildtrees", VCPKG_LINE_INFO);
        downloads =
            process_output_directory(filesystem, root, args.downloads_root_dir.get(), "downloads", VCPKG_LINE_INFO);
        m_pimpl->m_download_manager = Downloads::DownloadManager{
            parse_download_configuration(args.asset_sources_template).value_or_exit(VCPKG_LINE_INFO)};
        packages =
            process_output_directory(filesystem, root, args.packages_root_dir.get(), "packages", VCPKG_LINE_INFO);
        scripts = process_input_directory(filesystem, root, args.scripts_root_dir.get(), "scripts", VCPKG_LINE_INFO);
        prefab = root / vcpkg::u8path("prefab");

        if (args.default_visual_studio_path)
        {
            m_pimpl->default_vs_path =
                filesystem.almost_canonical(VCPKG_LINE_INFO, vcpkg::u8path(*args.default_visual_studio_path));
        }

        triplets = filesystem.almost_canonical(VCPKG_LINE_INFO, root / vcpkg::u8path("triplets"));
        community_triplets = filesystem.almost_canonical(VCPKG_LINE_INFO, triplets / vcpkg::u8path("community"));

        tools = downloads / vcpkg::u8path("tools");
        buildsystems = scripts / vcpkg::u8path("buildsystems");
        const auto msbuildDirectory = buildsystems / vcpkg::u8path("msbuild");
        buildsystems_msbuild_targets = msbuildDirectory / vcpkg::u8path("vcpkg.targets");
        buildsystems_msbuild_props = msbuildDirectory / vcpkg::u8path("vcpkg.props");

        vcpkg_dir = installed / vcpkg::u8path("vcpkg");
        vcpkg_dir_status_file = vcpkg_dir / vcpkg::u8path("status");
        vcpkg_dir_info = vcpkg_dir / vcpkg::u8path("info");
        vcpkg_dir_updates = vcpkg_dir / vcpkg::u8path("updates");

        const auto versioning_tmp = buildtrees / vcpkg::u8path("versioning_tmp");
        const auto versioning_output = buildtrees / vcpkg::u8path("versioning");

        baselines_dot_git_dir = versioning_tmp / vcpkg::u8path(".baselines.git");
        baselines_work_tree = versioning_tmp / vcpkg::u8path("baselines-worktree");
        baselines_output = versioning_output / vcpkg::u8path("baselines");

        versions_dot_git_dir = versioning_tmp / vcpkg::u8path(".versions.git");
        versions_work_tree = versioning_tmp / vcpkg::u8path("versions-worktree");
        versions_output = versioning_output / vcpkg::u8path("versions");

        ports_cmake = filesystem.almost_canonical(VCPKG_LINE_INFO, scripts / vcpkg::u8path("ports.cmake"));

        for (auto&& overlay_triplets_dir : args.overlay_triplets)
        {
            m_pimpl->triplets_dirs.emplace_back(
                filesystem.almost_canonical(VCPKG_LINE_INFO, vcpkg::u8path(overlay_triplets_dir)));
        }
        m_pimpl->triplets_dirs.emplace_back(triplets);
        m_pimpl->triplets_dirs.emplace_back(community_triplets);

        // detect custom registry
        if (args.registry_root_dir)
        {
            current_registry_root = filesystem.almost_canonical(VCPKG_LINE_INFO, u8path(*args.registry_root_dir));
        }
        else
        {
            current_registry_root = filesystem.find_file_recursively_up(original_cwd, "ports");
            if (!current_registry_root.empty())
            {
                const auto current_registry_versions = current_registry_root / u8path("versions");
                if (!filesystem.exists(current_registry_versions))
                {
                    Debug::print("Found ports dir ",
                                 u8string(current_registry_root / u8path("ports")),
                                 " but versions dir ",
                                 u8string(current_registry_versions),
                                 " does not exists\n");
                    current_registry_root.clear();
                }
            }
            if (current_registry_root.empty())
            {
                current_registry_root = this->root;
            }
        }
        Debug::print("Using current_registry_root: ", u8string(current_registry_root), '\n');
        for (auto& dir_name : {".git", "ports", "versions"})
        {
            Checks::check_exit(VCPKG_LINE_INFO,
                               get_filesystem().exists(VCPKG_LINE_INFO, current_registry_root / u8path(dir_name)),
                               "Error: There is no %s directory for current registry at %s",
                               dir_name,
                               u8string(current_registry_root));
        }
    }

    path VcpkgPaths::package_dir(const PackageSpec& spec) const { return this->packages / vcpkg::u8path(spec.dir()); }
    path VcpkgPaths::build_dir(const PackageSpec& spec) const { return this->buildtrees / vcpkg::u8path(spec.name()); }
    path VcpkgPaths::build_dir(const std::string& package_name) const
    {
        return this->buildtrees / vcpkg::u8path(package_name);
    }

    path VcpkgPaths::build_info_file_path(const PackageSpec& spec) const
    {
        return this->package_dir(spec) / "BUILD_INFO";
    }

    path VcpkgPaths::listfile_path(const BinaryParagraph& pgh) const
    {
        return this->vcpkg_dir_info / (pgh.fullstem() + ".list");
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
                for (auto&& path : fs.get_files_non_recursive(triplets_dir))
                {
                    if (vcpkg::is_regular_file(fs.status(VCPKG_LINE_INFO, path)))
                    {
                        output.emplace_back(TripletFile(vcpkg::u8string(path.stem().filename()), triplets_dir));
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
            auto files = fs.get_files_non_recursive(this->scripts / vcpkg::u8path("cmake"));
            for (auto&& file : files)
            {
                helpers.emplace(vcpkg::u8string(file.stem()),
                                Hash::get_file_hash(VCPKG_LINE_INFO, fs, file, Hash::Algorithm::Sha256));
            }
            return helpers;
        });
    }

    StringView VcpkgPaths::get_ports_cmake_hash() const
    {
        return m_pimpl->ports_cmake_hash.get_lazy([this]() -> std::string {
            return Hash::get_file_hash(VCPKG_LINE_INFO, get_filesystem(), ports_cmake, Hash::Algorithm::Sha256);
        });
    }

    static LockFile load_lockfile(const Filesystem& fs, const path& p)
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
            if (doc.is_object())
            {
                for (auto&& x : doc.object())
                {
                    if (!x.second.is_string())
                    {
                        Debug::print("Lockfile value for key '", x.first, "' was not a string\n");
                        return ret;
                    }
                    auto sv = x.second.string();
                    if (!is_git_commit_sha(sv))
                    {
                        Debug::print("Lockfile value for key '", x.first, "' was not a git commit sha\n");
                        return ret;
                    }
                    ret.lockdata.emplace(x.first.to_string(), LockFile::EntryData{sv.to_string(), true});
                }
                return ret;
            }
            Debug::print("Lockfile was not an object\n");
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
            m_pimpl->m_installed_lock = load_lockfile(get_filesystem(), lockfile_path(*this));
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
        Json::Object obj;
        for (auto&& data : lockfile.lockdata)
        {
            obj.insert(data.first, Json::Value::string(data.second.value));
        }
        get_filesystem().write_rename_contents(
            lockfile_path(*this), vcpkg::u8path("vcpkg-lock.json.tmp"), Json::stringify(obj, {}), VCPKG_LINE_INFO);
    }

    const path VcpkgPaths::get_triplet_file_path(Triplet triplet) const
    {
        return m_pimpl->m_triplets_cache.get_lazy(
            triplet, [&]() -> auto {
                for (const auto& triplet_dir : m_pimpl->triplets_dirs)
                {
                    auto path = triplet_dir / (triplet.canonical_name() + ".cmake");
                    if (this->get_filesystem().exists(path))
                    {
                        return path;
                    }
                }

                Checks::exit_with_message(
                    VCPKG_LINE_INFO, "Error: Triplet file %s.cmake not found", triplet.canonical_name());
            });
    }

    const path& VcpkgPaths::get_tool_exe(const std::string& tool) const
    {
        return m_pimpl->m_tool_cache->get_tool_path(*this, tool);
    }
    const std::string& VcpkgPaths::get_tool_version(const std::string& tool) const
    {
        return m_pimpl->m_tool_cache->get_tool_version(*this, tool);
    }

    Command VcpkgPaths::git_cmd_builder(const path& dot_git_dir, const path& work_tree) const
    {
        Command ret(get_tool_exe(Tools::GIT));
        if (!dot_git_dir.empty())
        {
            ret.string_arg(Strings::concat("--git-dir=", vcpkg::u8string(dot_git_dir)));
        }
        if (!work_tree.empty())
        {
            ret.string_arg(Strings::concat("--work-tree=", vcpkg::u8string(work_tree)));
        }
        ret.string_arg("-c").string_arg("core.autocrlf=false");
        return ret;
    }

    ExpectedS<std::string> VcpkgPaths::get_current_git_sha() const
    {
        return get_current_git_sha(this->root / u8path(".git"));
    }
    std::string VcpkgPaths::get_current_git_sha_message() const
    {
        return get_current_git_sha_message(this->root / u8path(".git"));
    }
    ExpectedS<std::string> VcpkgPaths::get_current_git_sha(const path& dot_git_dir) const
    {
        auto cmd = git_cmd_builder(dot_git_dir, this->root);
        cmd.string_arg("rev-parse").string_arg("HEAD");
        auto output = cmd_execute_and_capture_output(cmd);
        if (output.exit_code != 0)
        {
            return {std::move(output.output), expected_right_tag};
        }
        else
        {
            return {Strings::trim(std::move(output.output)), expected_left_tag};
        }
    }
    std::string VcpkgPaths::get_current_git_sha_message(const path& dot_git_dir) const
    {
        auto maybe_cur_sha = get_current_git_sha(dot_git_dir);
        if (auto p_sha = maybe_cur_sha.get())
        {
            return Strings::concat("The current commit is \"", *p_sha, '"');
        }
        else
        {
            return Strings::concat("Failed to determine the current commit:\n", maybe_cur_sha.error());
        }
    }

    ExpectedS<std::string> VcpkgPaths::git_show(const std::string& treeish, const path& dot_git_dir) const
    {
        // All git commands are run with: --git-dir={dot_git_dir} --work-tree={work_tree_temp}
        // git clone --no-checkout --local {vcpkg_root} {dot_git_dir}
        Command showcmd = git_cmd_builder(dot_git_dir, dot_git_dir).string_arg("show").string_arg(treeish);

        auto output = cmd_execute_and_capture_output(showcmd);
        if (output.exit_code == 0)
        {
            return {std::move(output.output), expected_left_tag};
        }
        else
        {
            return {std::move(output.output), expected_right_tag};
        }
    }

    ExitCodeAndOutput VcpkgPaths::git_commit(const path& dot_git_dir,
                                             std::vector<path>&& files,
                                             const std::string& message,
                                             bool amend) const
    {
        for (auto& path : files)
            path = get_filesystem().relative(VCPKG_LINE_INFO, path, dot_git_dir.parent_path());
        Command add_cmd = git_cmd_builder(dot_git_dir, dot_git_dir.parent_path())
                              .string_arg("add")
                              .string_arg("--force")
                              .string_arg("--");
        for (const auto& path : files)
            add_cmd.path_arg(path);
        const auto result = cmd_execute_and_capture_output(add_cmd);
        if (result.exit_code != 0) return result;

        Command commit_cmd = git_cmd_builder(dot_git_dir, dot_git_dir.parent_path()).string_arg("commit");
        if (amend)
        {
            commit_cmd.string_arg("--amend");
        }
        if (!message.empty())
        {
            commit_cmd.string_arg("-m").string_arg(message);
        }
        else if (amend)
        {
            commit_cmd.string_arg("--no-edit");
        }
        commit_cmd.string_arg("--");
        for (const auto& path : files)
            commit_cmd.path_arg(path);

        return cmd_execute_and_capture_output(commit_cmd);
    }

    ExpectedS<std::map<std::string, std::string, std::less<>>> VcpkgPaths::git_get_port_treeish_map(
        const path& ports_dir) const
    {
        const auto git_cmd = git_cmd_builder({}, {})
                                 .string_arg("-C")
                                 .path_arg(ports_dir)
                                 .string_arg("ls-tree")
                                 .string_arg("-d")
                                 .string_arg("HEAD")
                                 .string_arg("--");

        auto output = cmd_execute_and_capture_output(git_cmd);
        if (output.exit_code != 0)
            return Strings::format("Error: Couldn't get local treeish objects for ports.\n%s", output.output);

        std::map<std::string, std::string, std::less<>> ret;
        auto lines = Strings::split(output.output, '\n');
        // The first line of the output is always the parent directory itself.
        for (auto line : lines)
        {
            // The default output comes in the format:
            // <mode> SP <type> SP <object> TAB <file>
            auto split_line = Strings::split(line, '\t');
            if (split_line.size() != 2)
                return Strings::format("Error: Unexpected output from command `%s`. Couldn't split by `\\t`.\n%s",
                                       git_cmd.command_line(),
                                       line);

            auto file_info_section = Strings::split(split_line[0], ' ');
            if (file_info_section.size() != 3)
                return Strings::format("Error: Unexepcted output from command `%s`. Couldn't split by ` `.\n%s",
                                       git_cmd.command_line(),
                                       line);

            ret.emplace(split_line[1], file_info_section.back());
        }
        return ret;
    }

    ExpectedS<std::map<std::string, std::string, std::less<>>> VcpkgPaths::git_get_local_port_treeish_map() const
    {
        return git_get_port_treeish_map(builtin_registry_ports_dir());
    }

    ExpectedS<path> VcpkgPaths::git_checkout_baseline(const path& registry_root, StringView commit_sha) const
    {
        Filesystem& fs = get_filesystem();
        const path destination_parent = this->baselines_output / vcpkg::u8path(commit_sha);
        path destination = destination_parent / vcpkg::u8path("baseline.json");
        if (!fs.exists(destination))
        {
            const path destination_tmp = destination_parent / u8path("baseline.json.tmp");
            auto treeish = Strings::concat(commit_sha, ":versions/baseline.json");
            auto maybe_contents = git_show(treeish, registry_root / u8path(".git"));
            if (auto contents = maybe_contents.get())
            {
                std::error_code ec;
                fs.create_directories(destination_parent, ec);
                if (ec)
                {
                    return {Strings::format(
                                "Error: while checking out baseline %s\nError: while creating directories %s: %s",
                                commit_sha,
                                vcpkg::u8string(destination_parent),
                                ec.message()),
                            expected_right_tag};
                }
                fs.write_contents(destination_tmp, *contents, ec);
                if (ec)
                {
                    return {Strings::format("Error: while checking out baseline %s\nError: while writing %s: %s",
                                            commit_sha,
                                            vcpkg::u8string(destination_tmp),
                                            ec.message()),
                            expected_right_tag};
                }
                fs.rename(destination_tmp, destination, ec);
                if (ec)
                {
                    return {Strings::format("Error: while checking out baseline %s\nError: while renaming %s to %s: %s",
                                            commit_sha,
                                            vcpkg::u8string(destination_tmp),
                                            vcpkg::u8string(destination),
                                            ec.message()),
                            expected_right_tag};
                }
            }
            else
            {
                return {Strings::format("Error: while checking out baseline '%s':\n%s\nThis may be fixed by updating "
                                        "vcpkg to the latest master via `git pull`.",
                                        treeish,
                                        maybe_contents.error()),
                        expected_right_tag};
            }
        }
        return destination;
    }

    ExpectedS<path> VcpkgPaths::git_checkout_port(StringView port_name,
                                                  StringView git_tree,
                                                  const path& dot_git_dir) const
    {
        /* Check out a git tree into the versioned port recipes folder
         *
         * Since we are checking a git tree object, all files will be checked out to the root of `work-tree`.
         * Because of that, it makes sense to use the git hash as the name for the directory.
         */
        Filesystem& fs = get_filesystem();
        path destination = this->versions_output / vcpkg::u8path(port_name) / vcpkg::u8path(git_tree);
        if (fs.exists(destination))
        {
            return destination;
        }

        const path destination_tmp =
            this->versions_output / vcpkg::u8path(port_name) / vcpkg::u8path(Strings::concat(git_tree, ".tmp"));
        const path destination_tar =
            this->versions_output / vcpkg::u8path(port_name) / vcpkg::u8path(Strings::concat(git_tree, ".tar"));
#define PRELUDE "Error: while checking out port ", port_name, " with git tree ", git_tree, "\n"
        std::error_code ec;
        path failure_point;
        fs.remove_all(destination_tmp, ec, failure_point);
        if (ec)
        {
            return {
                Strings::concat(PRELUDE, "Error: while removing ", vcpkg::u8string(failure_point), ": ", ec.message()),
                expected_right_tag};
        }
        fs.create_directories(destination_tmp, ec);
        if (ec)
        {
            return {Strings::concat(PRELUDE,
                                    "Error: while creating directories ",
                                    vcpkg::u8string(destination_tmp),
                                    ": ",
                                    ec.message()),
                    expected_right_tag};
        }

        auto tar_cmd_builder = git_cmd_builder(dot_git_dir, dot_git_dir)
                                   .string_arg("archive")
                                   .string_arg(git_tree)
                                   .string_arg("-o")
                                   .path_arg(destination_tar);
        const auto tar_output = cmd_execute_and_capture_output(tar_cmd_builder);
        if (tar_output.exit_code != 0)
        {
            return {Strings::concat(PRELUDE, "Error: Failed to tar port directory\n", tar_output.output),
                    expected_right_tag};
        }

        auto extract_cmd_builder =
            Command{this->get_tool_exe(Tools::CMAKE)}.string_arg("-E").string_arg("tar").string_arg("xf").path_arg(
                destination_tar);

        const auto extract_output =
            cmd_execute_and_capture_output(extract_cmd_builder, InWorkingDirectory{destination_tmp});
        if (extract_output.exit_code != 0)
        {
            return {Strings::concat(PRELUDE, "Error: Failed to extract port directory\n", extract_output.output),
                    expected_right_tag};
        }
        fs.remove(destination_tar, ec);
        if (ec)
        {
            return {Strings::concat(
                        PRELUDE, "Error: while removing ", vcpkg::u8string(destination_tar), ": ", ec.message()),
                    expected_right_tag};
        }
        fs.rename_with_retry(destination_tmp, destination, ec);
        if (ec)
        {
            return {Strings::concat(PRELUDE,
                                    "Error: while renaming ",
                                    vcpkg::u8string(destination_tmp),
                                    " to ",
                                    vcpkg::u8string(destination),
                                    ": ",
                                    ec.message()),
                    expected_right_tag};
        }

        return destination;
#undef PRELUDE
    }

    ExpectedS<std::string> VcpkgPaths::git_fetch_from_remote_registry(StringView repo, StringView treeish) const
    {
        auto& fs = get_filesystem();

        auto work_tree = m_pimpl->registries_work_tree_dir;
        fs.create_directories(work_tree, VCPKG_LINE_INFO);
        auto dot_git_dir = m_pimpl->registries_dot_git_dir;

        Command init_registries_git_dir = git_cmd_builder(dot_git_dir, work_tree).string_arg("init");
        auto init_output = cmd_execute_and_capture_output(init_registries_git_dir);
        if (init_output.exit_code != 0)
        {
            return {Strings::format("Error: Failed to initialize local repository %s.\n%s\n",
                                    vcpkg::u8string(work_tree),
                                    init_output.output),
                    expected_right_tag};
        }

        auto lock_file = work_tree / vcpkg::u8path(".vcpkg-lock");

        std::error_code ec;
        ExclusiveFileLock guard(ExclusiveFileLock::Wait::Yes, fs, lock_file, ec);

        Command fetch_git_ref = git_cmd_builder(dot_git_dir, work_tree)
                                    .string_arg("fetch")
                                    .string_arg("--update-shallow")
                                    .string_arg("--")
                                    .string_arg(repo)
                                    .string_arg(treeish);

        auto fetch_output = cmd_execute_and_capture_output(fetch_git_ref);
        if (fetch_output.exit_code != 0)
        {
            return {Strings::format(
                        "Error: Failed to fetch ref %s from repository %s.\n%s\n", treeish, repo, fetch_output.output),
                    expected_right_tag};
        }

        Command get_fetch_head =
            git_cmd_builder(dot_git_dir, work_tree).string_arg("rev-parse").string_arg("FETCH_HEAD");
        auto fetch_head_output = cmd_execute_and_capture_output(get_fetch_head);
        if (fetch_head_output.exit_code != 0)
        {
            return {Strings::format("Error: Failed to rev-parse FETCH_HEAD.\n%s\n", fetch_head_output.output),
                    expected_right_tag};
        }
        return {Strings::trim(fetch_head_output.output).to_string(), expected_left_tag};
    }

    Optional<std::string> VcpkgPaths::git_fetch(StringView repo, StringView treeish) const
    {
        auto& fs = get_filesystem();

        auto work_tree = m_pimpl->registries_work_tree_dir;
        fs.create_directories(work_tree, VCPKG_LINE_INFO);

        auto lock_file = work_tree / vcpkg::u8path(".vcpkg-lock");

        std::error_code ec;
        ExclusiveFileLock guard(ExclusiveFileLock::Wait::Yes, fs, lock_file, ec);

        auto dot_git_dir = m_pimpl->registries_dot_git_dir;

        Command init_registries_git_dir = git_cmd_builder(dot_git_dir, work_tree).string_arg("init");
        auto init_output = cmd_execute_and_capture_output(init_registries_git_dir);
        if (init_output.exit_code != 0)
        {
            return Strings::format("Error: Failed to initialize local repository %s.\n%s\n",
                                   vcpkg::u8string(work_tree),
                                   init_output.output);
        }
        Command fetch_git_ref = git_cmd_builder(dot_git_dir, work_tree)
                                    .string_arg("fetch")
                                    .string_arg("--update-shallow")
                                    .string_arg("--")
                                    .string_arg(repo)
                                    .string_arg(treeish);

        auto fetch_output = cmd_execute_and_capture_output(fetch_git_ref);
        if (fetch_output.exit_code != 0)
        {
            return Strings::format(
                "Error: Failed to fetch ref %s from repository %s.\n%s\n", treeish, repo, fetch_output.output);
        }
        return nullopt;
    }

    // returns an error if there was an unexpected error; returns nullopt if the file doesn't exist at the specified
    // hash
    ExpectedS<std::string> VcpkgPaths::git_show_from_remote_registry(StringView hash, const path& relative_path) const
    {
        auto revision = Strings::format("%s:%s", hash, vcpkg::generic_u8string(relative_path));
        Command git_show = git_cmd_builder(m_pimpl->registries_dot_git_dir, m_pimpl->registries_work_tree_dir)
                               .string_arg("show")
                               .string_arg(revision);

        auto git_show_output = cmd_execute_and_capture_output(git_show);
        if (git_show_output.exit_code != 0)
        {
            return {git_show_output.output, expected_right_tag};
        }
        return {git_show_output.output, expected_left_tag};
    }
    ExpectedS<std::string> VcpkgPaths::git_find_object_id_for_remote_registry_path(StringView hash,
                                                                                   const path& relative_path) const
    {
        auto revision = Strings::format("%s:%s", hash, vcpkg::generic_u8string(relative_path));
        Command git_rev_parse = git_cmd_builder(m_pimpl->registries_dot_git_dir, m_pimpl->registries_work_tree_dir)
                                    .string_arg("rev-parse")
                                    .string_arg(revision);

        auto git_rev_parse_output = cmd_execute_and_capture_output(git_rev_parse);
        if (git_rev_parse_output.exit_code != 0)
        {
            return {git_rev_parse_output.output, expected_right_tag};
        }
        return {Strings::trim(git_rev_parse_output.output).to_string(), expected_left_tag};
    }
    ExpectedS<path> VcpkgPaths::git_checkout_object_from_remote_registry(StringView object) const
    {
        auto& fs = get_filesystem();
        fs.create_directories(m_pimpl->registries_git_trees, VCPKG_LINE_INFO);

        auto git_tree_final = m_pimpl->registries_git_trees / vcpkg::u8path(object);
        if (fs.exists(git_tree_final))
        {
            return std::move(git_tree_final);
        }

        auto pid = get_process_id();

        path git_tree_temp = vcpkg::u8path(Strings::format("%s.tmp%ld", vcpkg::u8string(git_tree_final), pid));
        path git_tree_temp_tar = vcpkg::u8path(Strings::format("%s.tmp%ld.tar", vcpkg::u8string(git_tree_final), pid));
        fs.remove_all(git_tree_temp, VCPKG_LINE_INFO);
        fs.create_directory(git_tree_temp, VCPKG_LINE_INFO);

        auto dot_git_dir = m_pimpl->registries_dot_git_dir;
        Command git_archive = git_cmd_builder(dot_git_dir, m_pimpl->registries_work_tree_dir)
                                  .string_arg("archive")
                                  .string_arg("--format")
                                  .string_arg("tar")
                                  .string_arg(object)
                                  .string_arg("--output")
                                  .path_arg(git_tree_temp_tar);
        auto git_archive_output = cmd_execute_and_capture_output(git_archive);
        if (git_archive_output.exit_code != 0)
        {
            return {Strings::format("git archive failed with message:\n%s", git_archive_output.output),
                    expected_right_tag};
        }

        auto untar = Command{get_tool_exe(Tools::CMAKE)}.string_arg("-E").string_arg("tar").string_arg("xf").path_arg(
            git_tree_temp_tar);

        auto untar_output = cmd_execute_and_capture_output(untar, InWorkingDirectory{git_tree_temp});
        // Attempt to remove temporary files, though non-critical.
        fs.remove(git_tree_temp_tar, ignore_errors);
        if (untar_output.exit_code != 0)
        {
            return {Strings::format("cmake's untar failed with message:\n%s", untar_output.output), expected_right_tag};
        }

        std::error_code ec;
        fs.rename(git_tree_temp, git_tree_final, ec);

        if (fs.exists(git_tree_final))
        {
            return git_tree_final;
        }
        if (ec)
        {
            return {
                Strings::format("rename to %s failed with message:\n%s", vcpkg::u8string(git_tree_final), ec.message()),
                expected_right_tag};
        }
        else
        {
            return {"Unknown error", expected_right_tag};
        }
    }

    Optional<const Json::Object&> VcpkgPaths::get_manifest() const
    {
        if (auto p = m_pimpl->m_manifest_doc.get())
        {
            return p->first;
        }
        else
        {
            return nullopt;
        }
    }
    Optional<const path&> VcpkgPaths::get_manifest_path() const
    {
        if (m_pimpl->m_manifest_doc)
        {
            return m_pimpl->m_manifest_path;
        }
        else
        {
            return nullopt;
        }
    }

    const Configuration& VcpkgPaths::get_configuration() const { return m_pimpl->m_config; }
    const Downloads::DownloadManager& VcpkgPaths::get_download_manager() const { return m_pimpl->m_download_manager; }

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

#if !defined(_WIN32)
        Checks::exit_maybe_upgrade(VCPKG_LINE_INFO, "Cannot build windows triplets from non-windows.");
#else
        View<Toolset> vs_toolsets = get_all_toolsets();

        std::vector<const Toolset*> candidates = Util::fmap(vs_toolsets, [](auto&& x) { return &x; });
        const auto tsv = prebuildinfo.platform_toolset.get();
        auto vsp = prebuildinfo.visual_studio_path.get();
        if (!vsp && !m_pimpl->default_vs_path.empty())
        {
            vsp = &m_pimpl->default_vs_path;
        }

        if (tsv && vsp)
        {
            Util::erase_remove_if(
                candidates, [&](const Toolset* t) { return *tsv != t->version || *vsp != t->visual_studio_root_path; });
            Checks::check_exit(VCPKG_LINE_INFO,
                               !candidates.empty(),
                               "Could not find Visual Studio instance at %s with %s toolset.",
                               vcpkg::u8string(*vsp),
                               *tsv);

            Checks::check_exit(VCPKG_LINE_INFO, candidates.size() == 1);
            return *candidates.back();
        }

        if (tsv)
        {
            Util::erase_remove_if(candidates, [&](const Toolset* t) { return *tsv != t->version; });
            Checks::check_exit(
                VCPKG_LINE_INFO, !candidates.empty(), "Could not find Visual Studio instance with %s toolset.", *tsv);
        }

        if (vsp)
        {
            const path vs_root_path = *vsp;
            Util::erase_remove_if(candidates,
                                  [&](const Toolset* t) { return vs_root_path != t->visual_studio_root_path; });
            Checks::check_exit(VCPKG_LINE_INFO,
                               !candidates.empty(),
                               "Could not find Visual Studio instance at %s.",
                               generic_u8string(vs_root_path));
        }

        Checks::check_exit(VCPKG_LINE_INFO, !candidates.empty(), "No suitable Visual Studio instances were found");
        return *candidates.front();

#endif
    }

    View<Toolset> VcpkgPaths::get_all_toolsets() const
    {
#if defined(_WIN32)
        return m_pimpl->toolsets.get_lazy(
            [this]() { return VisualStudio::find_toolset_instances_preferred_first(*this); });
#else
        return {};
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

    Filesystem& VcpkgPaths::get_filesystem() const { return *m_pimpl->fs_ptr; }

    const FeatureFlagSettings& VcpkgPaths::get_feature_flags() const { return m_pimpl->m_ff_settings; }

    void VcpkgPaths::track_feature_flag_metrics() const
    {
        struct
        {
            StringView flag;
            bool enabled;
        } flags[] = {{VcpkgCmdArguments::MANIFEST_MODE_FEATURE, manifest_mode_enabled()}};

        for (const auto& flag : flags)
        {
            Metrics::g_metrics.lock()->track_feature(flag.flag.to_string(), flag.enabled);
        }
    }

    VcpkgPaths::~VcpkgPaths()
    {
        std::error_code ec;
        if (m_pimpl->file_lock_handle.is_valid())
        {
            m_pimpl->fs_ptr->unlock_file_lock(m_pimpl->file_lock_handle, ec);
            if (ec)
            {
                Debug::print("Failed to unlock filesystem lock: ", ec.message(), '\n');
            }
        }
    }
}
