#include <vcpkg/base/files.h>
#include <vcpkg/base/hash.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.print.h>
#include <vcpkg/base/util.h>

#include <vcpkg/binarycaching.h>
#include <vcpkg/build.h>
#include <vcpkg/cmakevars.h>
#include <vcpkg/commands.setinstalled.h>
#include <vcpkg/configuration.h>
#include <vcpkg/dependencies.h>
#include <vcpkg/globalstate.h>
#include <vcpkg/help.h>
#include <vcpkg/input.h>
#include <vcpkg/install.h>
#include <vcpkg/metrics.h>
#include <vcpkg/paragraphs.h>
#include <vcpkg/remove.h>
#include <vcpkg/tools.h>
#include <vcpkg/vcpkglib.h>
#include <vcpkg/vcpkgpaths.h>

namespace vcpkg::Install
{
    using namespace vcpkg;
    using namespace Dependencies;

    using file_pack = std::pair<std::string, std::string>;

    InstallDir InstallDir::from_destination_root(const path& destination_root,
                                                 const std::string& destination_subdirectory,
                                                 const path& listfile)
    {
        InstallDir dirs;
        dirs.m_destination = destination_root / destination_subdirectory;
        dirs.m_destination_subdirectory = destination_subdirectory;
        dirs.m_listfile = listfile;
        return dirs;
    }

    const path& InstallDir::destination() const { return this->m_destination; }

    const std::string& InstallDir::destination_subdirectory() const { return this->m_destination_subdirectory; }

    const path& InstallDir::listfile() const { return this->m_listfile; }

    void install_package_and_write_listfile(const VcpkgPaths& paths,
                                            const PackageSpec& spec,
                                            const InstallDir& destination_dir)
    {
        auto& fs = paths.get_filesystem();
        auto source_dir = paths.package_dir(spec);
        Checks::check_exit(VCPKG_LINE_INFO,
                           fs.exists(source_dir),
                           Strings::concat("Source directory ", vcpkg::u8string(source_dir), "does not exist"));
        auto files = fs.get_files_recursive(source_dir);
        install_files_and_write_listfile(fs, source_dir, files, destination_dir);
    }
    void install_files_and_write_listfile(Filesystem& fs,
                                          const path& source_dir,
                                          const std::vector<path>& files,
                                          const InstallDir& destination_dir)
    {
        std::vector<std::string> output;
        std::error_code ec;

        const size_t prefix_length = vcpkg::generic_u8string(source_dir).size();
        const path& destination = destination_dir.destination();
        const std::string& destination_subdirectory = destination_dir.destination_subdirectory();
        const path& listfile = destination_dir.listfile();

        fs.create_directories(destination, ec);
        Checks::check_exit(
            VCPKG_LINE_INFO, !ec, "Could not create destination directory %s", vcpkg::u8string(destination));
        const path listfile_parent = listfile.parent_path();
        fs.create_directories(listfile_parent, ec);
        Checks::check_exit(
            VCPKG_LINE_INFO, !ec, "Could not create directory for listfile %s", vcpkg::u8string(listfile));

        output.push_back(Strings::format(R"(%s/)", destination_subdirectory));
        for (auto&& file : files)
        {
            const auto status = fs.symlink_status(file, ec);
            if (ec)
            {
                print2(Color::error, "failed: ", vcpkg::u8string(file), ": ", ec.message(), "\n");
                continue;
            }

            const std::string filename = vcpkg::generic_u8string(file.filename());
            if (vcpkg::is_regular_file(status) && (Strings::case_insensitive_ascii_equals(filename, "CONTROL") ||
                                                   Strings::case_insensitive_ascii_equals(filename, "vcpkg.json") ||
                                                   Strings::case_insensitive_ascii_equals(filename, "BUILD_INFO")))
            {
                // Do not copy the control file or manifest file
                continue;
            }

            const std::string suffix = vcpkg::generic_u8string(file).substr(prefix_length + 1);
            const path target = destination / suffix;

            switch (status.type())
            {
                case vcpkg::file_type::directory:
                {
                    fs.create_directory(target, ec);
                    if (ec)
                    {
                        vcpkg::printf(Color::error, "failed: %s: %s\n", vcpkg::u8string(target), ec.message());
                    }

                    // Trailing backslash for directories
                    output.push_back(Strings::format(R"(%s/%s/)", destination_subdirectory, suffix));
                    break;
                }
                case vcpkg::file_type::regular:
                {
                    if (fs.exists(target))
                    {
                        print2(Color::warning,
                               "File ",
                               vcpkg::u8string(target),
                               " was already present and will be overwritten\n");
                    }
                    fs.copy_file(file, target, stdfs::copy_options::overwrite_existing, ec);
                    if (ec)
                    {
                        vcpkg::printf(Color::error, "failed: %s: %s\n", vcpkg::u8string(target), ec.message());
                    }
                    output.push_back(Strings::format(R"(%s/%s)", destination_subdirectory, suffix));
                    break;
                }
                case vcpkg::file_type::symlink:
                {
                    if (fs.exists(target))
                    {
                        print2(Color::warning,
                               "File ",
                               vcpkg::u8string(target),
                               " was already present and will be overwritten\n");
                    }
                    fs.copy_symlink(file, target, ec);
                    if (ec)
                    {
                        vcpkg::printf(Color::error, "failed: %s: %s\n", vcpkg::u8string(target), ec.message());
                    }
                    output.push_back(Strings::format(R"(%s/%s)", destination_subdirectory, suffix));
                    break;
                }
                default:
                    vcpkg::printf(Color::error, "failed: %s: cannot handle file type\n", vcpkg::u8string(file));
                    break;
            }
        }

        std::sort(output.begin(), output.end());

        fs.write_lines(listfile, output, VCPKG_LINE_INFO);
    }

    static std::vector<file_pack> extract_files_in_triplet(
        const std::vector<StatusParagraphAndAssociatedFiles>& pgh_and_files,
        Triplet triplet,
        const size_t remove_chars = 0)
    {
        std::vector<file_pack> output;
        for (const StatusParagraphAndAssociatedFiles& t : pgh_and_files)
        {
            if (t.pgh.package.spec.triplet() != triplet)
            {
                continue;
            }

            const std::string name = t.pgh.package.displayname();

            for (const std::string& file : t.files)
            {
                output.emplace_back(file_pack{std::string(file, remove_chars), name});
            }
        }

        std::sort(output.begin(), output.end(), [](const file_pack& lhs, const file_pack& rhs) {
            return lhs.first < rhs.first;
        });
        return output;
    }

