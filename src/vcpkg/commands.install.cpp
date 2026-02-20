#include <vcpkg/base/fwd/message_sinks.h>

#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/hash.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/parallel-algorithms.h>
#include <vcpkg/base/path.h>
#include <vcpkg/base/sortedvector.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.h>
#include <vcpkg/base/util.h>

#include <vcpkg/binarycaching.h>
#include <vcpkg/cmakevars.h>
#include <vcpkg/commands.build.h>
#include <vcpkg/commands.install.h>
#include <vcpkg/commands.remove.h>
#include <vcpkg/commands.set-installed.h>
#include <vcpkg/configuration.h>
#include <vcpkg/dependencies.h>
#include <vcpkg/documentation.h>
#include <vcpkg/input.h>
#include <vcpkg/installedpaths.h>
#include <vcpkg/metrics.h>
#include <vcpkg/paragraphs.h>
#include <vcpkg/portfileprovider.h>
#include <vcpkg/tools.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkglib.h>
#include <vcpkg/vcpkgpaths.h>
#include <vcpkg/xunitwriter.h>

#include <iterator>
#include <unordered_map>

namespace
{
    using namespace vcpkg;

    // Editable port source info parsed from portfile.cmake
    // Initialize an editable port: copy port files to editable-ports/<port>/port/ folder
    // Structure:
    //   editable-ports/<port>/port/     <- port files (portfile.cmake, vcpkg.json, etc.)
    //   editable-ports/<port>/sources/  <- source code (src1/, src2/, etc. for multi-source ports)
    //   editable-ports/<port>/build/    <- build artifacts
    //   editable-ports/<port>/packages/ <- package output
    // Note: Source handling is done by CMake macros (vcpkg_from_github, etc.)
    // which check _VCPKG_EDITABLE flag and use local source if available
    void initialize_editable_port(const Filesystem& fs,
                                  const SourceControlFileAndLocation& scfl,
                                  const Path& editable_port_dir)
    {
        const auto port_dir = scfl.port_directory();
        const auto port_name = port_dir.filename().to_string();

        msg::println(Color::success, LocalizedString::from_raw("Initializing editable port: " + port_name));

        // Copy all port files to <editable_port_dir>/port/
        const auto port_files_path = editable_port_dir / "port";
        fs.create_directories(port_files_path, VCPKG_LINE_INFO);
        fs.copy_regular_recursive(port_dir, port_files_path, VCPKG_LINE_INFO);

        msg::println(LocalizedString::from_raw("  Port files copied to: " + port_files_path.native()));
        msg::println(LocalizedString::from_raw("  Sources will be cloned automatically on first build to: " +
                                               (editable_port_dir / "sources").native()));
    }

    struct InstalledFile
    {
        std::string file_path;
        std::string package_display_name;

        InstalledFile(std::string&& file_path_, const std::string& package_display_name_)
            : file_path(std::move(file_path_)), package_display_name(package_display_name_)
        {
        }

        InstalledFile(const InstalledFile&) = default;
        InstalledFile(InstalledFile&&) = default;
        InstalledFile& operator=(const InstalledFile&) = default;
        InstalledFile& operator=(InstalledFile&&) = default;
    };

    struct InstalledFilePathCompare
    {
        bool operator()(const std::string& lhs, const InstalledFile& rhs) const noexcept
        {
            return Strings::case_insensitive_ascii_less(lhs, rhs.file_path);
        }
        bool operator()(const InstalledFile& lhs, const std::string& rhs) const noexcept
        {
            return Strings::case_insensitive_ascii_less(lhs.file_path, rhs);
        }
        bool operator()(const InstalledFile& lhs, const InstalledFile& rhs) const noexcept
        {
            return Strings::case_insensitive_ascii_less(lhs.file_path, rhs.file_path);
        }
    };

    // Restore timestamp from old file to new file if they are identical
    void restore_timestamp_if_unchanged(const Filesystem& fs, const Path& new_file, const Path& old_file_in_temp)
    {
        if (!fs.exists(old_file_in_temp, IgnoreErrors{})) return;
        if (!fs.exists(new_file, IgnoreErrors{})) return;

        if (fs.files_are_identical(new_file, old_file_in_temp))
        {
            std::error_code ec;
            const auto old_timestamp = fs.last_write_time(old_file_in_temp, ec);
            if (!ec)
            {
                fs.last_write_time(null_diagnostic_context, new_file, old_timestamp);
                Debug::println("Restored timestamp for unchanged file: ", new_file);
            }
        }
    }

    static constexpr StringLiteral SYMLINK_STATUS = "symlink_status";
    static constexpr StringLiteral STATUS = "status";
}

namespace vcpkg
{
    void install_files_and_write_listfile(const Filesystem& fs,
                                          const Path& source_dir,
                                          const std::vector<std::string>& proximate_files,
                                          const Path& destination_installed,
                                          StringView triplet_canonical_name,
                                          const Path& listfile,
                                          const SymlinkHydrate hydrate)
    {
        auto destination_triplet = destination_installed / triplet_canonical_name;
        fs.create_directories(destination_triplet, VCPKG_LINE_INFO);
        const auto listfile_parent = listfile.parent_path();
        fs.create_directories(listfile_parent, VCPKG_LINE_INFO);

        std::string listfile_triplet_prefix;
        listfile_triplet_prefix.reserve(triplet_canonical_name.size() + 1); // +1 for the slash
        listfile_triplet_prefix.append(triplet_canonical_name.data(), triplet_canonical_name.size()).push_back('/');

        std::vector<std::string> listfile_lines;
        listfile_lines.push_back(listfile_triplet_prefix);

        std::mutex console_mutex;
        std::vector<Path> source_files(proximate_files.size());
        std::vector<vcpkg::FileType> statuses(proximate_files.size());
        execute_in_parallel(proximate_files.size(), [&, hydrate](size_t idx) {
            const auto& proximate_file = proximate_files[idx];
            const auto filename = parse_filename(proximate_file);
            if (filename == FileDotDsStore)
            {
                // Do not copy .DS_Store files (leaves file_type::none)
                return;
            }

            auto source_file = source_dir / proximate_file;
            std::error_code ec;
            vcpkg::FileType status;
            const StringLiteral* status_call_name;
            switch (hydrate)
            {
                case SymlinkHydrate::CopySymlinks:
                    status = fs.symlink_status(source_file, ec);
                    status_call_name = &SYMLINK_STATUS;
                    break;
                case SymlinkHydrate::CopyData:
                    status = fs.status(source_file, ec);
                    status_call_name = &STATUS;
                    break;
                default: Checks::unreachable(VCPKG_LINE_INFO);
            }
            if (ec)
            {
                std::lock_guard<std::mutex> lock(console_mutex);
                msg::println_warning(format_filesystem_call_error(ec, *status_call_name, {source_file}));
                status = FileType::none;
            }
            else
            {
                switch (status)
                {
                    case FileType::regular:
                        if (filename == FileControl || filename == FileVcpkgDotJson || filename == FileBuildInfo)
                        {
                            // Do not copy the control file or manifest file
                            status = FileType::none;
                        }
                        break;
                    case FileType::directory:
                    case FileType::symlink:
                    case FileType::junction: break;
                    default:
                        std::lock_guard<std::mutex> lock(console_mutex);
                        msg::println_error(msgInvalidFileType, msg::path = source_file);
                        status = FileType::none;
                }
            }

            source_files[idx] = std::move(source_file);
            statuses[idx] = status;
        });

        // At this point, each corresponding index between proximate_files, filenames, source_files, and statuses is
        // either has file_type::none (skip), or is filled out
        // Copy all the non-regular-files serially to avoid races with missing parent directories.
        std::vector<Path> target_regular_files(proximate_files.size());
        std::error_code ec;
        std::string list_listfile_line;
        for (std::size_t idx = 0; idx < proximate_files.size(); ++idx)
        {
            const auto& proximate_file = proximate_files[idx];
            list_listfile_line = listfile_triplet_prefix;
            list_listfile_line.append(proximate_file);
            auto target = destination_triplet / proximate_file;
            switch (statuses[idx])
            {
                case FileType::directory:
                {
                    fs.create_directory(target, ec);
                    if (ec)
                    {
                        msg::println_error(msgInstallFailed, msg::path = target, msg::error_msg = ec.message());
                    }

                    // Trailing slash for directories
                    list_listfile_line.push_back('/');
                    listfile_lines.push_back(list_listfile_line);
                    break;
                }
                case FileType::regular:
                {
                    target_regular_files[idx] = std::move(target);
                    listfile_lines.push_back(list_listfile_line);
                    break;
                }
                case FileType::symlink:
                case FileType::junction:
                {
                    if (fs.exists(target, IgnoreErrors{}))
                    {
                        msg::println_warning(msgOverwritingFile, msg::path = target);
                    }

                    fs.copy_symlink(source_files[idx], target, ec);
                    if (ec)
                    {
                        msg::println_error(msgInstallFailed, msg::path = target, msg::error_msg = ec.message());
                    }

                    listfile_lines.push_back(list_listfile_line);
                    break;
                }
                case FileType::none: break; // skip or error case
                default: Checks::unreachable(VCPKG_LINE_INFO); break;
            }
        }

        // explicit captures to avoid capturing outer 'ec'
        execute_in_parallel(
            proximate_files.size(), [&statuses, &target_regular_files, &source_files, &fs, &console_mutex](size_t idx) {
                if (statuses[idx] == FileType::regular)
                {
                    const auto& target = target_regular_files[idx];
                    if (fs.exists(target, IgnoreErrors{}))
                    {
                        {
                            std::lock_guard<std::mutex> lock(console_mutex);
                            msg::println_warning(msgOverwritingFile, msg::path = target);
                        } // unlock

                        fs.remove_all(target, IgnoreErrors{});
                    }

                    std::error_code ec;
                    fs.create_hard_link(source_files[idx], target, ec);
                    if (ec)
                    {
                        {
                            std::lock_guard<std::mutex> lock(console_mutex);
                            Debug::println("Install from packages to installed: Fallback to copy "
                                           "instead creating hard links because of: ",
                                           ec.message());
                        } // unlock

                        fs.copy_file(source_files[idx], target, CopyOptions::overwrite_existing, ec);
                        if (ec)
                        {
                            std::lock_guard<std::mutex> lock(console_mutex);
                            msg::println_error(msgInstallFailed, msg::path = target, msg::error_msg = ec.message());
                        } // unlock
                    }
                }
            });

        std::sort(listfile_lines.begin(), listfile_lines.end());
        fs.write_lines(listfile, listfile_lines, VCPKG_LINE_INFO);
    }

