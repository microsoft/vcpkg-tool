#include <vcpkg/base/files.h>
#include <vcpkg/base/hash.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.print.h>
#include <vcpkg/base/util.h>

#include <vcpkg/binarycaching.h>
#include <vcpkg/build.h>
#include <vcpkg/cmakevars.h>
#include <vcpkg/commands.setinstalled.h>
#include <vcpkg/configuration.h>
#include <vcpkg/dependencies.h>
#include <vcpkg/documentation.h>
#include <vcpkg/globalstate.h>
#include <vcpkg/help.h>
#include <vcpkg/input.h>
#include <vcpkg/install.h>
#include <vcpkg/installedpaths.h>
#include <vcpkg/metrics.h>
#include <vcpkg/paragraphs.h>
#include <vcpkg/remove.h>
#include <vcpkg/tools.h>
#include <vcpkg/vcpkglib.h>
#include <vcpkg/vcpkgpaths.h>
#include <vcpkg/xunitwriter.h>

#include <iterator>

namespace vcpkg
{
    using file_pack = std::pair<std::string, std::string>;

    InstallDir InstallDir::from_destination_root(const InstalledPaths& ip, Triplet t, const BinaryParagraph& pgh)
    {
        InstallDir dirs;
        dirs.m_destination = ip.triplet_dir(t);
        dirs.m_listfile = ip.listfile_path(pgh);
        return dirs;
    }

    const Path& InstallDir::destination() const { return this->m_destination; }

    const Path& InstallDir::listfile() const { return this->m_listfile; }