    static SortedVector<std::string> build_list_of_package_files(const Filesystem& fs, const path& package_dir)
    {
        const std::vector<path> package_file_paths = fs.get_files_recursive(package_dir);
        const size_t package_remove_char_count = generic_u8string(package_dir).size() + 1; // +1 for the slash
        auto package_files = Util::fmap(package_file_paths, [package_remove_char_count](const path& target) {
            return std::string(generic_u8string(target), package_remove_char_count);
        });

        return SortedVector<std::string>(std::move(package_files));
    }

    static SortedVector<file_pack> build_list_of_installed_files(
        const std::vector<StatusParagraphAndAssociatedFiles>& pgh_and_files, Triplet triplet)
    {
        const size_t installed_remove_char_count = triplet.canonical_name().size() + 1; // +1 for the slash
        std::vector<file_pack> installed_files =
            extract_files_in_triplet(pgh_and_files, triplet, installed_remove_char_count);

        return SortedVector<file_pack>(std::move(installed_files));
    }

    InstallResult install_package(const VcpkgPaths& paths, const BinaryControlFile& bcf, StatusParagraphs* status_db)
    {
        const path package_dir = paths.package_dir(bcf.core_paragraph.spec);
        Triplet triplet = bcf.core_paragraph.spec.triplet();
        const std::vector<StatusParagraphAndAssociatedFiles> pgh_and_files = get_installed_files(paths, *status_db);

        const SortedVector<std::string> package_files =
            build_list_of_package_files(paths.get_filesystem(), package_dir);
        const SortedVector<file_pack> installed_files = build_list_of_installed_files(pgh_and_files, triplet);

        struct intersection_compare
        {
            // The VS2015 standard library requires comparison operators of T and U
            // to also support comparison of T and T, and of U and U, due to debug checks.
#if _MSC_VER <= 1910
            bool operator()(const std::string& lhs, const std::string& rhs) { return lhs < rhs; }
            bool operator()(const file_pack& lhs, const file_pack& rhs) { return lhs.first < rhs.first; }
#endif
            bool operator()(const std::string& lhs, const file_pack& rhs) { return lhs < rhs.first; }
            bool operator()(const file_pack& lhs, const std::string& rhs) { return lhs.first < rhs; }
        };

        std::vector<file_pack> intersection;

        std::set_intersection(installed_files.begin(),
                              installed_files.end(),
                              package_files.begin(),
                              package_files.end(),
                              std::back_inserter(intersection),
                              intersection_compare());

        std::sort(intersection.begin(), intersection.end(), [](const file_pack& lhs, const file_pack& rhs) {
            return lhs.second < rhs.second;
        });

        if (!intersection.empty())
        {
            const path triplet_install_path = paths.installed / triplet.canonical_name();
            vcpkg::printf(Color::error,
                          "The following files are already installed in %s and are in conflict with %s\n\n",
                          generic_u8string(triplet_install_path),
                          bcf.core_paragraph.spec);

            auto i = intersection.begin();
            while (i != intersection.end())
            {
                print2("Installed by ", i->second, "\n    ");
                auto next =
                    std::find_if(i, intersection.end(), [i](const auto& val) { return i->second != val.second; });

                print2(Strings::join("\n    ", i, next, [](const file_pack& file) { return file.first; }));
                print2("\n\n");

                i = next;
            }

            return InstallResult::FILE_CONFLICTS;
        }

        StatusParagraph source_paragraph;
        source_paragraph.package = bcf.core_paragraph;
        source_paragraph.want = Want::INSTALL;
        source_paragraph.state = InstallState::HALF_INSTALLED;

        write_update(paths, source_paragraph);
        status_db->insert(std::make_unique<StatusParagraph>(source_paragraph));

        std::vector<StatusParagraph> features_spghs;
        for (auto&& feature : bcf.features)
        {
            features_spghs.emplace_back();

            StatusParagraph& feature_paragraph = features_spghs.back();
            feature_paragraph.package = feature;
            feature_paragraph.want = Want::INSTALL;
            feature_paragraph.state = InstallState::HALF_INSTALLED;

            write_update(paths, feature_paragraph);
            status_db->insert(std::make_unique<StatusParagraph>(feature_paragraph));
        }

        const InstallDir install_dir = InstallDir::from_destination_root(
            paths.installed, triplet.to_string(), paths.listfile_path(bcf.core_paragraph));

        install_package_and_write_listfile(paths, bcf.core_paragraph.spec, install_dir);

        source_paragraph.state = InstallState::INSTALLED;
        write_update(paths, source_paragraph);
        status_db->insert(std::make_unique<StatusParagraph>(source_paragraph));

        for (auto&& feature_paragraph : features_spghs)
        {
            feature_paragraph.state = InstallState::INSTALLED;
            write_update(paths, feature_paragraph);
            status_db->insert(std::make_unique<StatusParagraph>(feature_paragraph));
        }

        return InstallResult::SUCCESS;
    }

    using Build::BuildResult;
    using Build::ExtendedBuildResult;