    static std::vector<std::string> build_list_of_package_files(const ReadOnlyFilesystem& fs, const Path& package_dir)
    {
        auto result = Util::fmap(fs.get_files_recursive_lexically_proximate(package_dir, IgnoreErrors{}),
                                 [](Path&& target) { return std::move(target).generic_u8string(); });
        Util::sort(result, Strings::case_insensitive_ascii_less);
        return result;
    }

    static std::vector<InstalledFile> build_list_of_installed_files(
        std::vector<StatusParagraphAndAssociatedFiles>&& pgh_and_files, Triplet triplet)
    {
        const size_t installed_remove_char_count = triplet.canonical_name().size() + 1; // +1 for the slash
        std::vector<InstalledFile> output;
        for (StatusParagraphAndAssociatedFiles& t : pgh_and_files)
        {
            if (t.pgh.package.spec.triplet() != triplet)
            {
                continue;
            }

            const std::string package_display_name = t.pgh.package.display_name();
            for (std::string& file : t.files)
            {
                file.erase(0, installed_remove_char_count);
                output.emplace_back(std::move(file), package_display_name);
            }
        }

        return output;
    }

    static bool check_for_install_conflicts(const Filesystem& fs,
                                            const std::vector<std::string>& package_files,
                                            const InstalledPaths& installed,
                                            const StatusParagraphs& status_db,
                                            const PackageSpec& spec)
    {
        std::vector<InstalledFile> installed_files =
            build_list_of_installed_files(get_installed_files_and_upgrade(fs, installed, status_db), spec.triplet());
        std::vector<InstalledFile> intersection;

        Util::sort(installed_files, InstalledFilePathCompare{});
        assert(std::is_sorted(package_files.begin(), package_files.end(), Strings::case_insensitive_ascii_less));
        std::set_intersection(installed_files.begin(),
                              installed_files.end(),
                              package_files.begin(),
                              package_files.end(),
                              std::back_inserter(intersection),
                              InstalledFilePathCompare{});

        if (intersection.empty())
        {
            return false;
        }

        // Re-sort by package display name to group conflicts by package
        std::stable_sort(
            intersection.begin(), intersection.end(), [](const InstalledFile& lhs, const InstalledFile& rhs) {
                return lhs.package_display_name < rhs.package_display_name;
            });

        const auto triplet_install_path = installed.triplet_dir(spec.triplet());
        msg::println_error(msgConflictingFiles, msg::path = triplet_install_path.generic_u8string(), msg::spec = spec);

        auto i = intersection.begin();
        while (i != intersection.end())
        {
            const auto& conflicting_display_name = i->package_display_name;
            auto next = std::find_if(i + 1, intersection.end(), [&conflicting_display_name](const InstalledFile& val) {
                return conflicting_display_name != val.package_display_name;
            });
            std::vector<LocalizedString> this_conflict_list;
            this_conflict_list.reserve(next - i);
            for (; i != next; ++i)
            {
                this_conflict_list.emplace_back(LocalizedString::from_raw(std::move(i->file_path)));
            }

            msg::print(msg::format(msgInstalledBy, msg::path = conflicting_display_name)
                           .append_raw(':')
                           .append_floating_list(1, this_conflict_list)
                           .append_raw('\n'));
        }

        return true;
    }

    static InstallResult install_package(const VcpkgPaths& paths,
                                         const Path& package_dir,
                                         const BinaryControlFile& bcf,
                                         StatusParagraphs& status_db)
    {
        auto& fs = paths.get_filesystem();
        const auto& installed = paths.installed();
        const auto& bcf_core_paragraph = bcf.core_paragraph;
        const auto& bcf_spec = bcf_core_paragraph.spec;
        auto package_files = build_list_of_package_files(fs, package_dir);
        if (check_for_install_conflicts(fs, package_files, installed, status_db, bcf_spec))
        {
            return InstallResult::FILE_CONFLICTS;
        }

        StatusParagraph source_paragraph;
        source_paragraph.package = bcf_core_paragraph;
        source_paragraph.status = StatusLine{Want::INSTALL, InstallState::HALF_INSTALLED};

        write_update(fs, installed, source_paragraph);
        status_db.insert(std::make_unique<StatusParagraph>(source_paragraph));

        std::vector<StatusParagraph> features_spghs;
        for (auto&& feature : bcf.features)
        {
            StatusParagraph& feature_paragraph = features_spghs.emplace_back();
            feature_paragraph.package = feature;
            feature_paragraph.status = StatusLine{Want::INSTALL, InstallState::HALF_INSTALLED};

            write_update(fs, installed, feature_paragraph);
            status_db.insert(std::make_unique<StatusParagraph>(feature_paragraph));
        }

        install_files_and_write_listfile(fs,
                                         package_dir,
                                         package_files,
                                         installed.root(),
                                         bcf_spec.triplet().canonical_name(),
                                         installed.listfile_path(bcf_core_paragraph),
                                         SymlinkHydrate::CopySymlinks);

        source_paragraph.status.state = InstallState::INSTALLED;
        write_update(fs, installed, source_paragraph);
        status_db.insert(std::make_unique<StatusParagraph>(source_paragraph));

        for (auto&& feature_paragraph : features_spghs)
        {
            feature_paragraph.status.state = InstallState::INSTALLED;
            write_update(fs, installed, feature_paragraph);
            status_db.insert(std::make_unique<StatusParagraph>(feature_paragraph));
        }

        return InstallResult::SUCCESS;
    }