    void install_package_and_write_listfile(Filesystem& fs, const Path& source_dir, const InstallDir& destination_dir)
    {
        Checks::check_exit(VCPKG_LINE_INFO,
                           fs.exists(source_dir, IgnoreErrors{}),
                           Strings::concat("Source directory ", source_dir, "does not exist"));
        auto files = fs.get_files_recursive(source_dir, VCPKG_LINE_INFO);
        Util::erase_remove_if(files, [](Path& path) { return path.filename() == ".DS_Store"; });
        install_files_and_write_listfile(fs, source_dir, files, destination_dir);
    }
    void install_files_and_write_listfile(Filesystem& fs,
                                          const Path& source_dir,
                                          const std::vector<Path>& files,
                                          const InstallDir& destination_dir)
    {
        std::vector<std::string> output;

        const size_t prefix_length = source_dir.native().size();
        const Path& destination = destination_dir.destination();
        std::string destination_subdirectory = destination.filename().to_string();
        const Path& listfile = destination_dir.listfile();

        fs.create_directories(destination, VCPKG_LINE_INFO);
        const auto listfile_parent = listfile.parent_path();
        fs.create_directories(listfile_parent, VCPKG_LINE_INFO);

        output.push_back(destination_subdirectory + "/");
        for (auto&& file : files)
        {
            std::error_code ec;
            const auto status = fs.symlink_status(file, ec);
            if (ec)
            {
                print2(Color::error, "failed: ", file, ": ", ec.message(), "\n");
                continue;
            }

            const auto filename = file.filename();
            if (vcpkg::is_regular_file(status) &&
                (filename == "CONTROL" || filename == "vcpkg.json" || filename == "BUILD_INFO"))
            {
                // Do not copy the control file or manifest file
                continue;
            }

            const auto suffix = file.generic_u8string().substr(prefix_length + 1);
            const auto target = destination / suffix;

            auto this_output = Strings::concat(destination_subdirectory, "/", suffix);
            switch (status)
            {
                case FileType::directory:
                {
                    fs.create_directory(target, ec);
                    if (ec)
                    {
                        vcpkg::printf(Color::error, "failed: %s: %s\n", target, ec.message());
                    }

                    // Trailing backslash for directories
                    this_output.push_back('/');
                    output.push_back(std::move(this_output));
                    break;
                }
                case FileType::regular:
                {
                    if (fs.exists(target, IgnoreErrors{}))
                    {
                        print2(Color::warning, "File ", target, " was already present and will be overwritten\n");
                    }

                    fs.copy_file(file, target, CopyOptions::overwrite_existing, ec);
                    if (ec)
                    {
                        vcpkg::printf(Color::error, "failed: %s: %s\n", target, ec.message());
                    }

                    output.push_back(std::move(this_output));
                    break;
                }
                case FileType::symlink:
                case FileType::junction:
                {
                    if (fs.exists(target, IgnoreErrors{}))
                    {
                        print2(Color::warning, "File ", target, " was already present and will be overwritten\n");
                    }

                    fs.copy_symlink(file, target, ec);
                    if (ec)
                    {
                        vcpkg::printf(Color::error, "failed: %s: %s\n", target, ec.message());
                    }

                    output.push_back(std::move(this_output));
                    break;
                }
                default: vcpkg::printf(Color::error, "failed: %s: cannot handle file type\n", file); break;
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

    static SortedVector<std::string> build_list_of_package_files(const Filesystem& fs, const Path& package_dir)
    {
        std::vector<Path> package_file_paths = fs.get_files_recursive(package_dir, IgnoreErrors{});
        Util::erase_remove_if(package_file_paths, [](Path& path) { return path.filename() == ".DS_Store"; });
        const size_t package_remove_char_count = package_dir.native().size() + 1; // +1 for the slash
        auto package_files = Util::fmap(package_file_paths, [package_remove_char_count](const Path& target) {
            return std::string(target.generic_u8string(), package_remove_char_count);
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
        auto& fs = paths.get_filesystem();
        const auto& installed = paths.installed();
        const auto package_dir = paths.package_dir(bcf.core_paragraph.spec);
        Triplet triplet = bcf.core_paragraph.spec.triplet();
        const std::vector<StatusParagraphAndAssociatedFiles> pgh_and_files =
            get_installed_files(fs, installed, *status_db);

        const SortedVector<std::string> package_files = build_list_of_package_files(fs, package_dir);
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
            const auto triplet_install_path = installed.triplet_dir(triplet);
            vcpkg::printf(Color::error,
                          "The following files are already installed in %s and are in conflict with %s\n\n",
                          triplet_install_path.generic_u8string(),
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

        write_update(fs, installed, source_paragraph);
        status_db->insert(std::make_unique<StatusParagraph>(source_paragraph));

        std::vector<StatusParagraph> features_spghs;
        for (auto&& feature : bcf.features)
        {
            features_spghs.emplace_back();

            StatusParagraph& feature_paragraph = features_spghs.back();
            feature_paragraph.package = feature;
            feature_paragraph.want = Want::INSTALL;
            feature_paragraph.state = InstallState::HALF_INSTALLED;

            write_update(fs, installed, feature_paragraph);
            status_db->insert(std::make_unique<StatusParagraph>(feature_paragraph));
        }

        const InstallDir install_dir =
            InstallDir::from_destination_root(paths.installed(), triplet, bcf.core_paragraph);

        install_package_and_write_listfile(fs, paths.package_dir(bcf.core_paragraph.spec), install_dir);

        source_paragraph.state = InstallState::INSTALLED;
        write_update(fs, installed, source_paragraph);
        status_db->insert(std::make_unique<StatusParagraph>(source_paragraph));

        for (auto&& feature_paragraph : features_spghs)
        {
            feature_paragraph.state = InstallState::INSTALLED;
            write_update(fs, installed, feature_paragraph);
            status_db->insert(std::make_unique<StatusParagraph>(feature_paragraph));
        }

        return InstallResult::SUCCESS;
    }

    static ExtendedBuildResult perform_install_plan_action(const VcpkgCmdArguments& args,
                                                           const VcpkgPaths& paths,
                                                           InstallPlanAction& action,
                                                           StatusParagraphs& status_db,
                                                           BinaryCache& binary_cache,
                                                           const IBuildLogsRecorder& build_logs_recorder)
    {
        auto& fs = paths.get_filesystem();
        const InstallPlanType& plan_type = action.plan_type;

        const bool is_user_requested = action.request_type == RequestType::USER_REQUESTED;
        const bool use_head_version = Util::Enum::to_bool(action.build_options.use_head_version);

        if (plan_type == InstallPlanType::ALREADY_INSTALLED)
        {
            if (use_head_version && is_user_requested)
                msg::println(Color::warning, msgAlreadyInstalledNotHead, msg::spec = action.spec);
            else
                msg::println(Color::success, msgAlreadyInstalled, msg::spec = action.spec);
            return ExtendedBuildResult{BuildResult::SUCCEEDED};
        }

        if (plan_type == InstallPlanType::BUILD_AND_INSTALL)
        {
            std::unique_ptr<BinaryControlFile> bcf;
            auto restore = binary_cache.try_restore(action);
            if (restore == RestoreResult::restored)
            {
                auto maybe_bcf = Paragraphs::try_load_cached_package(fs, paths.package_dir(action.spec), action.spec);
                bcf = std::make_unique<BinaryControlFile>(std::move(maybe_bcf).value_or_exit(VCPKG_LINE_INFO));
            }
            else if (action.build_options.build_missing == BuildMissing::NO)
            {
                return ExtendedBuildResult{BuildResult::CACHE_MISSING};
            }
            else
            {
                if (use_head_version)
                    msg::println(msgBuildingFromHead, msg::spec = action.displayname());
                else
                    msg::println(msgBuildingPackage, msg::spec = action.displayname());

                auto result = build_package(args, paths, action, binary_cache, build_logs_recorder, status_db);

                if (BuildResult::DOWNLOADED == result.code)
                {
                    msg::println(Color::success, msgDownloadedSources, msg::spec = action.displayname());
                    return result;
                }

                if (result.code != BuildResult::SUCCEEDED)
                {
                    LocalizedString warnings;
                    for (auto&& msg : action.build_failure_messages)
                    {
                        warnings.append(msg).append_raw('\n');
                    }

                    if (!warnings.data().empty())
                    {
                        msg::print(Color::warning, warnings);
                    }

                    msg::println_error(create_error_message(result, action.spec));
                    return result;
                }

                bcf = std::move(result.binary_control_file);
            }
            // Build or restore succeeded and `bcf` is populated with the control file.
            Checks::check_exit(VCPKG_LINE_INFO, bcf != nullptr);

            const auto install_result = install_package(paths, *bcf, &status_db);
            BuildResult code;
            switch (install_result)
            {
                case InstallResult::SUCCESS: code = BuildResult::SUCCEEDED; break;
                case InstallResult::FILE_CONFLICTS: code = BuildResult::FILE_CONFLICTS; break;
                default: Checks::unreachable(VCPKG_LINE_INFO);
            }

            if (action.build_options.clean_packages == CleanPackages::YES)
            {
                fs.remove_all(paths.package_dir(action.spec), VCPKG_LINE_INFO);
            }

            if (action.build_options.clean_downloads == CleanDownloads::YES)
            {
                for (auto& p : fs.get_regular_files_non_recursive(paths.downloads, IgnoreErrors{}))
                {
                    fs.remove(p, VCPKG_LINE_INFO);
                }
            }

            return {code, std::move(bcf)};
        }

        if (plan_type == InstallPlanType::EXCLUDED)
        {
            msg::println(Color::warning, msgExcludedPackage, msg::spec = action.spec);
            return ExtendedBuildResult{BuildResult::EXCLUDED};
        }

        Checks::unreachable(VCPKG_LINE_INFO);
    }

    void InstallSummary::print() const
    {
        msg::println(msgResultsHeader);

        for (const SpecSummary& result : this->results)
        {
            msg::println(LocalizedString().append_indent().append_fmt_raw(
                "{}: {}: {}",
                result.get_spec(),
                to_string(result.build_result.value_or_exit(VCPKG_LINE_INFO).code),
                result.timing));
        }

        std::map<Triplet, BuildResultCounts> summary;
        for (const SpecSummary& r : this->results)
        {
            summary[r.get_spec().triplet()].increment(r.build_result.value_or_exit(VCPKG_LINE_INFO).code);
        }

        msg::println();

        for (auto&& entry : summary)
        {
            entry.second.println(entry.first);
        }
    }

    bool InstallSummary::failed() const
    {
        for (const auto& result : this->results)
        {
            if (result.build_result.value_or_exit(VCPKG_LINE_INFO).code != BuildResult::SUCCEEDED)
            {
                return true;
            }
        }
        return false;
    }

    struct TrackedPackageInstallGuard
    {
        SpecSummary* current_summary = nullptr;
        ElapsedTimer build_timer = ElapsedTimer::create_started();

        TrackedPackageInstallGuard(const size_t action_index,
                                   const size_t action_count,
                                   std::vector<SpecSummary>& results,
                                   const InstallPlanAction& action)
        {
            results.emplace_back(action);
            current_summary = &results.back();

            msg::println(msgInstallingPackage,
                         msg::action_index = action_index,
                         msg::count = action_count,
                         msg::spec = action.spec);
        }

        TrackedPackageInstallGuard(const size_t action_index,
                                   const size_t action_count,
                                   std::vector<SpecSummary>& results,
                                   const RemovePlanAction& action)
        {
            results.emplace_back(action);
            current_summary = &results.back();
            msg::println(Remove::msgRemovingPackage,
                         msg::action_index = action_index,
                         msg::count = action_count,
                         msg::spec = action.spec);
        }

        ~TrackedPackageInstallGuard()
        {
            current_summary->timing = build_timer.elapsed();
            msg::println(
                msgElapsedForPackage, msg::spec = current_summary->get_spec(), msg::elapsed = current_summary->timing);
        }

        TrackedPackageInstallGuard(const TrackedPackageInstallGuard&) = delete;
        TrackedPackageInstallGuard& operator=(const TrackedPackageInstallGuard&) = delete;
    };

    InstallSummary Install::perform(const VcpkgCmdArguments& args,
                                    ActionPlan& action_plan,
                                    const KeepGoing keep_going,
                                    const VcpkgPaths& paths,
                                    StatusParagraphs& status_db,
                                    BinaryCache& binary_cache,
                                    const IBuildLogsRecorder& build_logs_recorder,
                                    const CMakeVars::CMakeVarProvider& var_provider)
    {
        std::vector<SpecSummary> results;
        const size_t action_count = action_plan.remove_actions.size() + action_plan.install_actions.size();
        size_t action_index = 1;

        for (auto&& action : action_plan.remove_actions)
        {
            TrackedPackageInstallGuard this_install(action_index++, action_count, results, action);
            Remove::perform_remove_plan_action(paths, action, Remove::Purge::YES, &status_db);
            results.back().build_result.emplace(BuildResult::REMOVED);
        }

        for (auto&& action : action_plan.already_installed)
        {
            results.emplace_back(action);
            results.back().build_result.emplace(
                perform_install_plan_action(args, paths, action, status_db, binary_cache, build_logs_recorder));
        }

        compute_all_abis(paths, action_plan, var_provider, status_db);
        binary_cache.prefetch(action_plan.install_actions);
        for (auto&& action : action_plan.install_actions)
        {
            TrackedPackageInstallGuard this_install(action_index++, action_count, results, action);
            auto result =
                perform_install_plan_action(args, paths, action, status_db, binary_cache, build_logs_recorder);
            if (result.code != BuildResult::SUCCEEDED && keep_going == KeepGoing::NO)
            {
                Checks::msg_exit_with_message(
                    VCPKG_LINE_INFO,
                    create_user_troubleshooting_message(
                        action, paths, result.stdoutlog.then([&](auto&) -> Optional<Path> {
                            const auto issue_body_path = paths.installed().root() / "vcpkg" / "issue_body.md";
                            paths.get_filesystem().write_contents(
                                issue_body_path, create_github_issue(args, result, paths, action), VCPKG_LINE_INFO);
                            return issue_body_path;
                        })));
            }

            this_install.current_summary->build_result.emplace(std::move(result));
        }

        return InstallSummary{std::move(results)};
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
    static constexpr StringLiteral OPTION_ENFORCE_PORT_CHECKS = "enforce-port-checks";
    static constexpr StringLiteral OPTION_ALLOW_UNSUPPORTED_PORT = "allow-unsupported";
    static constexpr StringLiteral OPTION_NO_PRINT_USAGE = "no-print-usage";

    static constexpr std::array<CommandSwitch, 18> INSTALL_SWITCHES = {
        {{OPTION_DRY_RUN, "Do not actually build or install"},
         {OPTION_USE_HEAD_VERSION,
          "Install the libraries on the command line using the latest upstream sources (classic mode)"},
         {OPTION_NO_DOWNLOADS, "Do not download new sources"},
         {OPTION_ONLY_DOWNLOADS, "Download sources but don't build packages"},
         {OPTION_ONLY_BINARYCACHING, "Fail if cached binaries are not available"},
         {OPTION_RECURSE, "Allow removal of packages as part of installation"},
         {OPTION_KEEP_GOING, "Continue installing packages on failure"},
         {OPTION_EDITABLE,
          "Disable source re-extraction and binary caching for libraries on the command line (classic mode)"},

         {OPTION_USE_ARIA2, "Use aria2 to perform download tasks"},
         {OPTION_CLEAN_AFTER_BUILD, "Clean buildtrees, packages and downloads after building each package"},
         {OPTION_CLEAN_BUILDTREES_AFTER_BUILD, "Clean buildtrees after building each package"},
         {OPTION_CLEAN_PACKAGES_AFTER_BUILD, "Clean packages after building each package"},
         {OPTION_CLEAN_DOWNLOADS_AFTER_BUILD, "Clean downloads after building each package"},
         {OPTION_MANIFEST_NO_DEFAULT_FEATURES,
          "Don't install the default features from the top-level manifest (manifest mode)."},
         {OPTION_ENFORCE_PORT_CHECKS,
          "Fail install if a port has detected problems or attempts to use a deprecated feature"},
         {OPTION_PROHIBIT_BACKCOMPAT_FEATURES, ""},
         {OPTION_ALLOW_UNSUPPORTED_PORT, "Instead of erroring on an unsupported port, continue with a warning."},
         {OPTION_NO_PRINT_USAGE, "Don't print cmake usage information after install."}}};

    static constexpr std::array<CommandSetting, 2> INSTALL_SETTINGS = {{
        {OPTION_XUNIT, ""}, // internal use
        {OPTION_WRITE_PACKAGES_CONFIG,
         "Writes out a NuGet packages.config-formatted file for use with external binary caching.\nSee `vcpkg help "
         "binarycaching` for more information."},
    }};

    static constexpr std::array<CommandMultiSetting, 1> INSTALL_MULTISETTINGS = {{
        {OPTION_MANIFEST_FEATURE, "Additional feature from the top-level manifest to install (manifest mode)."},
    }};

    static std::vector<std::string> get_all_port_names(const VcpkgPaths& paths)
    {
        const auto& registries = paths.get_registry_set();

        std::vector<std::string> ret;
        for (const auto& registry : registries.registries())
        {
            const auto packages = registry.packages();
            ret.insert(ret.end(), packages.begin(), packages.end());
        }
        if (auto registry = registries.default_registry())
        {
            registry->get_all_port_names(ret);
        }

        Util::sort_unique_erase(ret);
        return ret;
    }

    const CommandStructure Install::COMMAND_STRUCTURE = {
        create_example_string("install zlib zlib:x64-windows curl boost"),
        0,
        SIZE_MAX,
        {INSTALL_SWITCHES, INSTALL_SETTINGS, INSTALL_MULTISETTINGS},
        &get_all_port_names,
    };

    // This command structure must share "critical" values (switches, number of arguments). It exists only to provide a
    // better example string.
    const CommandStructure MANIFEST_COMMAND_STRUCTURE = {
        create_example_string("install --triplet x64-windows"),
        0,
        SIZE_MAX,
        {INSTALL_SWITCHES, INSTALL_SETTINGS, INSTALL_MULTISETTINGS},
        nullptr,
    };
    void Install::print_usage_information(const BinaryParagraph& bpgh,
                                          std::set<std::string>& printed_usages,
                                          const Filesystem& fs,
                                          const InstalledPaths& installed)
    {
        auto message = get_cmake_usage(fs, installed, bpgh).message;
        if (!message.empty())
        {
            auto existing = printed_usages.lower_bound(message);
            if (existing == printed_usages.end() || *existing != message)
            {
                print2(message);
                printed_usages.insert(existing, std::move(message));
            }
        }
    }

    static const char* find_skip_add_library(const char* real_first, const char* first, const char* last)
    {
        static constexpr StringLiteral ADD_LIBRARY_CALL = "add_library(";

        for (;;)
        {
            first = Util::search(first, last, ADD_LIBRARY_CALL);
            if (first == last)
            {
                return first;
            }
            if (first == real_first || !ParserBase::is_word_char(*(first - 1)))
            {
                return first + ADD_LIBRARY_CALL.size();
            }
            ++first;
        }
    }

    std::vector<std::string> get_cmake_add_library_names(StringView cmake_file)
    {
        constexpr static auto is_library_name_char = [](char ch) {
            return ch != ')' && ch != '$' && !ParserBase::is_whitespace(ch);
        };

        const auto real_first = cmake_file.begin();
        auto first = real_first;
        const auto last = cmake_file.end();

        std::vector<std::string> res;
        for (;;)
        {
            first = find_skip_add_library(real_first, first, last);
            if (first == last)
            {
                return res;
            }
            auto start_of_library_name = std::find_if_not(first, last, ParserBase::is_whitespace);
            auto end_of_library_name = std::find_if_not(start_of_library_name, last, is_library_name_char);
            if (end_of_library_name == start_of_library_name)
            {
                first = end_of_library_name;
                continue;
            }
            res.emplace_back(start_of_library_name, end_of_library_name);
        }
    }

    CMakeUsageInfo get_cmake_usage(const Filesystem& fs, const InstalledPaths& installed, const BinaryParagraph& bpgh)
    {
        CMakeUsageInfo ret;

        std::error_code ec;

        auto usage_file = installed.usage_file(bpgh.spec);
        if (fs.is_regular_file(usage_file))
        {
            ret.usage_file = true;
            auto contents = fs.read_contents(usage_file, ec);
            if (!ec)
            {
                ret.message = std::move(contents);
                ret.message.push_back('\n');
            }

            return ret;
        }

        auto files = fs.read_lines(installed.listfile_path(bpgh), ec);
        if (!ec)
        {
            std::unordered_map<std::string, std::string> config_files;
            std::map<std::string, std::vector<std::string>> library_targets;
            bool is_header_only = true;
            std::string header_path;

            for (auto&& suffix : files)
            {
                if (Strings::contains(suffix, "/share/") && Strings::ends_with(suffix, ".cmake"))
                {
                    // CMake file is inside the share folder
                    const auto path = installed.root() / suffix;
                    const auto find_package_name = Path(path.parent_path()).filename().to_string();
                    const auto contents = fs.read_contents(path, ec);
                    if (!ec)
                    {
                        auto targets = get_cmake_add_library_names(contents);
                        if (!targets.empty())
                        {
                            auto& all_targets = library_targets[find_package_name];
                            all_targets.insert(all_targets.end(),
                                               std::make_move_iterator(targets.begin()),
                                               std::make_move_iterator(targets.end()));
                        }
                    }

                    auto filename = Path(suffix).filename().to_string();
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
                if (Strings::contains(suffix, "/lib/") || Strings::contains(suffix, "/bin/"))
                {
                    if (!Strings::ends_with(suffix, ".pc") && !Strings::ends_with(suffix, "/")) is_header_only = false;
                }

                if (is_header_only && header_path.empty())
                {
                    const auto it = suffix.find("/include/");
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
                        if (n.empty() || ParserBase::is_ascii_digit(n[0]))
                        {
                            n.insert(n.begin(), '_');
                        }
                        return n;
                    };

                    const auto name = cmakeify(bpgh.spec.name());
                    auto msg = msg::format(msgHeaderOnlyUsage, msg::package_name = bpgh.spec.name()).extract_data();
                    Strings::append(msg, "\n\n");
                    Strings::append(msg, "    find_path(", name, "_INCLUDE_DIRS \"", header_path, "\")\n");
                    Strings::append(msg, "    target_include_directories(main PRIVATE ${", name, "_INCLUDE_DIRS})\n\n");

                    ret.message = std::move(msg);
                }
            }
            else
            {
                auto msg = msg::format(msgCMakeTargetsUsage, msg::package_name = bpgh.spec.name()).append_raw("\n\n");
                msg.append_indent().append(msgCMakeTargetsUsageHeuristicMessage).append_raw('\n');

                for (auto&& library_target_pair : library_targets)
                {
                    auto config_it = config_files.find(library_target_pair.first);
                    msg.append_indent();
                    msg.append_fmt_raw("find_package({} CONFIG REQUIRED)",
                                       config_it == config_files.end() ? library_target_pair.first : config_it->second);
                    msg.append_raw('\n');

                    auto& targets = library_target_pair.second;
                    Util::sort_unique_erase(targets, [](const std::string& l, const std::string& r) {
                        if (l.size() < r.size()) return true;
                        if (l.size() > r.size()) return false;
                        return l < r;
                    });

                    if (targets.size() > 4)
                    {
                        auto omitted = targets.size() - 4;
                        library_target_pair.second.erase(targets.begin() + 4, targets.end());
                        msg.append_indent()
                            .append_raw("# ")
                            .append(msgCmakeTargetsExcluded, msg::count = omitted)
                            .append_raw('\n');
                    }

                    msg.append_indent()
                        .append_fmt_raw("target_link_libraries(main PRIVATE {})", Strings::join(" ", targets))
                        .append_raw("\n\n");
                }

                ret.message = msg.extract_data();
            }
            ret.cmake_targets_map = std::move(library_targets);
        }
        return ret;
    }

    void Install::perform_and_exit(const VcpkgCmdArguments& args,
                                   const VcpkgPaths& paths,
                                   Triplet default_triplet,
                                   Triplet host_triplet)
    {
        const ParsedArguments options =
            args.parse_arguments(paths.manifest_mode_enabled() ? MANIFEST_COMMAND_STRUCTURE : COMMAND_STRUCTURE);

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
        const KeepGoing keep_going = Util::Sets::contains(options.switches, OPTION_KEEP_GOING) || only_downloads
                                         ? KeepGoing::YES
                                         : KeepGoing::NO;
        const bool prohibit_backcompat_features =
            Util::Sets::contains(options.switches, (OPTION_PROHIBIT_BACKCOMPAT_FEATURES)) ||
            Util::Sets::contains(options.switches, (OPTION_ENFORCE_PORT_CHECKS));
        const auto unsupported_port_action = Util::Sets::contains(options.switches, OPTION_ALLOW_UNSUPPORTED_PORT)
                                                 ? UnsupportedPortAction::Warn
                                                 : UnsupportedPortAction::Error;
        const bool no_print_usage = Util::Sets::contains(options.switches, OPTION_NO_PRINT_USAGE);

        LockGuardPtr<Metrics>(g_metrics)->track_property("install_manifest_mode", paths.manifest_mode_enabled());

        if (auto p = paths.get_manifest().get())
        {
            bool failure = false;
            if (!args.command_arguments.empty())
            {
                msg::println_error(msgErrorIndividualPackagesUnsupported);
                msg::println(Color::error, msg::msgSeeURL, msg::url = docs::manifests_url);
                failure = true;
            }
            if (use_head_version)
            {
                msg::println_error(msgErrorInvalidManifestModeOption, msg::option = OPTION_USE_HEAD_VERSION);
                failure = true;
            }
            if (is_editable)
            {
                msg::println_error(msgErrorInvalidManifestModeOption, msg::option = OPTION_EDITABLE);
                failure = true;
            }
            if (failure)
            {
                msg::println(msgUsingManifestAt, msg::path = p->path);
                print2("\n");
                print_usage(MANIFEST_COMMAND_STRUCTURE);
                Checks::exit_fail(VCPKG_LINE_INFO);
            }
        }
        else
        {
            bool failure = false;
            if (args.command_arguments.empty())
            {
                msg::println_error(msgErrorRequirePackagesList);
                failure = true;
            }
            if (Util::Sets::contains(options.switches, OPTION_MANIFEST_NO_DEFAULT_FEATURES))
            {
                msg::println_error(msgErrorInvalidClassicModeOption, msg::option = OPTION_MANIFEST_NO_DEFAULT_FEATURES);
                failure = true;
            }
            if (Util::Sets::contains(options.multisettings, OPTION_MANIFEST_FEATURE))
            {
                msg::println_error(msgErrorInvalidClassicModeOption, msg::option = OPTION_MANIFEST_FEATURE);
                failure = true;
            }
            if (failure)
            {
                print2("\n");
                print_usage(COMMAND_STRUCTURE);
                Checks::exit_fail(VCPKG_LINE_INFO);
            }
        }

        BinaryCache binary_cache;
        if (!only_downloads)
        {
            binary_cache.install_providers_for(args, paths);
        }

        auto& fs = paths.get_filesystem();

        DownloadTool download_tool = DownloadTool::BUILT_IN;
        if (use_aria2) download_tool = DownloadTool::ARIA2;

        const BuildPackageOptions install_plan_options = {
            Util::Enum::to_enum<BuildMissing>(!no_build_missing),
            Util::Enum::to_enum<UseHeadVersion>(use_head_version),
            Util::Enum::to_enum<AllowDownloads>(!no_downloads),
            Util::Enum::to_enum<OnlyDownloads>(only_downloads),
            Util::Enum::to_enum<CleanBuildtrees>(clean_after_build || clean_buildtrees_after_build),
            Util::Enum::to_enum<CleanPackages>(clean_after_build || clean_packages_after_build),
            Util::Enum::to_enum<CleanDownloads>(clean_after_build || clean_downloads_after_build),
            download_tool,
            PurgeDecompressFailure::NO,
            Util::Enum::to_enum<Editable>(is_editable),
            prohibit_backcompat_features ? BackcompatFeatures::PROHIBIT : BackcompatFeatures::ALLOW,
            Util::Enum::to_enum<PrintUsage>(!no_print_usage),
        };

        auto var_provider_storage = CMakeVars::make_triplet_cmake_var_provider(paths);
        auto& var_provider = *var_provider_storage;

        if (auto manifest = paths.get_manifest().get())
        {
            Optional<Path> pkgsconfig;
            auto it_pkgsconfig = options.settings.find(OPTION_WRITE_PACKAGES_CONFIG);
            if (it_pkgsconfig != options.settings.end())
            {
                LockGuardPtr<Metrics>(g_metrics)->track_property("x-write-nuget-packages-config", "defined");
                pkgsconfig = Path(it_pkgsconfig->second);
            }
            auto maybe_manifest_scf =
                SourceControlFile::parse_project_manifest_object(manifest->path, manifest->manifest, stdout_sink);
            if (!maybe_manifest_scf)
            {
                print_error_message(maybe_manifest_scf.error());
                print2("See ", docs::manifests_url, " for more information.\n");
                Checks::exit_fail(VCPKG_LINE_INFO);
            }

            auto manifest_scf = std::move(maybe_manifest_scf).value_or_exit(VCPKG_LINE_INFO);
            const auto& manifest_core = *manifest_scf->core_paragraph;
            if (auto maybe_error = manifest_scf->check_against_feature_flags(
                    manifest->path, paths.get_feature_flags(), paths.get_registry_set().is_default_builtin_registry()))
            {
                Checks::exit_with_message(VCPKG_LINE_INFO, maybe_error.value_or_exit(VCPKG_LINE_INFO));
            }

            std::vector<std::string> features;
            auto manifest_feature_it = options.multisettings.find(OPTION_MANIFEST_FEATURE);
            if (manifest_feature_it != options.multisettings.end())
            {
                features.insert(features.end(), manifest_feature_it->second.begin(), manifest_feature_it->second.end());
            }
            if (Util::Sets::contains(options.switches, OPTION_MANIFEST_NO_DEFAULT_FEATURES))
            {
                features.push_back("core");
            }

            auto core_it = std::remove(features.begin(), features.end(), "core");
            if (core_it == features.end())
            {
                const auto& default_features = manifest_core.default_features;
                features.insert(features.end(), default_features.begin(), default_features.end());
            }
            else
            {
                features.erase(core_it, features.end());
            }
            Util::sort_unique_erase(features);

            auto dependencies = manifest_core.dependencies;
            for (const auto& feature : features)
            {
                auto it = Util::find_if(
                    manifest_scf->feature_paragraphs,
                    [&feature](const std::unique_ptr<FeatureParagraph>& fpgh) { return fpgh->name == feature; });

                if (it == manifest_scf->feature_paragraphs.end())
                {
                    vcpkg::printf(Color::warning,
                                  "Warning: feature %s was passed, but that is not a feature that %s supports.",
                                  feature,
                                  manifest_core.name);
                }
                else
                {
                    dependencies.insert(
                        dependencies.end(), it->get()->dependencies.begin(), it->get()->dependencies.end());
                }
            }

            if (std::any_of(dependencies.begin(), dependencies.end(), [](const Dependency& dep) {
                    return dep.constraint.type != VersionConstraintKind::None;
                }))
            {
                LockGuardPtr<Metrics>(g_metrics)->track_property("manifest_version_constraint", "defined");
            }

            if (!manifest_core.overrides.empty())
            {
                LockGuardPtr<Metrics>(g_metrics)->track_property("manifest_overrides", "defined");
            }

            auto verprovider = make_versioned_portfile_provider(paths);
            auto baseprovider = make_baseline_provider(paths);

            std::vector<std::string> extended_overlay_ports;
            const bool add_builtin_ports_directory_as_overlay =
                paths.get_registry_set().is_default_builtin_registry() && !paths.use_git_default_registry();
            extended_overlay_ports.reserve(args.overlay_ports.size() + add_builtin_ports_directory_as_overlay);
            extended_overlay_ports = args.overlay_ports;
            if (add_builtin_ports_directory_as_overlay)
            {
                extended_overlay_ports.emplace_back(paths.builtin_ports_directory().native());
            }

            auto oprovider =
                make_manifest_provider(paths, extended_overlay_ports, manifest->path, std::move(manifest_scf));
            PackageSpec toplevel{manifest_core.name, default_triplet};
            auto install_plan = create_versioned_install_plan(*verprovider,
                                                              *baseprovider,
                                                              *oprovider,
                                                              var_provider,
                                                              dependencies,
                                                              manifest_core.overrides,
                                                              toplevel,
                                                              host_triplet,
                                                              unsupported_port_action)
                                    .value_or_exit(VCPKG_LINE_INFO);

            for (const auto& warning : install_plan.warnings)
            {
                print2(Color::warning, warning, '\n');
            }
            for (InstallPlanAction& action : install_plan.install_actions)
            {
                action.build_options = install_plan_options;
                action.build_options.use_head_version = UseHeadVersion::NO;
                action.build_options.editable = Editable::NO;
            }

            // If the manifest refers to itself, it will be added to the install plan.
            Util::erase_remove_if(install_plan.install_actions,
                                  [&toplevel](auto&& action) { return action.spec == toplevel; });

            PathsPortFileProvider provider(paths, std::move(oprovider));
            Commands::SetInstalled::perform_and_exit_ex(args,
                                                        paths,
                                                        provider,
                                                        binary_cache,
                                                        var_provider,
                                                        std::move(install_plan),
                                                        dry_run ? Commands::DryRun::Yes : Commands::DryRun::No,
                                                        pkgsconfig,
                                                        host_triplet,
                                                        keep_going);
        }

        PathsPortFileProvider provider(paths, make_overlay_provider(paths, args.overlay_ports));

        const std::vector<FullPackageSpec> specs = Util::fmap(args.command_arguments, [&](auto&& arg) {
            return check_and_get_full_package_spec(
                std::string(arg), default_triplet, COMMAND_STRUCTURE.example_text, paths);
        });

        // create the plan
        print2("Computing installation plan...\n");
        StatusParagraphs status_db = database_load_check(fs, paths.installed());

        // Note: action_plan will hold raw pointers to SourceControlFileLocations from this map
        auto action_plan = create_feature_install_plan(
            provider, var_provider, specs, status_db, {host_triplet, unsupported_port_action});

        for (const auto& warning : action_plan.warnings)
        {
            print2(Color::warning, warning, '\n');
        }
        for (auto&& action : action_plan.install_actions)
        {
            action.build_options = install_plan_options;
            if (action.request_type != RequestType::USER_REQUESTED)
            {
                action.build_options.use_head_version = UseHeadVersion::NO;
                action.build_options.editable = Editable::NO;
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
                               common_triplet,
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

        print_plan(action_plan, is_recursive, paths.builtin_ports_directory());

        auto it_pkgsconfig = options.settings.find(OPTION_WRITE_PACKAGES_CONFIG);
        if (it_pkgsconfig != options.settings.end())
        {
            LockGuardPtr<Metrics>(g_metrics)->track_property("x-write-nuget-packages-config", "defined");
            compute_all_abis(paths, action_plan, var_provider, status_db);

            auto pkgsconfig_path = paths.original_cwd / it_pkgsconfig->second;
            auto pkgsconfig_contents = generate_nuget_packages_config(action_plan);
            fs.write_contents(pkgsconfig_path, pkgsconfig_contents, VCPKG_LINE_INFO);
            print2("Wrote NuGet packages config information to ", pkgsconfig_path, "\n");
        }

        if (dry_run)
        {
            Checks::exit_success(VCPKG_LINE_INFO);
        }

        paths.flush_lockfile();

        track_install_plan(action_plan);

        const InstallSummary summary = Install::perform(
            args, action_plan, keep_going, paths, status_db, binary_cache, null_build_logs_recorder(), var_provider);

        print2("\nTotal elapsed time: ", GlobalState::timer.to_string(), "\n\n");

        if (keep_going == KeepGoing::YES)
        {
            summary.print();
        }

        auto it_xunit = options.settings.find(OPTION_XUNIT);
        if (it_xunit != options.settings.end())
        {
            XunitWriter xwriter;

            for (auto&& result : summary.results)
            {
                xwriter.add_test_results(result.get_spec(),
                                         result.build_result.value_or_exit(VCPKG_LINE_INFO).code,
                                         result.timing,
                                         result.start_time,
                                         "",
                                         {});
            }

            fs.write_contents(it_xunit->second, xwriter.build_xml(default_triplet), VCPKG_LINE_INFO);
        }

        if (install_plan_options.print_usage == PrintUsage::YES)
        {
            std::set<std::string> printed_usages;
            for (auto&& result : summary.results)
            {
                if (!result.is_user_requested_install()) continue;
                auto bpgh = result.get_binary_paragraph();
                // If a package failed to build, don't attempt to print usage.
                // e.g. --keep-going
                if (!bpgh) continue;
                Install::print_usage_information(*bpgh, printed_usages, fs, paths.installed());
            }
        }

        Checks::exit_with_code(VCPKG_LINE_INFO, summary.failed());
    }

    void InstallCommand::perform_and_exit(const VcpkgCmdArguments& args,
                                          const VcpkgPaths& paths,
                                          Triplet default_triplet,
                                          Triplet host_triplet) const
    {
        Install::perform_and_exit(args, paths, default_triplet, host_triplet);
    }

    SpecSummary::SpecSummary(const InstallPlanAction& action)
        : build_result()
        , timing()
        , start_time(std::chrono::system_clock::now())
        , m_install_action(&action)
        , m_spec(action.spec)
    {
    }

    SpecSummary::SpecSummary(const RemovePlanAction& action)
        : build_result()
        , timing()
        , start_time(std::chrono::system_clock::now())
        , m_install_action(nullptr)
        , m_spec(action.spec)
    {
    }

    const BinaryParagraph* SpecSummary::get_binary_paragraph() const
    {
        // if we actually built this package, the build result will contain the BinaryParagraph for what we built.
        if (const auto br = build_result.get())
        {
            if (br->binary_control_file)
            {
                return &br->binary_control_file->core_paragraph;
            }
        }

        // if the package was already installed, the installed_package record will contain the BinaryParagraph for what
        // was built before.
        if (m_install_action)
        {
            if (auto p_status = m_install_action->installed_package.get())
            {
                return &p_status->core->package;
            }
        }

        return nullptr;
    }

    bool SpecSummary::is_user_requested_install() const
    {
        return m_install_action && m_install_action->request_type == RequestType::USER_REQUESTED;
    }

    struct InstallPlanActionMetrics
    {
        enum Action
        {
            Install,
            Remove
        };

        Action action;
        std::string port_hash;
        std::string triplet_hash;
        std::string version_hash;
        std::string origin;

        Json::Object serialize() const
        {
            Json::Object obj;
            obj.insert_or_replace("action", action == Action::Install ? "install" : "remove");
            obj.insert_or_replace("port", port_hash);
            obj.insert_or_replace("triplet", triplet_hash);
            obj.insert_or_replace("version", version_hash);
            obj.insert_or_replace("origin", origin);
            return obj;
        }
    };

    void track_install_plan(ActionPlan& plan)
    {
        std::vector<InstallPlanActionMetrics> metrics;

        Cache<Triplet, std::string> triplet_hashes;

        auto hash_triplet = [&triplet_hashes](Triplet t) -> const std::string& {
            return triplet_hashes.get_lazy(
                t, [t]() { return Hash::get_string_hash(t.canonical_name(), Hash::Algorithm::Sha256); });
        };

        std::string specs_string;
        for (auto&& remove_action : plan.remove_actions)
        {
            InstallPlanActionMetrics action_metrics;
            action_metrics.action = InstallPlanActionMetrics::Action::Remove;
            action_metrics.port_hash = Hash::get_string_hash(remove_action.spec.name(), Hash::Algorithm::Sha256);
            action_metrics.triplet_hash = hash_triplet(remove_action.spec.triplet());
            metrics.push_back(action_metrics);

            // keep installplan_1 telemetry around
            if (!specs_string.empty()) specs_string.push_back(',');
            specs_string += Strings::concat("R$", action_metrics.port_hash, ":", action_metrics.triplet_hash);
        }

        for (auto&& install_action : plan.install_actions)
        {
            const auto& spec = install_action.spec;
            const auto& scfl = install_action.source_control_file_and_location.value_or_exit(VCPKG_LINE_INFO);

            InstallPlanActionMetrics action_metrics;
            action_metrics.action = InstallPlanActionMetrics::Action::Install;
            action_metrics.port_hash = Hash::get_string_hash(spec.name(), Hash::Algorithm::Sha256);
            action_metrics.triplet_hash = hash_triplet(spec.triplet());
            action_metrics.version_hash = Hash::get_string_hash(scfl.to_version().to_string(), Hash::Algorithm::Sha256);
            action_metrics.origin = scfl.origin;
            metrics.push_back(action_metrics);

            // installplan_1
            if (!specs_string.empty()) specs_string.push_back(',');
            specs_string += Strings::concat(
                action_metrics.port_hash, ":", action_metrics.triplet_hash, ":", action_metrics.version_hash);
        }

        Json::Array metrics_array;
        for (auto&& m : metrics)
        {
            metrics_array.push_back(m.serialize());
        }
        LockGuardPtr<Metrics>(g_metrics)->track_property("installplan_1", specs_string);
        LockGuardPtr<Metrics>(g_metrics)->track_property("installplan_2", metrics_array);
    }
}