    static ExtendedBuildResult perform_install_plan_action(const VcpkgCmdArguments& args,
                                                           const VcpkgPaths& paths,
                                                           InstallPlanAction& action,
                                                           StatusParagraphs& status_db,
                                                           IBinaryProvider& binaries_provider,
                                                           const Build::IBuildLogsRecorder& build_logs_recorder)
    {
        const InstallPlanType& plan_type = action.plan_type;
        const std::string display_name = action.spec.to_string();
        const std::string display_name_with_features = action.displayname();

        const bool is_user_requested = action.request_type == RequestType::USER_REQUESTED;
        const bool use_head_version = Util::Enum::to_bool(action.build_options.use_head_version);

        if (plan_type == InstallPlanType::ALREADY_INSTALLED)
        {
            if (use_head_version && is_user_requested)
                vcpkg::printf(
                    Color::warning, "Package %s is already installed -- not building from HEAD\n", display_name);
            else
                vcpkg::printf(Color::success, "Package %s is already installed\n", display_name);
            return BuildResult::SUCCEEDED;
        }

        if (plan_type == InstallPlanType::BUILD_AND_INSTALL)
        {
            if (use_head_version)
                vcpkg::printf("Building package %s from HEAD...\n", display_name_with_features);
            else
                vcpkg::printf("Building package %s...\n", display_name_with_features);

            auto result = Build::build_package(args, paths, action, binaries_provider, build_logs_recorder, status_db);

            if (BuildResult::DOWNLOADED == result.code)
            {
                print2(Color::success, "Downloaded sources for package ", display_name_with_features, "\n");
                return result;
            }

            if (result.code != Build::BuildResult::SUCCEEDED)
            {
                print2(Color::error, Build::create_error_message(result.code, action.spec), "\n");
                return result;
            }

            vcpkg::printf("Building package %s... done\n", display_name_with_features);

            auto bcf = std::make_unique<BinaryControlFile>(
                Paragraphs::try_load_cached_package(paths, action.spec).value_or_exit(VCPKG_LINE_INFO));
            vcpkg::printf("Installing package %s...\n", display_name_with_features);
            const auto install_result = install_package(paths, *bcf, &status_db);
            BuildResult code;
            switch (install_result)
            {
                case InstallResult::SUCCESS:
                    vcpkg::printf(Color::success, "Installing package %s... done\n", display_name_with_features);
                    code = BuildResult::SUCCEEDED;
                    break;
                case InstallResult::FILE_CONFLICTS: code = BuildResult::FILE_CONFLICTS; break;
                default: Checks::unreachable(VCPKG_LINE_INFO);
            }

            if (action.build_options.clean_packages == Build::CleanPackages::YES)
            {
                auto& fs = paths.get_filesystem();
                const path package_dir = paths.package_dir(action.spec);
                fs.remove_all(package_dir, VCPKG_LINE_INFO);
            }

            if (action.build_options.clean_downloads == Build::CleanDownloads::YES)
            {
                auto& fs = paths.get_filesystem();
                const path download_dir = paths.downloads;
                std::error_code ec;
                for (auto& p : fs.get_files_non_recursive(download_dir))
                {
                    if (!fs.is_directory(p))
                    {
                        fs.remove(p, VCPKG_LINE_INFO);
                    }
                }
            }

            return {code, std::move(bcf)};
        }

        if (plan_type == InstallPlanType::EXCLUDED)
        {
            vcpkg::printf(Color::warning, "Package %s is excluded\n", display_name);
            return BuildResult::EXCLUDED;
        }

        Checks::unreachable(VCPKG_LINE_INFO);
    }

    void InstallSummary::print() const
    {
        print2("RESULTS\n");

        for (const SpecSummary& result : this->results)
        {
            vcpkg::printf("    %s: %s: %s\n", result.spec, Build::to_string(result.build_result.code), result.timing);
        }

        std::map<BuildResult, int> summary;
        for (const BuildResult& v : Build::BUILD_RESULT_VALUES)
        {
            summary[v] = 0;
        }

        for (const SpecSummary& r : this->results)
        {
            summary[r.build_result.code]++;
        }

        print2("\nSUMMARY\n");
        for (const std::pair<const BuildResult, int>& entry : summary)
        {
            vcpkg::printf("    %s: %d\n", Build::to_string(entry.first), entry.second);
        }
    }

    struct TrackedPackageInstallGuard
    {
        SpecSummary* current_summary = nullptr;
        Chrono::ElapsedTimer build_timer = Chrono::ElapsedTimer::create_started();

        TrackedPackageInstallGuard(const size_t action_index,
                                   const size_t action_count,
                                   std::vector<SpecSummary>& results,
                                   const PackageSpec& spec)
        {
            results.emplace_back(spec, nullptr);
            current_summary = &results.back();
            vcpkg::printf("Starting package %zd/%zd: %s\n", action_index, action_count, spec.to_string());
        }

        ~TrackedPackageInstallGuard()
        {
            current_summary->timing = build_timer.elapsed();
            vcpkg::printf(
                "Elapsed time for package %s: %s\n", current_summary->spec.to_string(), current_summary->timing);
        }

        TrackedPackageInstallGuard(const TrackedPackageInstallGuard&) = delete;
        TrackedPackageInstallGuard& operator=(const TrackedPackageInstallGuard&) = delete;
    };

    InstallSummary perform(const VcpkgCmdArguments& args,
                           ActionPlan& action_plan,
                           const KeepGoing keep_going,
                           const VcpkgPaths& paths,
                           StatusParagraphs& status_db,
                           IBinaryProvider& binaryprovider,
                           const Build::IBuildLogsRecorder& build_logs_recorder,
                           const CMakeVars::CMakeVarProvider& var_provider)
    {
        std::vector<SpecSummary> results;
        const size_t action_count = action_plan.remove_actions.size() + action_plan.install_actions.size();
        size_t action_index = 1;

        const auto timer = Chrono::ElapsedTimer::create_started();
        for (auto&& action : action_plan.remove_actions)
        {
            TrackedPackageInstallGuard this_install(action_index++, action_count, results, action.spec);
            Remove::perform_remove_plan_action(paths, action, Remove::Purge::YES, &status_db);
        }

        for (auto&& action : action_plan.already_installed)
        {
            results.emplace_back(action.spec, &action);
            results.back().build_result =
                perform_install_plan_action(args, paths, action, status_db, binaryprovider, build_logs_recorder);
        }

        Build::compute_all_abis(paths, action_plan, var_provider, status_db);

        std::vector<const InstallPlanAction*> to_prefetch;
        for (auto&& action : action_plan.install_actions)
        {
            if (action.has_package_abi())
            {
                to_prefetch.push_back(&action);
            }
        }
        binaryprovider.prefetch(paths, to_prefetch);

        for (auto&& action : action_plan.install_actions)
        {
            TrackedPackageInstallGuard this_install(action_index++, action_count, results, action.spec);
            auto result =
                perform_install_plan_action(args, paths, action, status_db, binaryprovider, build_logs_recorder);
            if (result.code != BuildResult::SUCCEEDED && keep_going == KeepGoing::NO)
            {
                print2(Build::create_user_troubleshooting_message(action.spec), '\n');
                Checks::exit_fail(VCPKG_LINE_INFO);
            }

            this_install.current_summary->action = &action;
            this_install.current_summary->build_result = std::move(result);
        }

        return InstallSummary{std::move(results), timer.to_string()};
    }