    void LicenseReport::print_license_report(const msg::MessageT<>& named_license_heading) const
    {
        if (any_unknown_licenses || !named_licenses.empty())
        {
            msg::println(msgPackageLicenseWarning);
            if (any_unknown_licenses)
            {
                msg::println(msgPackageLicenseUnknown);
            }

            if (!named_licenses.empty())
            {
                msg::println(named_license_heading);
                for (auto&& license : named_licenses)
                {
                    msg::print(LocalizedString::from_raw(license).append_raw('\n'));
                }
            }
        }
    }

    static ExtendedBuildResult perform_install_plan_action_2(const VcpkgCmdArguments& args,
                                                             const VcpkgPaths& paths,
                                                             Triplet host_triplet,
                                                             const BuildPackageOptions& build_options,
                                                             const InstallPlanAction& action,
                                                             StatusParagraphs& status_db,
                                                             BinaryCache& binary_cache,
                                                             const IBuildLogsRecorder& build_logs_recorder)
    {
        auto& fs = paths.get_filesystem();

        bool all_dependencies_satisfied;
        std::unique_ptr<BinaryControlFile> bcf;
        if (binary_cache.is_restored(action))
        {
            auto maybe_bcf = Paragraphs::try_load_cached_package(fs, action.package_dir, action.spec);
            bcf = std::make_unique<BinaryControlFile>(std::move(maybe_bcf).value_or_exit(VCPKG_LINE_INFO));
            all_dependencies_satisfied = true;
        }
        else if (build_options.build_missing == BuildMissing::No)
        {
            return ExtendedBuildResult{action.spec, BuildResult::CacheMissing};
        }
        else
        {
            msg::println(action.use_head_version == UseHeadVersion::Yes ? msgBuildingFromHead : msgBuildingPackage,
                         msg::spec = action.display_name());

            auto result =
                build_package(args, paths, host_triplet, build_options, action, build_logs_recorder, status_db);

            if (BuildResult::Downloaded == result.code)
            {
                msg::println(Color::success, msgDownloadedSources, msg::spec = action.display_name());
                return result;
            }

            all_dependencies_satisfied = result.unmet_dependencies.empty();
            if (result.code != BuildResult::Succeeded)
            {
                for (auto&& msg : action.dependency_diagnostics)
                {
                    msg.print_to(out_sink);
                }

                msg::println_error(create_error_message(result, action.spec));
                return result;
            }

            bcf = std::move(result.binary_control_file);
        }
        // Build or restore succeeded and `bcf` is populated with the control file.
        Checks::check_exit(VCPKG_LINE_INFO, bcf != nullptr);
        BuildResult code;
        if (all_dependencies_satisfied)
        {
            const auto install_result = install_package(paths, action.package_dir, *bcf, status_db);
            switch (install_result)
            {
                case InstallResult::SUCCESS: code = BuildResult::Succeeded; break;
                case InstallResult::FILE_CONFLICTS: code = BuildResult::FileConflicts; break;
                default: Checks::unreachable(VCPKG_LINE_INFO);
            }
            binary_cache.push_success(build_options.clean_packages, action);
        }
        else
        {
            Checks::check_exit(VCPKG_LINE_INFO, build_options.only_downloads == OnlyDownloads::Yes);
            code = BuildResult::Downloaded;
        }

        if (build_options.clean_downloads == CleanDownloads::Yes)
        {
            for (auto& p : fs.get_regular_files_non_recursive(paths.downloads, IgnoreErrors{}))
            {
                fs.remove(p, VCPKG_LINE_INFO);
            }
        }

        return {action.spec, code, std::move(bcf)};
    }

    static InstallSpecSummary perform_install_plan_action(const VcpkgCmdArguments& args,
                                                          const VcpkgPaths& paths,
                                                          Triplet host_triplet,
                                                          const BuildPackageOptions& build_options,
                                                          const InstallPlanAction& action,
                                                          StatusParagraphs& status_db,
                                                          BinaryCache& binary_cache,
                                                          const IBuildLogsRecorder& build_logs_recorder)
    {
        const ElapsedTimer install_timer;
        const auto start_time = std::chrono::system_clock::now();
        auto build_result = perform_install_plan_action_2(
            args, paths, host_triplet, build_options, action, status_db, binary_cache, build_logs_recorder);
        const auto timing = install_timer.elapsed();
        const auto& abi_info = action.abi_info.value_or_exit(VCPKG_LINE_INFO);
        return InstallSpecSummary{std::move(build_result),
                                  action.feature_list,
                                  action.version,
                                  action.request_type,
                                  timing,
                                  start_time,
                                  abi_info.package_abi,
                                  abi_info.compiler_info};
    }

    template<typename SummaryType>
    static void format_results_block(std::map<Triplet, BuildResultCounts>& summary_counts,
                                     std::string& to_print,
                                     const std::vector<SummaryType>& results)
    {
        for (const SummaryType& r : results)
        {
            summary_counts[r.build_result.spec.triplet()].increment(r.build_result.code);

            to_print.append(2, ' ');
            r.to_string(to_print);
            to_print.push_back('\n');
        }
    }

    LocalizedString InstallSummary::format_results() const
    {
        std::map<Triplet, BuildResultCounts> summary_counts;
        std::string to_print = msg::format(msgResultsHeader).extract_data();
        to_print.push_back('\n');
        format_results_block(summary_counts, to_print, removed_results);
        format_results_block(summary_counts, to_print, already_installed_results);
        format_results_block(summary_counts, to_print, install_results);
        to_print.push_back('\n');
        for (auto&& entry : summary_counts)
        {
            to_print.append(entry.second.format(entry.first).data());
        }

        return LocalizedString::from_raw(std::move(to_print));
    }

    template<typename SummaryType>
    static void append_failures_block(std::string& output, const std::vector<SummaryType>& results)
    {
        for (const SummaryType& result : results)
        {
            if (result.build_result.code != BuildResult::Succeeded)
            {
                output.append(2, ' ');
                result.to_string(output);
                output.push_back('\n');
            }
        }
    }

    void InstallSummary::print_failed() const
    {
        std::string output;
        output.push_back('\n');
        output.append(msg::format(msgResultsHeader).extract_data());
        output.push_back('\n');
        append_failures_block(output, removed_results);
        append_failures_block(output, already_installed_results);
        append_failures_block(output, install_results);
        output.push_back('\n');
        msg::print(LocalizedString::from_raw(std::move(output)));
    }

    void InstallSummary::print_complete_message() const
    {
        if (failed)
        {
            msg::println(msgTotalInstallTime, msg::elapsed = elapsed);
        }
        else
        {
            msg::println(Color::success, msgTotalInstallTimeSuccess, msg::elapsed = elapsed);
        }
    }

    void install_preclear_plan_packages(const VcpkgPaths& paths, const ActionPlan& action_plan)
    {
        purge_packages_dirs(paths, action_plan.remove_actions);
        install_clear_installed_packages(paths, action_plan.install_actions);
    }

    void install_clear_installed_packages(const VcpkgPaths& paths, View<InstallPlanAction> install_actions)
    {
        auto& fs = paths.get_filesystem();
        for (auto&& action : install_actions)
        {
            fs.remove_all(action.package_dir, VCPKG_LINE_INFO);
        }
    }