    static constexpr StringLiteral OPTION_DRY_RUN = "dry-run";
    static constexpr StringLiteral OPTION_USE_HEAD_VERSION = "head";
    static constexpr StringLiteral OPTION_NO_DOWNLOADS = "no-downloads";
    static constexpr StringLiteral OPTION_ONLY_BINARYCACHING = "only-binarycaching";
    static constexpr StringLiteral OPTION_ONLY_DOWNLOADS = "only-downloads";
    static constexpr StringLiteral OPTION_RECURSE = "recurse";
    static constexpr StringLiteral OPTION_KEEP_GOING = "keep-going";
    static constexpr StringLiteral OPTION_EDITABLE = "editable";
    static constexpr StringLiteral OPTION_XUNIT = "x-xunit";
    static constexpr StringLiteral OPTION_USE_ARIA2 = "x-use-aria2";
    static constexpr StringLiteral OPTION_CLEAN_AFTER_BUILD = "clean-after-build";
    static constexpr StringLiteral OPTION_CLEAN_BUILDTREES_AFTER_BUILD = "clean-buildtrees-after-build";
    static constexpr StringLiteral OPTION_CLEAN_PACKAGES_AFTER_BUILD = "clean-packages-after-build";
    static constexpr StringLiteral OPTION_CLEAN_DOWNLOADS_AFTER_BUILD = "clean-downloads-after-build";
    static constexpr StringLiteral OPTION_WRITE_PACKAGES_CONFIG = "x-write-nuget-packages-config";
    static constexpr StringLiteral OPTION_MANIFEST_NO_DEFAULT_FEATURES = "x-no-default-features";
    static constexpr StringLiteral OPTION_MANIFEST_FEATURE = "x-feature";
    static constexpr StringLiteral OPTION_PROHIBIT_BACKCOMPAT_FEATURES = "x-prohibit-backcompat-features";

    static constexpr std::array<CommandSwitch, 14> INSTALL_SWITCHES = {{
        {OPTION_DRY_RUN, "Do not actually build or install"},
        {OPTION_USE_HEAD_VERSION, "Install the libraries on the command line using the latest upstream sources"},
        {OPTION_NO_DOWNLOADS, "Do not download new sources"},
        {OPTION_ONLY_DOWNLOADS, "Download sources but don't build packages"},
        {OPTION_ONLY_BINARYCACHING, "Fail if cached binaries are not available"},
        {OPTION_RECURSE, "Allow removal of packages as part of installation"},
        {OPTION_KEEP_GOING, "Continue installing packages on failure"},
        {OPTION_EDITABLE, "Disable source re-extraction and binary caching for libraries on the command line"},

        {OPTION_USE_ARIA2, "Use aria2 to perform download tasks"},
        {OPTION_CLEAN_AFTER_BUILD, "Clean buildtrees, packages and downloads after building each package"},
        {OPTION_CLEAN_BUILDTREES_AFTER_BUILD, "Clean buildtrees after building each package"},
        {OPTION_CLEAN_PACKAGES_AFTER_BUILD, "Clean packages after building each package"},
        {OPTION_CLEAN_DOWNLOADS_AFTER_BUILD, "Clean downloads after building each package"},
        {OPTION_PROHIBIT_BACKCOMPAT_FEATURES,
         "(experimental) Fail install if a package attempts to use a deprecated feature"},
    }};
    static constexpr std::array<CommandSwitch, 14> MANIFEST_INSTALL_SWITCHES = {{
        {OPTION_DRY_RUN, "Do not actually build or install"},
        {OPTION_USE_HEAD_VERSION, "Install the libraries on the command line using the latest upstream sources"},
        {OPTION_NO_DOWNLOADS, "Do not download new sources"},
        {OPTION_ONLY_DOWNLOADS, "Download sources but don't build packages"},
        {OPTION_ONLY_BINARYCACHING, "Fail if cached binaries are not available"},
        {OPTION_RECURSE, "Allow removal of packages as part of installation"},
        {OPTION_KEEP_GOING, "Continue installing packages on failure"},
        {OPTION_EDITABLE, "Disable source re-extraction and binary caching for libraries on the command line"},
        {OPTION_USE_ARIA2, "Use aria2 to perform download tasks"},
        {OPTION_CLEAN_AFTER_BUILD, "Clean buildtrees, packages and downloads after building each package"},
        {OPTION_CLEAN_BUILDTREES_AFTER_BUILD, "Clean buildtrees after building each package"},
        {OPTION_CLEAN_PACKAGES_AFTER_BUILD, "Clean packages after building each package"},
        {OPTION_CLEAN_DOWNLOADS_AFTER_BUILD, "Clean downloads after building each package"},
        {OPTION_MANIFEST_NO_DEFAULT_FEATURES, "Don't install the default features from the manifest."},
    }};

    static constexpr std::array<CommandSetting, 2> INSTALL_SETTINGS = {{
        {OPTION_XUNIT, "File to output results in XUnit format (Internal use)"},
        {OPTION_WRITE_PACKAGES_CONFIG,
         "Writes out a NuGet packages.config-formatted file for use with external binary caching.\nSee `vcpkg help "
         "binarycaching` for more information."},
    }};

    static constexpr std::array<CommandMultiSetting, 1> MANIFEST_INSTALL_MULTISETTINGS = {{
        {OPTION_MANIFEST_FEATURE, "A feature from the manifest to install."},
    }};

    std::vector<std::string> get_all_port_names(const VcpkgPaths& paths)
    {
        auto sources_and_errors = Paragraphs::try_load_all_registry_ports(paths);

        return Util::fmap(sources_and_errors.paragraphs, Paragraphs::get_name_of_control_file);
    }

    const CommandStructure COMMAND_STRUCTURE = {
        create_example_string("install zlib zlib:x64-windows curl boost"),
        1,
        SIZE_MAX,
        {INSTALL_SWITCHES, INSTALL_SETTINGS},
        &get_all_port_names,
    };

    const CommandStructure MANIFEST_COMMAND_STRUCTURE = {
        create_example_string("install --triplet x64-windows"),
        0,
        0,
        {MANIFEST_INSTALL_SWITCHES, INSTALL_SETTINGS, MANIFEST_INSTALL_MULTISETTINGS},
        nullptr,
    };

    static void print_cmake_information(const BinaryParagraph& bpgh, const VcpkgPaths& paths)
    {
        auto usage = get_cmake_usage(bpgh, paths);

        if (!usage.message.empty())
        {
            print2(usage.message);
        }
    }

    CMakeUsageInfo get_cmake_usage(const BinaryParagraph& bpgh, const VcpkgPaths& paths)
    {
        static const std::regex cmake_library_regex(R"(\badd_library\(([^\$\s\)]+)\s)",
                                                    std::regex_constants::ECMAScript);

        CMakeUsageInfo ret;

        auto& fs = paths.get_filesystem();

        auto usage_file = paths.installed / bpgh.spec.triplet().canonical_name() / "share" / bpgh.spec.name() / "usage";
        if (fs.exists(usage_file))
        {
            ret.usage_file = true;
            auto maybe_contents = fs.read_contents(usage_file);
            if (auto p_contents = maybe_contents.get())
            {
                ret.message = std::move(*p_contents);
                ret.message.push_back('\n');
            }
            return ret;
        }

        auto files = fs.read_lines(paths.listfile_path(bpgh));
        if (auto p_lines = files.get())
        {
            std::map<std::string, std::string> config_files;
            std::map<std::string, std::vector<std::string>> library_targets;
            bool is_header_only = true;
            std::string header_path;

            for (auto&& suffix : *p_lines)
            {
                if (Strings::case_insensitive_ascii_contains(suffix, "/share/") && Strings::ends_with(suffix, ".cmake"))
                {
                    // CMake file is inside the share folder
                    auto path = paths.installed / suffix;
                    auto maybe_contents = fs.read_contents(path);
                    auto find_package_name = vcpkg::u8string(path.parent_path().filename());
                    if (auto p_contents = maybe_contents.get())
                    {
                        std::sregex_iterator next(p_contents->begin(), p_contents->end(), cmake_library_regex);
                        std::sregex_iterator last;

                        while (next != last)
                        {
                            auto match = *next;
                            auto& targets = library_targets[find_package_name];
                            if (std::find(targets.cbegin(), targets.cend(), match[1]) == targets.cend())
                                targets.push_back(match[1]);
                            ++next;
                        }
                    }

                    auto filename = vcpkg::u8string(vcpkg::u8path(suffix).filename());

                    if (Strings::ends_with(filename, "Config.cmake"))
                    {
                        auto root = filename.substr(0, filename.size() - 12);
                        if (Strings::case_insensitive_ascii_equals(root, find_package_name))
                            config_files[find_package_name] = root;
                    }
                    else if (Strings::ends_with(filename, "-config.cmake"))
                    {
                        auto root = filename.substr(0, filename.size() - 13);
                        if (Strings::case_insensitive_ascii_equals(root, find_package_name))
                            config_files[find_package_name] = root;
                    }
                }
                if (Strings::case_insensitive_ascii_contains(suffix, "/lib/") ||
                    Strings::case_insensitive_ascii_contains(suffix, "/bin/"))
                {
                    if (!Strings::ends_with(suffix, ".pc") && !Strings::ends_with(suffix, "/")) is_header_only = false;
                }

                if (is_header_only && header_path.empty())
                {
                    auto it = suffix.find("/include/");
                    if (it != std::string::npos && !Strings::ends_with(suffix, "/"))
                    {
                        header_path = suffix.substr(it + 9);
                    }
                }
            }

            ret.header_only = is_header_only;

            if (library_targets.empty())
            {
                if (is_header_only && !header_path.empty())
                {
                    static auto cmakeify = [](std::string name) {
                        auto n = Strings::ascii_to_uppercase(Strings::replace_all(std::move(name), "-", "_"));
                        if (n.empty() || Parse::ParserBase::is_ascii_digit(n[0]))
                        {
                            n.insert(n.begin(), '_');
                        }
                        return n;
                    };

                    const auto name = cmakeify(bpgh.spec.name());
                    auto msg = Strings::concat(
                        "The package ", bpgh.spec, " is header only and can be used from CMake via:\n\n");
                    Strings::append(msg, "    find_path(", name, "_INCLUDE_DIRS \"", header_path, "\")\n");
                    Strings::append(msg, "    target_include_directories(main PRIVATE ${", name, "_INCLUDE_DIRS})\n\n");

                    ret.message = std::move(msg);
                }
            }
            else
            {
                auto msg = Strings::concat("The package ", bpgh.spec, " provides CMake targets:\n\n");

                for (auto&& library_target_pair : library_targets)
                {
                    auto config_it = config_files.find(library_target_pair.first);
                    if (config_it != config_files.end())
                        Strings::append(msg, "    find_package(", config_it->second, " CONFIG REQUIRED)\n");
                    else
                        Strings::append(msg, "    find_package(", library_target_pair.first, " CONFIG REQUIRED)\n");

                    std::sort(library_target_pair.second.begin(),
                              library_target_pair.second.end(),
                              [](const std::string& l, const std::string& r) {
                                  if (l.size() < r.size()) return true;
                                  if (l.size() > r.size()) return false;
                                  return l < r;
                              });

                    if (library_target_pair.second.size() <= 4)
                    {
                        Strings::append(msg,
                                        "    target_link_libraries(main PRIVATE ",
                                        Strings::join(" ", library_target_pair.second),
                                        ")\n\n");
                    }
                    else
                    {
                        auto omitted = library_target_pair.second.size() - 4;
                        library_target_pair.second.erase(library_target_pair.second.begin() + 4,
                                                         library_target_pair.second.end());
                        msg += Strings::format("    # Note: %zd target(s) were omitted.\n"
                                               "    target_link_libraries(main PRIVATE %s)\n\n",
                                               omitted,
                                               Strings::join(" ", library_target_pair.second));
                    }
                }
                ret.message = std::move(msg);
            }
            ret.cmake_targets_map = std::move(library_targets);
        }
        return ret;
    }