    InstallSummary install_execute_plan(const VcpkgCmdArguments& args,
                                        const VcpkgPaths& paths,
                                        Triplet host_triplet,
                                        const BuildPackageOptions& build_options,
                                        const ActionPlan& action_plan,
                                        StatusParagraphs& status_db,
                                        BinaryCache& binary_cache,
                                        const IBuildLogsRecorder& build_logs_recorder,
                                        bool include_manifest_in_github_issue)
    {
        ElapsedTimer timer;
        InstallSummary summary;
        const size_t action_count = action_plan.remove_actions.size() + action_plan.install_actions.size();
        size_t action_index = 1;

        auto& fs = paths.get_filesystem();
        const auto& installed = paths.installed();

        // Create temporary directory for storing old files
        const auto temp_base = fs.create_or_get_temp_directory(VCPKG_LINE_INFO) / "vcpkg-incremental-install";
        // Clear any existing temp directory from previous runs
        fs.remove_all(temp_base, IgnoreErrors{});
        fs.create_directories(temp_base, VCPKG_LINE_INFO);

        // For each package to remove, copy its files to temp directory preserving timestamps
        // (files stay in installed/ so remove_package can clean them up normally)
        std::unordered_map<PackageSpec, Path> temp_package_dirs;
        for (const auto& action : action_plan.remove_actions)
        {
            auto maybe_ipv = status_db.get_installed_package_view(action.spec);
            if (auto ipv = maybe_ipv.get())
            {
                auto maybe_lines = fs.read_lines(installed.listfile_path(ipv->core->package));
                if (auto lines = maybe_lines.get())
                {
                    // Create subfolder named like the listfile: <port>_<version>_<triplet>
                    const auto& spec = action.spec;
                    const auto temp_pkg_dir = temp_base / ipv->core->package.fullstem();
                    fs.create_directories(temp_pkg_dir, VCPKG_LINE_INFO);
                    temp_package_dirs[spec] = temp_pkg_dir;

                    Debug::println("Copying old files for ", spec, " to temp: ", temp_pkg_dir);

                    // Copy each file to temp, preserving timestamps
                    for (const auto& suffix : *lines)
                    {
                        if (suffix.empty() || suffix.back() == '/') continue; // Skip directories

                        const auto source = installed.root() / suffix;
                        const auto dest = temp_pkg_dir / suffix;

                        if (fs.copy_file_preserving_timestamp(source, dest))
                        {
                            Debug::println("  Copied: ", suffix);
                        }
                    }
                }
            }
        }

        // Process removals
        for (auto&& action : action_plan.remove_actions)
        {
            msg::println(msgRemovingPackage,
                         msg::action_index = action_index,
                         msg::count = action_count,
                         msg::spec = action.spec);
            ++action_index;
            const auto& remove_summary =
                summary.removed_results.emplace_back(remove_package(fs, installed, action.spec, status_db));
            msg::println(msgElapsedForPackage,
                         msg::spec = remove_summary.build_result.spec,
                         msg::elapsed = remove_summary.timing);
        }

        for (auto&& action : action_plan.already_installed)
        {
            summary.already_installed_results.emplace_back(ExtendedBuildResult{action.spec, BuildResult::Succeeded},
                                                           action.feature_list,
                                                           action.version,
                                                           action.request_type,
                                                           ElapsedTime{},
                                                           std::chrono::system_clock::now(),
                                                           action.package_abi(),
                                                           nullptr);
        }

        // Install packages and restore timestamps for unchanged files
        for (auto&& action : action_plan.install_actions)
        {
            binary_cache.print_updates();
            const auto action_display_name = action.display_name();
            msg::println(msgInstallingPackage,
                         msg::action_index = action_index,
                         msg::count = action_count,
                         msg::spec = action_display_name);
            ++action_index;
            if (auto package_abi = action.package_abi())
            {
                msg::println(msgPackageAbi, msg::spec = action_display_name, msg::package_abi = *package_abi);
            }

            auto& result = summary.install_results.emplace_back(perform_install_plan_action(
                args, paths, host_triplet, build_options, action, status_db, binary_cache, build_logs_recorder));
            if (result.build_result.code == BuildResult::Succeeded)
            {
                // For reinstalled packages, restore timestamps for unchanged files
                auto temp_it = temp_package_dirs.find(action.spec);
                if (temp_it != temp_package_dirs.end())
                {
                    const auto& temp_pkg_dir = temp_it->second;
                    if (auto bcf = result.build_result.binary_control_file.get())
                    {
                        auto maybe_lines = fs.read_lines(installed.listfile_path(bcf->core_paragraph));
                        if (auto lines = maybe_lines.get())
                        {
                            Debug::println("Checking for unchanged files in ", action.spec);
                            for (const auto& suffix : *lines)
                            {
                                if (suffix.empty() || suffix.back() == '/') continue; // Skip directories

                                const auto new_file = installed.root() / suffix;
                                const auto old_file = temp_pkg_dir / suffix;

                                restore_timestamp_if_unchanged(fs, new_file, old_file);
                            }
                        }
                    }
                }

                const auto& scfl = action.source_control_file_and_location();
                const auto& scf = *scfl.source_control_file;
                auto& license = scf.core_paragraph->license;
                switch (license.kind())
                {
                    case SpdxLicenseDeclarationKind::NotPresent:
                    case SpdxLicenseDeclarationKind::Null: summary.license_report.any_unknown_licenses = true; break;
                    case SpdxLicenseDeclarationKind::String:
                        for (auto&& applicable_license : license.applicable_licenses())
                        {
                            summary.license_report.named_licenses.insert(applicable_license.to_string());
                        }
                        break;
                    default: Checks::unreachable(VCPKG_LINE_INFO);
                }

                for (const auto& feature_name : action.feature_list)
                {
                    if (feature_name == FeatureNameCore)
                    {
                        continue;
                    }

                    const auto* feature = scf.find_feature(feature_name);
                    Checks::check_exit(VCPKG_LINE_INFO, feature != nullptr);
                    for (auto&& applicable_license : feature->license.applicable_licenses())
                    {
                        summary.license_report.named_licenses.insert(applicable_license.to_string());
                    }
                }
            }
            else if (build_options.keep_going == KeepGoing::No)
            {
                msg::println(msgElapsedForPackage, msg::spec = action.spec, msg::elapsed = result.timing);
                print_user_troubleshooting_message(
                    action,
                    args.detected_ci(),
                    paths,
                    result.build_result.error_logs,
                    result.build_result.stdoutlog.then([&](auto&) -> Optional<Path> {
                        auto issue_body_path = paths.installed().root() / FileVcpkg / FileIssueBodyMD;
                        paths.get_filesystem().write_contents(
                            issue_body_path,
                            create_github_issue(args, paths, result, include_manifest_in_github_issue),
                            VCPKG_LINE_INFO);
                        return issue_body_path;
                    }));
                binary_cache.wait_for_async_complete_and_join();
                Checks::exit_fail(VCPKG_LINE_INFO);
            }

            switch (result.build_result.code)
            {
                case BuildResult::Succeeded:
                case BuildResult::Removed:
                case BuildResult::Downloaded:
                case BuildResult::Excluded:
                case BuildResult::ExcludedByParent:
                case BuildResult::ExcludedByDryRun:
                case BuildResult::Cached: break;
                case BuildResult::BuildFailed:
                case BuildResult::PostBuildChecksFailed:
                case BuildResult::FileConflicts:
                case BuildResult::CascadedDueToMissingDependencies:
                case BuildResult::Unsupported:
                case BuildResult::CacheMissing: summary.failed = true; break;
                default: Checks::unreachable(VCPKG_LINE_INFO);
            }

            msg::println(msgElapsedForPackage, msg::spec = action.spec, msg::elapsed = result.timing);
        }

        // Clean up temporary directory
        Debug::println("Cleaning up temporary directory: ", temp_base);
        fs.remove_all(temp_base, VCPKG_LINE_INFO);

        database_load_collapse(fs, paths.installed());
        summary.elapsed = timer.elapsed();
        return summary;
    }

    static constexpr CommandSwitch INSTALL_SWITCHES[] = {
        {SwitchDryRun, msgHelpTxtOptDryRun},
        {SwitchHead, msgHelpTxtOptUseHeadVersion},
        {SwitchNoDownloads, msgHelpTxtOptNoDownloads},
        {SwitchOnlyDownloads, msgHelpTxtOptOnlyDownloads},
        {SwitchOnlyBinarycaching, msgHelpTxtOptOnlyBinCache},
        {SwitchRecurse, msgHelpTxtOptRecurse},
        {SwitchKeepGoing, msgHelpTxtOptKeepGoing},
        {SwitchEditable, msgHelpTxtOptEditable},
        {SwitchCleanAfterBuild, msgHelpTxtOptCleanAfterBuild},
        {SwitchCleanBuildtreesAfterBuild, msgHelpTxtOptCleanBuildTreesAfterBuild},
        {SwitchCleanPackagesAfterBuild, msgHelpTxtOptCleanPkgAfterBuild},
        {SwitchCleanDownloadsAfterBuild, msgHelpTxtOptCleanDownloadsAfterBuild},
        {SwitchXNoDefaultFeatures, msgHelpTxtOptManifestNoDefault},
        {SwitchEnforcePortChecks, msgHelpTxtOptEnforcePortChecks},
        {SwitchXProhibitBackcompatFeatures, {}},
        {SwitchAllowUnsupported, msgHelpTxtOptAllowUnsupportedPort},
        {SwitchNoPrintUsage, msgHelpTxtOptNoUsage},
    };