    ///
    /// <summary>
    /// Run "install" command.
    /// </summary>
    ///
    void perform_and_exit(const VcpkgCmdArguments& args,
                          const VcpkgPaths& paths,
                          Triplet default_triplet,
                          Triplet host_triplet)
    {
        // input sanitization
        const ParsedArguments options =
            args.parse_arguments(paths.manifest_mode_enabled() ? MANIFEST_COMMAND_STRUCTURE : COMMAND_STRUCTURE);

        auto binaryprovider = create_binary_provider_from_configs(args.binary_sources).value_or_exit(VCPKG_LINE_INFO);

        const bool dry_run = Util::Sets::contains(options.switches, OPTION_DRY_RUN);
        const bool use_head_version = Util::Sets::contains(options.switches, (OPTION_USE_HEAD_VERSION));
        const bool no_downloads = Util::Sets::contains(options.switches, (OPTION_NO_DOWNLOADS));
        const bool only_downloads = Util::Sets::contains(options.switches, (OPTION_ONLY_DOWNLOADS));
        const bool no_build_missing = Util::Sets::contains(options.switches, OPTION_ONLY_BINARYCACHING);
        const bool is_recursive = Util::Sets::contains(options.switches, (OPTION_RECURSE));
        const bool is_editable = Util::Sets::contains(options.switches, (OPTION_EDITABLE)) || !args.cmake_args.empty();
        const bool use_aria2 = Util::Sets::contains(options.switches, (OPTION_USE_ARIA2));
        const bool clean_after_build = Util::Sets::contains(options.switches, (OPTION_CLEAN_AFTER_BUILD));
        const bool clean_buildtrees_after_build =
            Util::Sets::contains(options.switches, (OPTION_CLEAN_BUILDTREES_AFTER_BUILD));
        const bool clean_packages_after_build =
            Util::Sets::contains(options.switches, (OPTION_CLEAN_PACKAGES_AFTER_BUILD));
        const bool clean_downloads_after_build =
            Util::Sets::contains(options.switches, (OPTION_CLEAN_DOWNLOADS_AFTER_BUILD));
        const KeepGoing keep_going =
            to_keep_going(Util::Sets::contains(options.switches, OPTION_KEEP_GOING) || only_downloads);
        const bool prohibit_backcompat_features =
            Util::Sets::contains(options.switches, (OPTION_PROHIBIT_BACKCOMPAT_FEATURES));

        auto& fs = paths.get_filesystem();

        Build::DownloadTool download_tool = Build::DownloadTool::BUILT_IN;
        if (use_aria2) download_tool = Build::DownloadTool::ARIA2;

        const Build::BuildPackageOptions install_plan_options = {
            Util::Enum::to_enum<Build::BuildMissing>(!no_build_missing),
            Util::Enum::to_enum<Build::UseHeadVersion>(use_head_version),
            Util::Enum::to_enum<Build::AllowDownloads>(!no_downloads),
            Util::Enum::to_enum<Build::OnlyDownloads>(only_downloads),
            Util::Enum::to_enum<Build::CleanBuildtrees>(clean_after_build || clean_buildtrees_after_build),
            Util::Enum::to_enum<Build::CleanPackages>(clean_after_build || clean_packages_after_build),
            Util::Enum::to_enum<Build::CleanDownloads>(clean_after_build || clean_downloads_after_build),
            download_tool,
            Build::PurgeDecompressFailure::NO,
            Util::Enum::to_enum<Build::Editable>(is_editable),
            prohibit_backcompat_features ? Build::BackcompatFeatures::PROHIBIT : Build::BackcompatFeatures::ALLOW,
        };

        auto var_provider_storage = CMakeVars::make_triplet_cmake_var_provider(paths);
        auto& var_provider = *var_provider_storage;

        if (auto manifest = paths.get_manifest().get())
        {
            Optional<path> pkgsconfig;
            auto it_pkgsconfig = options.settings.find(OPTION_WRITE_PACKAGES_CONFIG);
            if (it_pkgsconfig != options.settings.end())
            {
                Metrics::g_metrics.lock()->track_property("x-write-nuget-packages-config", "defined");
                pkgsconfig = vcpkg::u8path(it_pkgsconfig->second);
            }
            const auto& manifest_path = paths.get_manifest_path().value_or_exit(VCPKG_LINE_INFO);
            auto maybe_manifest_scf = SourceControlFile::parse_manifest_file(manifest_path, *manifest);
            if (!maybe_manifest_scf)
            {
                print_error_message(maybe_manifest_scf.error());
                print2("See https://github.com/Microsoft/vcpkg/tree/master/docs/users/manifests.md for "
                       "more information.\n");
                Checks::exit_fail(VCPKG_LINE_INFO);
            }

            auto& manifest_scf = *maybe_manifest_scf.value_or_exit(VCPKG_LINE_INFO);

            if (auto maybe_error = manifest_scf.check_against_feature_flags(
                    manifest_path,
                    paths.get_feature_flags(),
                    paths.get_configuration().registry_set.is_default_builtin_registry()))
            {
                Checks::exit_with_message(VCPKG_LINE_INFO, maybe_error.value_or_exit(VCPKG_LINE_INFO));
            }

            std::vector<std::string> features;
            auto manifest_feature_it = options.multisettings.find(OPTION_MANIFEST_FEATURE);
            if (manifest_feature_it != options.multisettings.end())
            {
                features.insert(features.end(), manifest_feature_it->second.begin(), manifest_feature_it->second.end());
            }
            auto core_it = Util::find(features, "core");
            if (core_it == features.end() &&
                !Util::Sets::contains(options.switches, OPTION_MANIFEST_NO_DEFAULT_FEATURES))
            {
                const auto& default_features = manifest_scf.core_paragraph->default_features;
                features.insert(features.end(), default_features.begin(), default_features.end());
            }
            else
            {
                // remove "core" because resolve_deps_as_top_level uses default-inversion
                // support multiple core features
                core_it = std::remove(core_it, features.end(), "core");
                features.erase(core_it, features.end());
            }
            Util::sort_unique_erase(features);

            auto dependencies = manifest_scf.core_paragraph->dependencies;
            for (const auto& feature : features)
            {
                auto it = Util::find_if(
                    manifest_scf.feature_paragraphs,
                    [&feature](const std::unique_ptr<FeatureParagraph>& fpgh) { return fpgh->name == feature; });

                if (it == manifest_scf.feature_paragraphs.end())
                {
                    vcpkg::printf(Color::warning,
                                  "Warning: feature %s was passed, but that is not a feature that %s supports.",
                                  feature,
                                  manifest_scf.core_paragraph->name);
                }
                else
                {
                    dependencies.insert(
                        dependencies.end(), it->get()->dependencies.begin(), it->get()->dependencies.end());
                }
            }

            if (std::any_of(dependencies.begin(), dependencies.end(), [](const Dependency& dep) {
                    return dep.constraint.type != Versions::Constraint::Type::None;
                }))
            {
                Metrics::g_metrics.lock()->track_property("manifest_version_constraint", "defined");
            }

            if (!manifest_scf.core_paragraph->overrides.empty())
            {
                Metrics::g_metrics.lock()->track_property("manifest_overrides", "defined");
            }

            if (auto p_baseline = manifest_scf.core_paragraph->builtin_baseline.get())
            {
                Metrics::g_metrics.lock()->track_property("manifest_baseline", "defined");
                if (p_baseline->size() != 40 || !std::all_of(p_baseline->begin(), p_baseline->end(), [](char ch) {
                        return (ch >= 'a' || ch <= 'f') || Parse::ParserBase::is_ascii_digit(ch);
                    }))
                {
                    Metrics::g_metrics.lock()->track_property("versioning-error-baseline", "defined");
                    Checks::exit_maybe_upgrade(VCPKG_LINE_INFO,
                                               "Error: the top-level builtin-baseline (%s) was not a valid commit sha: "
                                               "expected 40 lowercase hexadecimal characters.\n%s\n",
                                               *p_baseline,
                                               paths.get_current_git_sha_message());
                }

                paths.get_configuration().registry_set.set_default_builtin_registry_baseline(*p_baseline);
            }

            auto verprovider = PortFileProvider::make_versioned_portfile_provider(paths);
            auto baseprovider = PortFileProvider::make_baseline_provider(paths);

            std::vector<std::string> extended_overlay_ports;
            extended_overlay_ports.reserve(args.overlay_ports.size() + 1);
            extended_overlay_ports.push_back(vcpkg::u8string(manifest_path.parent_path()));
            Util::Vectors::append(&extended_overlay_ports, args.overlay_ports);
            auto oprovider = PortFileProvider::make_overlay_provider(paths, extended_overlay_ports);

            PackageSpec toplevel{manifest_scf.core_paragraph->name, default_triplet};
            auto install_plan = Dependencies::create_versioned_install_plan(*verprovider,
                                                                            *baseprovider,
                                                                            *oprovider,
                                                                            var_provider,
                                                                            dependencies,
                                                                            manifest_scf.core_paragraph->overrides,
                                                                            toplevel,
                                                                            host_triplet)
                                    .value_or_exit(VCPKG_LINE_INFO);

            for (InstallPlanAction& action : install_plan.install_actions)
            {
                action.build_options = install_plan_options;
                action.build_options.use_head_version = Build::UseHeadVersion::NO;
                action.build_options.editable = Build::Editable::NO;
            }

            // If the manifest refers to itself, it will be added to the install plan.
            Util::erase_remove_if(install_plan.install_actions,
                                  [&toplevel](auto&& action) { return action.spec == toplevel; });

            PortFileProvider::PathsPortFileProvider provider(paths, extended_overlay_ports);

            Commands::SetInstalled::perform_and_exit_ex(args,
                                                        paths,
                                                        provider,
                                                        *binaryprovider,
                                                        var_provider,
                                                        std::move(install_plan),
                                                        dry_run ? Commands::DryRun::Yes : Commands::DryRun::No,
                                                        pkgsconfig,
                                                        host_triplet);
        }

        PortFileProvider::PathsPortFileProvider provider(paths, args.overlay_ports);

        const std::vector<FullPackageSpec> specs = Util::fmap(args.command_arguments, [&](auto&& arg) {
            return Input::check_and_get_full_package_spec(
                std::string(arg), default_triplet, COMMAND_STRUCTURE.example_text);
        });

        for (auto&& spec : specs)
        {
            Input::check_triplet(spec.package_spec.triplet(), paths);
        }

        // create the plan
        print2("Computing installation plan...\n");
        StatusParagraphs status_db = database_load_check(paths);

        // Note: action_plan will hold raw pointers to SourceControlFileLocations from this map
        auto action_plan =
            Dependencies::create_feature_install_plan(provider, var_provider, specs, status_db, {host_triplet});

        for (auto&& action : action_plan.install_actions)
        {
            action.build_options = install_plan_options;
            if (action.request_type != RequestType::USER_REQUESTED)
            {
                action.build_options.use_head_version = Build::UseHeadVersion::NO;
                action.build_options.editable = Build::Editable::NO;
            }
        }

        var_provider.load_tag_vars(action_plan, provider, host_triplet);

        // install plan will be empty if it is already installed - need to change this at status paragraph part
        Checks::check_exit(VCPKG_LINE_INFO, !action_plan.empty(), "Install plan cannot be empty");

#if defined(_WIN32)
        const auto maybe_common_triplet = Util::common_projection(
            action_plan.install_actions, [](const InstallPlanAction& to_install) { return to_install.spec.triplet(); });
        if (maybe_common_triplet)
        {
            const auto& common_triplet = maybe_common_triplet.value_or_exit(VCPKG_LINE_INFO);
            const auto maybe_common_arch = common_triplet.guess_architecture();
            if (maybe_common_arch)
            {
                const auto maybe_vs_prompt = guess_visual_studio_prompt_target_architecture();
                if (maybe_vs_prompt)
                {
                    const auto common_arch = maybe_common_arch.value_or_exit(VCPKG_LINE_INFO);
                    const auto vs_prompt = maybe_vs_prompt.value_or_exit(VCPKG_LINE_INFO);
                    if (common_arch != vs_prompt)
                    {
                        const auto vs_prompt_view = to_zstring_view(vs_prompt);
                        print2(vcpkg::Color::warning,
                               "warning: vcpkg appears to be in a Visual Studio prompt targeting ",
                               vs_prompt_view,
                               " but is installing packages for ",
                               common_triplet.to_string(),
                               ". Consider using --triplet ",
                               vs_prompt_view,
                               "-windows or --triplet ",
                               vs_prompt_view,
                               "-uwp.\n");
                    }
                }
            }
        }
#endif // defined(_WIN32)

        Dependencies::print_plan(action_plan, is_recursive, paths.builtin_ports_directory());

        auto it_pkgsconfig = options.settings.find(OPTION_WRITE_PACKAGES_CONFIG);
        if (it_pkgsconfig != options.settings.end())
        {
            Metrics::g_metrics.lock()->track_property("x-write-nuget-packages-config", "defined");
            Build::compute_all_abis(paths, action_plan, var_provider, status_db);

            auto pkgsconfig_path = combine(paths.original_cwd, vcpkg::u8path(it_pkgsconfig->second));
            auto pkgsconfig_contents = generate_nuget_packages_config(action_plan);
            fs.write_contents(pkgsconfig_path, pkgsconfig_contents, VCPKG_LINE_INFO);
            print2("Wrote NuGet packages config information to ", vcpkg::u8string(pkgsconfig_path), "\n");
        }

        if (dry_run)
        {
            Checks::exit_success(VCPKG_LINE_INFO);
        }

        paths.flush_lockfile();

        track_install_plan(action_plan);

        const InstallSummary summary =
            perform(args,
                    action_plan,
                    keep_going,
                    paths,
                    status_db,
                    args.binary_caching_enabled() && !only_downloads ? *binaryprovider : null_binary_provider(),
                    Build::null_build_logs_recorder(),
                    var_provider);

        print2("\nTotal elapsed time: ", summary.total_elapsed_time, "\n\n");

        if (keep_going == KeepGoing::YES)
        {
            summary.print();
        }

        auto it_xunit = options.settings.find(OPTION_XUNIT);
        if (it_xunit != options.settings.end())
        {
            std::string xunit_doc = "<assemblies><assembly><collection>\n";

            xunit_doc += summary.xunit_results();

            xunit_doc += "</collection></assembly></assemblies>\n";
            fs.write_contents(vcpkg::u8path(it_xunit->second), xunit_doc, VCPKG_LINE_INFO);
        }

        for (auto&& result : summary.results)
        {
            if (!result.action) continue;
            if (result.action->request_type != RequestType::USER_REQUESTED) continue;
            auto bpgh = result.get_binary_paragraph();
            if (!bpgh) continue;
            print_cmake_information(*bpgh, paths);
        }

        Checks::exit_success(VCPKG_LINE_INFO);
    }