    static constexpr CommandSetting INSTALL_SETTINGS[] = {
        {SwitchXXUnit, {}}, // internal use
        {SwitchXWriteNuGetPackagesConfig, msgHelpTxtOptWritePkgConfig},
    };

    static constexpr CommandMultiSetting INSTALL_MULTISETTINGS[] = {
        {SwitchXFeature, msgHelpTxtOptManifestFeature},
    };

    static std::vector<std::string> get_all_known_reachable_port_names_no_network(const VcpkgPaths& paths)
    {
        return paths.make_registry_set()->get_all_known_reachable_port_names_no_network().value_or_exit(
            VCPKG_LINE_INFO);
    }

    constexpr CommandMetadata CommandInstallMetadata{
        "install",
        msgHelpInstallCommand,
        {msgCmdInstallExample1,
         "vcpkg install zlib zlib:x64-windows curl boost",
         "vcpkg install --triplet x64-windows"},
        "https://learn.microsoft.com/vcpkg/commands/install",
        AutocompletePriority::Public,
        0,
        SIZE_MAX,
        {INSTALL_SWITCHES, INSTALL_SETTINGS, INSTALL_MULTISETTINGS},
        &get_all_known_reachable_port_names_no_network,
    };

    // These command metadata must share "critical" values (switches, number of arguments). They exist only to provide
    // better example strings.
    constexpr CommandMetadata CommandInstallMetadataClassic{
        "install",
        msgHelpInstallCommand,
        {msgCmdInstallExample1, "vcpkg install zlib zlib:x64-windows curl boost"},
        "https://learn.microsoft.com/vcpkg/commands/install",
        AutocompletePriority::Public,
        0,
        SIZE_MAX,
        {INSTALL_SWITCHES, INSTALL_SETTINGS, INSTALL_MULTISETTINGS},
        &get_all_known_reachable_port_names_no_network,
    };

    constexpr CommandMetadata CommandInstallMetadataManifest{
        "install",
        msgHelpInstallCommand,
        {msgCmdInstallExample1,
         "vcpkg install zlib zlib:x64-windows curl boost",
         "vcpkg install --triplet x64-windows"},
        "https://learn.microsoft.com/vcpkg/commands/install",
        AutocompletePriority::Public,
        0,
        SIZE_MAX,
        {INSTALL_SWITCHES, INSTALL_SETTINGS, INSTALL_MULTISETTINGS},
        nullptr,
    };