    void InstallCommand::perform_and_exit(const VcpkgCmdArguments& args,
                                          const VcpkgPaths& paths,
                                          Triplet default_triplet,
                                          Triplet host_triplet) const
    {
        Install::perform_and_exit(args, paths, default_triplet, host_triplet);
    }

    SpecSummary::SpecSummary(const PackageSpec& spec, const Dependencies::InstallPlanAction* action)
        : spec(spec), build_result{BuildResult::NULLVALUE, nullptr}, action(action)
    {
    }

    const BinaryParagraph* SpecSummary::get_binary_paragraph() const
    {
        if (build_result.binary_control_file) return &build_result.binary_control_file->core_paragraph;
        if (action)
            if (auto p_status = action->installed_package.get())
            {
                return &p_status->core->package;
            }
        return nullptr;
    }

    static std::string xunit_result(const PackageSpec& spec, Chrono::ElapsedTime time, BuildResult code)
    {
        std::string message_block;
        const char* result_string = "";
        switch (code)
        {
            case BuildResult::POST_BUILD_CHECKS_FAILED:
            case BuildResult::FILE_CONFLICTS:
            case BuildResult::BUILD_FAILED:
            case BuildResult::CACHE_MISSING:
                result_string = "Fail";
                message_block =
                    Strings::format("<failure><message><![CDATA[%s]]></message></failure>", to_string(code));
                break;
            case BuildResult::EXCLUDED:
            case BuildResult::CASCADED_DUE_TO_MISSING_DEPENDENCIES:
                result_string = "Skip";
                message_block = Strings::format("<reason><![CDATA[%s]]></reason>", to_string(code));
                break;
            case BuildResult::SUCCEEDED: result_string = "Pass"; break;
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }

        return Strings::format(R"(<test name="%s" method="%s" time="%lld" result="%s">%s</test>)"
                               "\n",
                               spec,
                               spec,
                               time.as<std::chrono::seconds>().count(),
                               result_string,
                               message_block);
    }