    void install_print_usage_information(const BinaryParagraph& bpgh,
                                         std::set<std::string>& printed_usages,
                                         const ReadOnlyFilesystem& fs,
                                         const InstalledPaths& installed)
    {
        auto message = get_cmake_usage(fs, installed, bpgh).message;
        if (!message.empty())
        {
            auto existing = printed_usages.lower_bound(message);
            if (existing == printed_usages.end() || *existing != message)
            {
                msg::write_unlocalized_text(Color::none, message);
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
        constexpr static auto is_terminating_char = [](const char ch) {
            return ch == ')' || ParserBase::is_whitespace(ch);
        };

        constexpr static auto is_forbidden_char = [](const char ch) {
            return ch == '$' || ch == '"' || ch == '[' || ch == '#' || ch == ';' || ch == '<';
        };

        const auto real_first = cmake_file.begin();
        auto first = real_first;
        const auto last = cmake_file.end();

        std::vector<std::string> res;
        while (first != last)
        {
            const auto start_of_library_name = find_skip_add_library(real_first, first, last);
            const auto end_of_library_name = std::find_if(start_of_library_name, last, is_terminating_char);
            if (end_of_library_name != start_of_library_name &&
                std::none_of(start_of_library_name, end_of_library_name, is_forbidden_char))
            {
                res.emplace_back(start_of_library_name, end_of_library_name);
            }

            first = end_of_library_name;
        }
        return res;
    }

    std::string get_cmake_find_package_name(StringView dirname, StringView filename)
    {
        static constexpr StringLiteral CASE_SENSITIVE_CONFIG_SUFFIX = "Config.cmake";
        static constexpr StringLiteral CASE_INSENSITIVE_CONFIG_SUFFIX = "-config.cmake";

        StringView res;
        if (filename.ends_with(CASE_SENSITIVE_CONFIG_SUFFIX))
        {
            res = filename.substr(0, filename.size() - CASE_SENSITIVE_CONFIG_SUFFIX.size());
        }
        else if (filename.ends_with(CASE_INSENSITIVE_CONFIG_SUFFIX))
        {
            res = filename.substr(0, filename.size() - CASE_INSENSITIVE_CONFIG_SUFFIX.size());
        }

        if (!Strings::case_insensitive_ascii_equals(res, dirname.substr(0, res.size())))
        {
            res = {};
        }

        return std::string(res);
    }

    CMakeUsageInfo get_cmake_usage(const ReadOnlyFilesystem& fs,
                                   const InstalledPaths& installed,
                                   const BinaryParagraph& bpgh)
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

        struct ConfigPackage
        {
            std::string dir;
            std::string name;
        };

        auto maybe_files = fs.read_lines(installed.listfile_path(bpgh));
        if (auto files = maybe_files.get())
        {
            std::vector<ConfigPackage> config_packages;
            std::vector<Path> pkgconfig_files;
            std::map<std::string, std::vector<std::string>> library_targets;
            std::string header_path;
            bool has_binaries = false;

            static constexpr StringLiteral DOT_CMAKE = ".cmake";
            static constexpr StringLiteral INCLUDE_PREFIX = "include/";

            for (auto&& triplet_and_suffix : *files)
            {
                if (triplet_and_suffix.empty() || triplet_and_suffix.back() == '/') continue;

                const auto first_slash = triplet_and_suffix.find("/");
                if (first_slash == std::string::npos) continue;

                const auto suffix = StringView(triplet_and_suffix).substr(first_slash + 1);
                if (suffix.empty() || suffix[0] == 'd' /*ebug*/)
                {
                    continue;
                }
                else if (suffix.starts_with("share/") && suffix.ends_with(DOT_CMAKE))
                {
                    const auto suffix_without_ending = suffix.substr(0, DOT_CMAKE.size());
                    if (suffix_without_ending.ends_with("/vcpkg-port-config")) continue;
                    if (suffix_without_ending.ends_with("/vcpkg-cmake-wrapper")) continue;
                    if (suffix_without_ending.ends_with(/*[Vv]*/ "ersion")) continue;

                    const auto filepath = installed.root() / triplet_and_suffix;
                    const auto parent_path = Path(filepath.parent_path());
                    if (!parent_path.parent_path().ends_with("/share"))
                        continue; // Ignore nested find modules, config, or helpers

                    if (suffix_without_ending.contains("/Find")) continue;

                    const auto dirname = parent_path.filename().to_string();
                    const auto package_name = get_cmake_find_package_name(dirname, filepath.filename());
                    if (!package_name.empty())
                    {
                        // This heuristics works for one package name per dir.
                        if (!config_packages.empty() && config_packages.back().dir == dirname)
                            config_packages.back().name.clear();
                        else
                            config_packages.push_back({dirname, package_name});
                    }

                    const auto contents = fs.read_contents(filepath, ec);
                    if (!ec)
                    {
                        auto targets = get_cmake_add_library_names(contents);
                        if (!targets.empty())
                        {
                            auto& all_targets = library_targets[dirname];
                            all_targets.insert(all_targets.end(),
                                               std::make_move_iterator(targets.begin()),
                                               std::make_move_iterator(targets.end()));
                        }
                    }
                }
                else if (!has_binaries && suffix.starts_with("bin/"))
                {
                    has_binaries = true;
                }
                else if (suffix.ends_with(".pc"))
                {
                    if (suffix.contains("pkgconfig"))
                    {
                        pkgconfig_files.push_back(installed.root() / triplet_and_suffix);
                    }
                }
                else if (suffix.starts_with("lib/"))
                {
                    has_binaries = true;
                }
                else if (header_path.empty() && suffix.starts_with(INCLUDE_PREFIX))
                {
                    header_path = suffix.substr(INCLUDE_PREFIX.size()).to_string();
                }
            }

            ret.header_only = !has_binaries && !header_path.empty();

            // Post-process cmake config data
            bool has_targets_for_output = false;
            for (auto&& package : config_packages)
            {
                const auto library_target_it = library_targets.find(package.dir);
                if (library_target_it == library_targets.end()) continue;

                auto& targets = library_target_it->second;
                if (!targets.empty())
                {
                    if (!package.name.empty()) has_targets_for_output = true;

                    Util::sort_unique_erase(targets, [](const std::string& l, const std::string& r) {
                        if (l.size() < r.size()) return true;
                        if (l.size() > r.size()) return false;
                        return l < r;
                    });

                    static const auto is_namespaced = [](StringView target) { return target.contains("::"); };
                    if (Util::any_of(targets, is_namespaced))
                    {
                        Util::erase_remove_if(targets, [](StringView t) { return !is_namespaced(t); });
                    }
                }
                ret.cmake_targets_map[package.name] = std::move(targets);
            }

            if (has_targets_for_output)
            {
                auto msg = msg::format(msgCMakeTargetsUsage, msg::package_name = bpgh.spec.name()).append_raw("\n\n");
                msg.append_indent().append(msgCMakeTargetsUsageHeuristicMessage).append_raw('\n');

                for (auto&& package_targets_pair : ret.cmake_targets_map)
                {
                    const auto& package_name = package_targets_pair.first;
                    if (package_name.empty()) continue;

                    const auto& targets = package_targets_pair.second;
                    if (targets.empty()) continue;

                    msg.append_indent();
                    msg.append_raw("find_package(").append_raw(package_name).append_raw(" CONFIG REQUIRED)\n");

                    const auto omitted = (targets.size() > 4) ? (targets.size() - 4) : 0;
                    if (omitted)
                    {
                        msg.append_indent()
                            .append_raw("# ")
                            .append_raw(NotePrefix)
                            .append(msgCmakeTargetsExcluded, msg::count = omitted)
                            .append_raw('\n');
                    }

                    msg.append_indent();
                    msg.append_raw("target_link_libraries(main PRIVATE ")
                        .append_raw(Strings::join(" ", targets.begin(), targets.end() - omitted))
                        .append_raw(")\n\n");
                }

                ret.message = msg.extract_data();
            }
            else if (ret.header_only)
            {
                static auto cmakeify = [](StringView name) {
                    auto n = Strings::ascii_to_uppercase(name);
                    Strings::inplace_replace_all(n, "-", "_");
                    if (n.empty() || ParserBase::is_ascii_digit(n[0]))
                    {
                        n.insert(n.begin(), '_');
                    }
                    return n;
                };

                const auto name = cmakeify(bpgh.spec.name());
                auto msg = msg::format(msgHeaderOnlyUsage, msg::package_name = bpgh.spec.name()).append_raw("\n\n");
                msg.append_indent()
                    .append_raw("find_path(")
                    .append_raw(name)
                    .append_raw("_INCLUDE_DIRS \"")
                    .append_raw(header_path)
                    .append_raw("\")\n");
                msg.append_indent()
                    .append_raw("target_include_directories(main PRIVATE ${")
                    .append_raw(name)
                    .append_raw("_INCLUDE_DIRS})\n\n");

                ret.message = msg.extract_data();
            }
            if (!pkgconfig_files.empty())
            {
                auto msg =
                    msg::format(msgCMakePkgConfigTargetsUsage, msg::package_name = bpgh.spec.name()).append_raw("\n\n");
                for (auto&& path : pkgconfig_files)
                {
                    const auto lines = fs.read_lines(path).value_or_exit(VCPKG_LINE_INFO);
                    for (const auto& line : lines)
                    {
                        if (Strings::starts_with(line, "Description: "))
                        {
                            msg.append_indent()
                                .append_raw("# ")
                                .append_raw(line.substr(StringLiteral("Description: ").size()))
                                .append_raw('\n');
                            break;
                        }
                    }
                    msg.append_indent().append_raw(path.stem()).append_raw("\n\n");
                }
                ret.message += msg.extract_data();
            }
        }
        return ret;
    }

    static bool cmake_args_sets_variable(const VcpkgCmdArguments& args)
    {
        return Util::any_of(args.cmake_args, [](StringView s) { return s.starts_with("-D"); });
    }

#if defined(_WIN32)
    static void maybe_print_vs_prompt_warning(const std::vector<InstallPlanAction>& install_actions)
    {
        if (!install_actions.empty())
        {
            const auto first = install_actions.begin();
            auto next = first;
            const auto last = install_actions.end();
            while (++next != last)
            {
                if (first->spec.triplet() != next->spec.triplet())
                {
                    return;
                }
            }

            const auto maybe_common_arch = first->spec.triplet().guess_architecture();
            if (auto common_arch = maybe_common_arch.get())
            {
                const auto maybe_vs_prompt = guess_visual_studio_prompt_target_architecture();
                if (auto vs_prompt = maybe_vs_prompt.get())
                {
                    // There is no "Developer Command Prompt for ARM64EC". ARM64EC and ARM64 use the same developer
                    // command prompt, and compiler toolset version. The only difference is adding a /arm64ec switch to
                    // the build
                    if (*common_arch != *vs_prompt &&
                        !(*common_arch == CPUArchitecture::ARM64EC && *vs_prompt == CPUArchitecture::ARM64))
                    {
                        msg::println_warning(
                            msgVcpkgInVsPrompt, msg::value = *vs_prompt, msg::triplet = first->spec.triplet());
                    }
                }
            }
        }
    }
#endif // ^^^ _WIN32

    void command_install_and_exit(const VcpkgCmdArguments& args,
                                  const VcpkgPaths& paths,
                                  Triplet default_triplet,
                                  Triplet host_triplet)
    {
        const auto* manifest = paths.get_manifest();
        const ParsedArguments options =
            args.parse_arguments(manifest ? CommandInstallMetadataManifest : CommandInstallMetadataClassic);

        const bool dry_run = Util::Sets::contains(options.switches, SwitchDryRun);
        const bool use_head_version = Util::Sets::contains(options.switches, (SwitchHead));
        const bool no_downloads = Util::Sets::contains(options.switches, (SwitchNoDownloads));
        const bool only_downloads = Util::Sets::contains(options.switches, (SwitchOnlyDownloads));
        const bool no_build_missing = Util::Sets::contains(options.switches, SwitchOnlyBinarycaching);
        const bool is_recursive = Util::Sets::contains(options.switches, (SwitchRecurse));
        const bool is_editable =
            Util::Sets::contains(options.switches, (SwitchEditable)) || cmake_args_sets_variable(args);
        const bool clean_after_build = Util::Sets::contains(options.switches, (SwitchCleanAfterBuild));
        const bool clean_buildtrees_after_build =
            Util::Sets::contains(options.switches, (SwitchCleanBuildtreesAfterBuild));
        const bool clean_packages_after_build = Util::Sets::contains(options.switches, (SwitchCleanPackagesAfterBuild));
        const bool clean_downloads_after_build =
            Util::Sets::contains(options.switches, (SwitchCleanDownloadsAfterBuild));
        const KeepGoing keep_going =
            Util::Sets::contains(options.switches, SwitchKeepGoing) || only_downloads ? KeepGoing::Yes : KeepGoing::No;
        const bool prohibit_backcompat_features =
            Util::Sets::contains(options.switches, (SwitchXProhibitBackcompatFeatures)) ||
            Util::Sets::contains(options.switches, (SwitchEnforcePortChecks));
        const auto unsupported_port_action = Util::Sets::contains(options.switches, SwitchAllowUnsupported)
                                                 ? UnsupportedPortAction::Warn
                                                 : UnsupportedPortAction::Error;
        const bool print_cmake_usage = !Util::Sets::contains(options.switches, SwitchNoPrintUsage);

        get_global_metrics_collector().track_bool(BoolMetric::InstallManifestMode, manifest);

        if (manifest)
        {
            bool failure = false;
            if (!options.command_arguments.empty())
            {
                msg::println_error(msgErrorIndividualPackagesUnsupported);
                msg::println(Color::error, msgSeeURL, msg::url = docs::manifests_url);
                failure = true;
            }
            if (use_head_version)
            {
                msg::println_error(msgErrorInvalidManifestModeOption, msg::option = SwitchHead);
                failure = true;
            }
            if (is_editable)
            {
                msg::println_error(msgErrorInvalidManifestModeOption, msg::option = SwitchEditable);
                failure = true;
            }
            if (failure)
            {
                msg::println(msgUsingManifestAt, msg::path = manifest->path);
                msg::print(usage_for_command(CommandInstallMetadataManifest));
                Checks::exit_fail(VCPKG_LINE_INFO);
            }
        }
        else
        {
            bool failure = false;
            if (options.command_arguments.empty())
            {
                msg::println_error(msgErrorRequirePackagesList);
                failure = true;
            }
            if (Util::Sets::contains(options.switches, SwitchXNoDefaultFeatures))
            {
                msg::println_error(msgErrorInvalidClassicModeOption, msg::option = SwitchXNoDefaultFeatures);
                failure = true;
            }
            if (Util::Sets::contains(options.multisettings, SwitchXFeature))
            {
                msg::println_error(msgErrorInvalidClassicModeOption, msg::option = SwitchXFeature);
                failure = true;
            }
            if (failure)
            {
                msg::write_unlocalized_text_to_stderr(Color::none, usage_for_command(CommandInstallMetadataClassic));
                Checks::exit_fail(VCPKG_LINE_INFO);
            }
        }

        auto& fs = paths.get_filesystem();

        const BuildPackageOptions build_package_options = {
            Util::Enum::to_enum<BuildMissing>(!no_build_missing),
            Util::Enum::to_enum<AllowDownloads>(!no_downloads),
            Util::Enum::to_enum<OnlyDownloads>(only_downloads),
            Util::Enum::to_enum<CleanBuildtrees>(clean_after_build || clean_buildtrees_after_build),
            Util::Enum::to_enum<CleanPackages>(clean_after_build || clean_packages_after_build),
            Util::Enum::to_enum<CleanDownloads>(clean_after_build || clean_downloads_after_build),
            prohibit_backcompat_features ? BackcompatFeatures::Prohibit : BackcompatFeatures::Allow,
            keep_going,
        };

        PackagesDirAssigner packages_dir_assigner{paths.packages()};
        const CreateInstallPlanOptions create_options{nullptr,
                                                      host_triplet,
                                                      unsupported_port_action,
                                                      Util::Enum::to_enum<UseHeadVersion>(use_head_version),
                                                      Util::Enum::to_enum<Editable>(is_editable)};

        auto var_provider_storage = CMakeVars::make_triplet_cmake_var_provider(paths);
        auto& var_provider = *var_provider_storage;

        if (manifest)
        {
            Optional<Path> pkgsconfig;
            auto it_pkgsconfig = options.settings.find(SwitchXWriteNuGetPackagesConfig);
            if (it_pkgsconfig != options.settings.end())
            {
                get_global_metrics_collector().track_define(DefineMetric::X_WriteNuGetPackagesConfig);
                pkgsconfig = Path(it_pkgsconfig->second);
            }
            auto maybe_manifest_scf =
                SourceControlFile::parse_project_manifest_object(manifest->path, manifest->manifest, out_sink);
            if (!maybe_manifest_scf)
            {
                msg::println(Color::error,
                             std::move(maybe_manifest_scf)
                                 .error()
                                 .append_raw('\n')
                                 .append_raw(NotePrefix)
                                 .append(msgExtendedDocumentationAtUrl, msg::url = docs::manifests_url)
                                 .append_raw('\n'));
                Checks::exit_fail(VCPKG_LINE_INFO);
            }

            auto manifest_scf = std::move(maybe_manifest_scf).value(VCPKG_LINE_INFO);
            const auto& manifest_core = *manifest_scf->core_paragraph;
            auto registry_set = paths.make_registry_set();
            manifest_scf
                ->check_against_feature_flags(
                    manifest->path, paths.get_feature_flags(), registry_set->is_default_builtin_registry())
                .value_or_exit(VCPKG_LINE_INFO);

            std::vector<std::string> features;
            auto manifest_feature_it = options.multisettings.find(SwitchXFeature);
            if (manifest_feature_it != options.multisettings.end())
            {
                features.insert(features.end(), manifest_feature_it->second.begin(), manifest_feature_it->second.end());
            }
            if (Util::Sets::contains(options.switches, SwitchXNoDefaultFeatures))
            {
                features.emplace_back(FeatureNameCore);
            }
            PackageSpec toplevel{manifest_core.name, default_triplet};
            auto core_it = std::remove(features.begin(), features.end(), FeatureNameCore);
            if (core_it == features.end())
            {
                if (Util::any_of(manifest_core.default_features, [](const auto& f) { return !f.platform.is_empty(); }))
                {
                    const auto& vars = var_provider.get_or_load_dep_info_vars(toplevel, host_triplet);
                    for (const auto& f : manifest_core.default_features)
                    {
                        if (f.platform.evaluate(vars)) features.push_back(f.name);
                    }
                }
                else
                {
                    for (const auto& f : manifest_core.default_features)
                        features.push_back(f.name);
                }
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
                    msg::println_warning(
                        msgUnsupportedFeature, msg::feature = feature, msg::package_name = manifest_core.name);
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
                get_global_metrics_collector().track_define(DefineMetric::ManifestVersionConstraint);
            }

            if (!manifest_core.overrides.empty())
            {
                get_global_metrics_collector().track_define(DefineMetric::ManifestOverrides);
            }

            const bool add_builtin_ports_directory_as_overlay =
                registry_set->is_default_builtin_registry() && !paths.use_git_default_registry();
            auto verprovider = make_versioned_portfile_provider(*registry_set);
            auto baseprovider = make_baseline_provider(*registry_set);

            auto extended_overlay_port_directories = paths.overlay_ports;
            if (add_builtin_ports_directory_as_overlay)
            {
                extended_overlay_port_directories.builtin_overlay_port_dir.emplace(paths.builtin_ports_directory());
            }

            auto oprovider =
                make_manifest_provider(fs, extended_overlay_port_directories, manifest->path, std::move(manifest_scf));
            auto install_plan = create_versioned_install_plan(*verprovider,
                                                              *baseprovider,
                                                              *oprovider,
                                                              var_provider,
                                                              dependencies,
                                                              manifest_core.overrides,
                                                              toplevel,
                                                              packages_dir_assigner,
                                                              create_options)
                                    .value_or_exit(VCPKG_LINE_INFO);

            install_plan.print_unsupported_warnings();

            // If the manifest refers to itself, it will be added to the install plan.
            Util::erase_remove_if(install_plan.install_actions,
                                  [&toplevel](auto&& action) { return action.spec == toplevel; });

            // Check configuration for editable ports
            const auto& config = paths.get_configuration();
            const auto& editable_config = config.config.editable_ports;
            const auto config_dir = config.directory;

            // Print warning if editable mode is active
            if (editable_config.has_value() && !editable_config.get()->ports.empty())
            {
                msg::println(Color::warning,
                             LocalizedString::from_raw("\n"
                                                       "=============== EDITABLE MODE ENABLED ===============\n"
                                                       "Editable ports are experimental and may cause:\n"
                                                       "  - Inconsistent builds between machines\n"
                                                       "  - Binary caching disabled for editable ports\n"
                                                       "  - Sources cloned to editable-ports/<port>/sources/\n"
                                                       "Use for development only, not production builds.\n"
                                                       "======================================================\n"));
            }

            for (InstallPlanAction& action : install_plan.install_actions)
            {
                const auto& port_name = action.spec.name();
                const bool port_is_editable =
                    editable_config.has_value() && editable_config.get()->is_port_editable(port_name);

                if (port_is_editable)
                {
                    action.editable = Editable::Yes;

                    msg::println(Color::success, LocalizedString::from_raw("Editable port: " + port_name));

                    const auto editable_ports_path = editable_config.get()->get_editable_ports_path(config_dir);
                    const auto editable_port_path = editable_ports_path / port_name;
                    action.editable_sources_path = editable_port_path / "sources";
                    action.editable_build_dir = editable_port_path / "build";
                    // Override package_dir to use editable location
                    action.package_dir = editable_port_path / "packages";

                    // Initialize if port directory doesn't exist yet
                    if (!fs.exists(editable_port_path, IgnoreErrors{}))
                    {
                        initialize_editable_port(fs, action.source_control_file_and_location(), editable_port_path);
                    }
                    else
                    {
                        msg::println(LocalizedString::from_raw("  Using existing editable port at: " +
                                                               editable_port_path.native()));
                    }
                }
            }

            // Compute editable subtree: mark ports that are editable or have editable dependencies
            // The install plan is topologically sorted (dependencies first), so we can propagate forward
            std::set<std::string> editable_subtree_ports;
            for (InstallPlanAction& action : install_plan.install_actions)
            {
                bool in_subtree = (action.editable == Editable::Yes);

                // Check if any dependency is in the editable subtree
                if (!in_subtree)
                {
                    for (const auto& dep_spec : action.package_dependencies)
                    {
                        if (editable_subtree_ports.count(dep_spec.name()))
                        {
                            in_subtree = true;
                            break;
                        }
                    }
                }

                if (in_subtree)
                {
                    editable_subtree_ports.insert(action.spec.name());
                    action.editable_subtree = EditableSubtree::Yes;
                }
            }

            command_set_installed_and_exit_ex(args,
                                              paths,
                                              host_triplet,
                                              build_package_options,
                                              var_provider,
                                              std::move(install_plan),
                                              dry_run ? DryRun::Yes : DryRun::No,
                                              print_cmake_usage ? PrintUsage::Yes : PrintUsage::No,
                                              pkgsconfig,
                                              true);
        }

        auto registry_set = paths.make_registry_set();
        PathsPortFileProvider provider(*registry_set, make_overlay_provider(fs, paths.overlay_ports));

        const std::vector<FullPackageSpec> specs = Util::fmap(options.command_arguments, [&](const std::string& arg) {
            return check_and_get_full_package_spec(arg, default_triplet, paths.get_triplet_db())
                .value_or_exit(VCPKG_LINE_INFO);
        });

        // create the plan
        msg::println(msgComputingInstallPlan);
        StatusParagraphs status_db = database_load_collapse(fs, paths.installed());

        // Note: action_plan will hold raw pointers to SourceControlFileLocations from this map
        auto action_plan = create_feature_install_plan(
            provider, var_provider, specs, status_db, packages_dir_assigner, create_options);

        action_plan.print_unsupported_warnings();
        var_provider.load_tag_vars(action_plan.install_actions, host_triplet);

        // install plan will be empty if it is already installed - need to change this at status paragraph part
        if (action_plan.empty())
        {
            Debug::print("Install plan cannot be empty");
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

#if defined(_WIN32)
        maybe_print_vs_prompt_warning(action_plan.install_actions);
#endif // defined(_WIN32)

        const auto formatted = print_plan(action_plan);
        if (!is_recursive && formatted.has_removals)
        {
            msg::println_warning(msgPackagesToRebuildSuggestRecurse);
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        auto it_pkgsconfig = options.settings.find(SwitchXWriteNuGetPackagesConfig);
        if (it_pkgsconfig != options.settings.end())
        {
            get_global_metrics_collector().track_define(DefineMetric::X_WriteNuGetPackagesConfig);
            compute_all_abis(paths, action_plan, var_provider, status_db);

            auto pkgsconfig_path = paths.original_cwd / it_pkgsconfig->second;
            auto pkgsconfig_contents = generate_nuget_packages_config(action_plan, args.nuget_id_prefix.value_or(""));
            fs.write_contents(pkgsconfig_path, pkgsconfig_contents, VCPKG_LINE_INFO);
            msg::println(msgWroteNuGetPkgConfInfo, msg::path = pkgsconfig_path);
        }
        else if (!dry_run)
        {
            compute_all_abis(paths, action_plan, var_provider, status_db);
        }

        if (dry_run)
        {
            Checks::exit_success(VCPKG_LINE_INFO);
        }

        paths.flush_lockfile();

        track_install_plan(action_plan);
        install_preclear_plan_packages(paths, action_plan);

        BinaryCache binary_cache(fs);
        if (!only_downloads)
        {
            if (!binary_cache.install_providers(console_diagnostic_context, args, paths))
            {
                Checks::exit_fail(VCPKG_LINE_INFO);
            }
        }

        binary_cache.fetch(console_diagnostic_context, fs, action_plan.install_actions);
        const InstallSummary summary = install_execute_plan(args,
                                                            paths,
                                                            host_triplet,
                                                            build_package_options,
                                                            action_plan,
                                                            status_db,
                                                            binary_cache,
                                                            null_build_logs_recorder);
        msg::println(msgTotalInstallTime, msg::elapsed = summary.elapsed);
        // Skip printing the summary without --keep-going because the status without it is 'obvious': everything was a
        // success.
        if (keep_going == KeepGoing::Yes)
        {
            msg::print(summary.format_results());
        }

        auto it_xunit = options.settings.find(SwitchXXUnit);
        if (it_xunit != options.settings.end())
        {
            XunitWriter xwriter;

            for (auto&& result : summary.install_results)
            {
                xwriter.add_test_results(
                    result.build_result.spec,
                    CiResult{
                        result.build_result.code,
                        CiBuiltResult{result.package_abi(), result.feature_list(), result.start_time, result.timing}});
            }

            fs.write_contents(it_xunit->second, xwriter.build_xml(default_triplet), VCPKG_LINE_INFO);
        }

        summary.license_report.print_license_report(msgPackageLicenseSpdxThisInstall);

        if (print_cmake_usage)
        {
            std::set<std::string> printed_usages;
            for (auto&& result : summary.install_results)
            {
                if (!result.is_user_requested_install()) continue;
                // If a package failed to build, don't attempt to print usage.
                // e.g. --keep-going
                if (auto built_package = result.build_result.binary_control_file.get())
                {
                    install_print_usage_information(
                        built_package->core_paragraph, printed_usages, fs, paths.installed());
                }
            }
        }
        binary_cache.wait_for_async_complete_and_join();
        summary.print_complete_message();
        Checks::exit_with_code(VCPKG_LINE_INFO, summary.failed);
    }

    SpecSummary::SpecSummary(ExtendedBuildResult&& build_result,
                             ElapsedTime timing,
                             std::chrono::system_clock::time_point start_time)
        : build_result(std::move(build_result)), timing(timing), start_time(start_time)
    {
    }

    std::string SpecSummary::to_string() const { return adapt_to_string(*this); }
    void SpecSummary::to_string(std::string& out_str) const
    {
        build_result.spec.to_string(out_str);
        out_str.append(": ");
        out_str.append(vcpkg::to_string(build_result.code).data());
        out_str.append(": ");
        timing.to_string(out_str);
    }

    InstallSpecSummary::InstallSpecSummary(ExtendedBuildResult&& build_result,
                                           const InternalFeatureSet& feature_list,
                                           const Version& version,
                                           RequestType request_type,
                                           ElapsedTime timing,
                                           std::chrono::system_clock::time_point start_time,
                                           StringView package_abi,
                                           const CompilerInfo* compiler_info)
        : SpecSummary(std::move(build_result), timing, start_time)
        , m_package_abi(package_abi.data(), package_abi.size())
        , m_feature_list(feature_list)
        , m_version(version)
        , m_request_type(request_type)
        , m_compiler_info(compiler_info)
    {
    }

    void track_install_plan(const ActionPlan& plan)
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
            if (!specs_string.empty()) specs_string.push_back(',');
            specs_string +=
                Strings::concat(Hash::get_string_hash(install_action.spec.name(), Hash::Algorithm::Sha256),
                                ":",
                                hash_triplet(install_action.spec.triplet()),
                                ":",
                                Hash::get_string_hash(install_action.version.text, Hash::Algorithm::Sha256));
        }

        get_global_metrics_collector().track_string(StringMetric::InstallPlan_1, specs_string);
    }
}