    std::string InstallSummary::xunit_results() const
    {
        std::string xunit_doc;
        for (auto&& result : results)
        {
            xunit_doc += xunit_result(result.spec, result.timing, result.build_result.code);
        }
        return xunit_doc;
    }

    void track_install_plan(Dependencies::ActionPlan& plan)
    {
        Cache<Triplet, std::string> triplet_hashes;

        auto hash_triplet = [&triplet_hashes](Triplet t) -> const std::string& {
            return triplet_hashes.get_lazy(
                t, [t]() { return Hash::get_string_hash(t.canonical_name(), Hash::Algorithm::Sha256); });
        };

        std::string specs_string;
        for (auto&& remove_action : plan.remove_actions)
        {
            if (!specs_string.empty()) specs_string.push_back(',');
            specs_string += Strings::concat("R$",
                                            Hash::get_string_hash(remove_action.spec.name(), Hash::Algorithm::Sha256),
                                            ":",
                                            hash_triplet(remove_action.spec.triplet()));
        }

        for (auto&& install_action : plan.install_actions)
        {
            auto&& version_as_string =
                install_action.source_control_file_location.value_or_exit(VCPKG_LINE_INFO).to_versiont().to_string();
            if (!specs_string.empty()) specs_string.push_back(',');
            specs_string += Strings::concat(Hash::get_string_hash(install_action.spec.name(), Hash::Algorithm::Sha256),
                                            ":",
                                            hash_triplet(install_action.spec.triplet()),
                                            ":",
                                            Hash::get_string_hash(version_as_string, Hash::Algorithm::Sha256));
        }

        Metrics::g_metrics.lock()->track_property("installplan_1", specs_string);
    }
}
