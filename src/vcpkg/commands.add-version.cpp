
#include <vcpkg/base/checks.h>
#include <vcpkg/base/expected.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/git.h>
#include <vcpkg/base/json.h>
#include <vcpkg/base/strings.h>

#include <vcpkg/commands.add-version.h>
#include <vcpkg/configuration.h>
#include <vcpkg/paragraphs.h>
#include <vcpkg/portfileprovider.h>
#include <vcpkg/portlint.h>
#include <vcpkg/registries.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>
#include <vcpkg/versions.h>

using namespace vcpkg;

namespace
{
    constexpr StringLiteral BASELINE = "baseline";
    constexpr StringLiteral VERSION_RELAXED = "version";
    constexpr StringLiteral VERSION_SEMVER = "version-semver";
    constexpr StringLiteral VERSION_DATE = "version-date";
    constexpr StringLiteral VERSION_STRING = "version-string";

    static constexpr StringLiteral OPTION_ALL = "all";
    static constexpr StringLiteral OPTION_OVERWRITE_VERSION = "overwrite-version";
    static constexpr StringLiteral OPTION_SKIP_FORMATTING_CHECK = "skip-formatting-check";
    static constexpr StringLiteral OPTION_SKIP_VERSION_FORMAT_CHECK = "skip-version-format-check";
    static constexpr StringLiteral OPTION_SKIP_LICENSE_CHECK = "skip-license-check";
    static constexpr StringLiteral OPTION_SKIP_PORTFILE_CHECK = "skip-portfile-check";
    static constexpr StringLiteral OPTION_VERBOSE = "verbose";

    enum class UpdateResult
    {
        Updated,
        NotUpdated
    };
    using VersionGitTree = std::pair<SchemedVersion, std::string>;

    void insert_version_to_json_object(Json::Object& obj, const Version& version, StringLiteral version_field)
    {
        obj.insert(version_field, Json::Value::string(version.text()));
        obj.insert("port-version", Json::Value::integer(version.port_version()));
    }

    void insert_schemed_version_to_json_object(Json::Object& obj, const SchemedVersion& version)
    {
        if (version.scheme == VersionScheme::Relaxed)
        {
            return insert_version_to_json_object(obj, version.version, VERSION_RELAXED);
        }

        if (version.scheme == VersionScheme::Semver)
        {
            return insert_version_to_json_object(obj, version.version, VERSION_SEMVER);
        }

        if (version.scheme == VersionScheme::Date)
        {
            return insert_version_to_json_object(obj, version.version, VERSION_DATE);
        }

        if (version.scheme == VersionScheme::String)
        {
            return insert_version_to_json_object(obj, version.version, VERSION_STRING);
        }
        Checks::unreachable(VCPKG_LINE_INFO);
    }

    static Json::Object serialize_baseline(const std::map<std::string, Version, std::less<>>& baseline)
    {
        Json::Object port_entries_obj;
        for (auto&& kv_pair : baseline)
        {
            Json::Object baseline_version_obj;
            insert_version_to_json_object(baseline_version_obj, kv_pair.second, BASELINE);
            port_entries_obj.insert(kv_pair.first, baseline_version_obj);
        }

        Json::Object baseline_obj;
        baseline_obj.insert("default", port_entries_obj);
        return baseline_obj;
    }

    static Json::Object serialize_versions(const std::vector<VersionGitTree>& versions)
    {
        Json::Array versions_array;
        for (auto&& version : versions)
        {
            Json::Object version_obj;
            version_obj.insert("git-tree", Json::Value::string(version.second));
            insert_schemed_version_to_json_object(version_obj, version.first);
            versions_array.push_back(std::move(version_obj));
        }

        Json::Object output_object;
        output_object.insert("versions", versions_array);
        return output_object;
    }

    static void write_baseline_file(Filesystem& fs,
                                    const std::map<std::string, Version, std::less<>>& baseline_map,
                                    const Path& output_path)
    {
        auto new_path = output_path + ".tmp";
        fs.create_directories(output_path.parent_path(), VCPKG_LINE_INFO);
        fs.write_contents(new_path, Json::stringify(serialize_baseline(baseline_map)), VCPKG_LINE_INFO);
        fs.rename(new_path, output_path, VCPKG_LINE_INFO);
    }

    static void write_versions_file(Filesystem& fs,
                                    const std::vector<VersionGitTree>& versions,
                                    const Path& output_path)
    {
        auto new_path = output_path + ".tmp";
        fs.create_directories(output_path.parent_path(), VCPKG_LINE_INFO);
        fs.write_contents(new_path, Json::stringify(serialize_versions(versions)), VCPKG_LINE_INFO);
        fs.rename(new_path, output_path, VCPKG_LINE_INFO);
    }

    static UpdateResult update_baseline_version(const VcpkgPaths& paths,
                                                const std::string& port_name,
                                                const Version& version,
                                                const Path& baseline_path,
                                                std::map<std::string, vcpkg::Version, std::less<>>& baseline_map,
                                                bool print_success)
    {
        auto& fs = paths.get_filesystem();

        auto it = baseline_map.find(port_name);
        if (it != baseline_map.end())
        {
            auto& baseline_version = it->second;
            if (baseline_version == version)
            {
                if (print_success)
                {
                    msg::println(Color::success,
                                 msgAddVersionVersionAlreadyInFile,
                                 msg::version = version,
                                 msg::path = baseline_path);
                }
                return UpdateResult::NotUpdated;
            }
            baseline_version = version;
        }
        else
        {
            baseline_map.emplace(port_name, version);
        }

        write_baseline_file(fs, baseline_map, baseline_path);
        if (print_success)
        {
            msg::println(
                Color::success, msgAddVersionAddedVersionToFile, msg::version = version, msg::path = baseline_path);
        }
        return UpdateResult::Updated;
    }

    static UpdateResult update_version_db_file(const VcpkgPaths& paths,
                                               const std::string& port_name,
                                               const SchemedVersion& port_version,
                                               const std::string& git_tree,
                                               const Path& version_db_file_path,
                                               bool overwrite_version,
                                               bool print_success,
                                               bool keep_going,
                                               bool skip_version_format_check,
                                               bool skip_license_check,
                                               bool skip_portfile_check,
                                               SourceControlFileAndLocation& scf)
    {
        auto& fs = paths.get_filesystem();
        const auto lint_port = [=, &scf, &fs]() {
            Lint::Status status = Lint::Status::Ok;
            if (!skip_version_format_check)
            {
                if (Lint::check_used_version_scheme(*scf.source_control_file, Lint::Fix::NO) == Lint::Status::Problem)
                {
                    status = Lint::Status::Problem;
                    msg::println(msgAddVersionDisableCheck, msg::option = OPTION_SKIP_VERSION_FORMAT_CHECK);
                }
            }
            if (!skip_license_check)
            {
                if (Lint::check_license_expression(*scf.source_control_file, Lint::Fix::NO) == Lint::Status::Problem)
                {
                    status = Lint::Status::Problem;
                    msg::println(msgAddVersionDisableCheck, msg::option = OPTION_SKIP_LICENSE_CHECK);
                }
            }
            if (!skip_portfile_check)
            {
                if (Lint::check_portfile_deprecated_functions(fs, scf, Lint::Fix::NO) == Lint::Status::Problem)
                {
                    status = Lint::Status::Problem;
                    msg::println(msgAddVersionDisableCheck, msg::option = OPTION_SKIP_PORTFILE_CHECK);
                }
            }
            Checks::msg_check_exit(
                VCPKG_LINE_INFO, status == Lint::Status::Ok, msgAddVersionLintPort, msg::package_name = port_name);
        };
        if (!fs.exists(version_db_file_path, IgnoreErrors{}))
        {
            lint_port();
            std::vector<VersionGitTree> new_entry{{port_version, git_tree}};
            write_versions_file(fs, new_entry, version_db_file_path);
            if (print_success)
            {
                msg::println(Color::success,
                             msg::format(msgAddVersionAddedVersionToFile,
                                         msg::version = port_version.version,
                                         msg::path = version_db_file_path)
                                 .append_raw(' ')
                                 .append(msgAddVersionNewFile));
            }
            return UpdateResult::Updated;
        }

        auto maybe_versions = get_builtin_versions(paths, port_name);
        if (auto versions = maybe_versions.get())
        {
            const auto& versions_end = versions->end();

            auto found_same_sha = std::find_if(
                versions->begin(), versions_end, [&](auto&& entry) -> bool { return entry.second == git_tree; });
            if (found_same_sha != versions_end)
            {
                if (found_same_sha->first.version == port_version.version)
                {
                    if (print_success)
                    {
                        msg::println(Color::success,
                                     msgAddVersionVersionAlreadyInFile,
                                     msg::version = port_version.version,
                                     msg::path = version_db_file_path);
                    }
                    return UpdateResult::NotUpdated;
                }
                msg::println_warning(msg::format(msgAddVersionPortFilesShaUnchanged,
                                                 msg::package_name = port_name,
                                                 msg::version = found_same_sha->first.version)
                                         .append_raw("\n-- SHA: ")
                                         .append_raw(git_tree)
                                         .append_raw("\n-- ")
                                         .append(msgAddVersionCommitChangesReminder)
                                         .append_raw("\n***")
                                         .append(msgAddVersionNoFilesUpdated)
                                         .append_raw("***"));
                if (keep_going) return UpdateResult::NotUpdated;
                Checks::exit_fail(VCPKG_LINE_INFO);
            }

            auto it = std::find_if(
                versions->begin(), versions_end, [&](const std::pair<SchemedVersion, std::string>& entry) -> bool {
                    return entry.first.version == port_version.version;
                });

            if (it != versions_end)
            {
                if (!overwrite_version)
                {
                    msg::println_error(
                        msg::format(msgAddVersionPortFilesShaChanged, msg::package_name = port_name)
                            .append_raw('\n')
                            .append(msgAddVersionVersionIs, msg::version = port_version.version)
                            .append_raw('\n')
                            .append(msgAddVersionOldShaIs, msg::commit_sha = it->second)
                            .append_raw('\n')
                            .append(msgAddVersionNewShaIs, msg::commit_sha = git_tree)
                            .append_raw('\n')
                            .append(msgAddVersionUpdateVersionReminder)
                            .append_raw('\n')
                            .append(msgAddVersionOverwriteOptionSuggestion, msg::option = OPTION_OVERWRITE_VERSION)
                            .append_raw("\n***")
                            .append(msgAddVersionNoFilesUpdated)
                            .append_raw("***"));
                    if (keep_going) return UpdateResult::NotUpdated;
                    Checks::exit_fail(VCPKG_LINE_INFO);
                }

                it->first = port_version;
                it->second = git_tree;
            }
            else
            {
                versions->insert(versions->begin(), std::make_pair(port_version, git_tree));
            }

            lint_port();

            write_versions_file(fs, *versions, version_db_file_path);
            if (print_success)
            {
                msg::println(Color::success,
                             msgAddVersionAddedVersionToFile,
                             msg::version = port_version.version,
                             msg::path = version_db_file_path);
            }
            return UpdateResult::Updated;
        }

        msg::println_error(msg::format(msgAddVersionUnableToParseVersionsFile, msg::path = version_db_file_path)
                               .append_raw('\n')
                               .append(maybe_versions.error()));
        Checks::exit_fail(VCPKG_LINE_INFO);
    }
}

namespace vcpkg::Commands::AddVersion
{
    const CommandSwitch COMMAND_SWITCHES[] = {
        {OPTION_ALL, []() { return msg::format(msgCmdAddVersionOptAll); }},
        {OPTION_OVERWRITE_VERSION, []() { return msg::format(msgCmdAddVersionOptOverwriteVersion); }},
        {OPTION_SKIP_FORMATTING_CHECK, []() { return msg::format(msgCmdAddVersionOptSkipFormatChk); }},
        {OPTION_SKIP_VERSION_FORMAT_CHECK, []() { return msg::format(msgCmdAddVersionOptSkipVersionFormatChk); }},
        {OPTION_SKIP_LICENSE_CHECK, []() { return msg::format(msgCmdAddVersionOptSkipLicenseChk); }},
        {OPTION_SKIP_PORTFILE_CHECK, []() { return msg::format(msgCmdAddVersionOptSkipPortfileChk); }},
        {OPTION_VERBOSE, []() { return msg::format(msgCmdAddVersionOptVerbose); }},
    };

    const CommandStructure COMMAND_STRUCTURE{
        [] { return create_example_string("x-add-version <port name>"); },
        0,
        1,
        {{COMMAND_SWITCHES}, {}, {}},
        nullptr,
    };

    void perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        auto parsed_args = args.parse_arguments(COMMAND_STRUCTURE);
        const bool add_all = Util::Sets::contains(parsed_args.switches, OPTION_ALL);
        const bool overwrite_version = Util::Sets::contains(parsed_args.switches, OPTION_OVERWRITE_VERSION);
        const bool skip_formatting_check = Util::Sets::contains(parsed_args.switches, OPTION_SKIP_FORMATTING_CHECK);
        const bool skip_version_format_check =
            Util::Sets::contains(parsed_args.switches, OPTION_SKIP_VERSION_FORMAT_CHECK);
        const bool skip_license_check = Util::Sets::contains(parsed_args.switches, OPTION_SKIP_LICENSE_CHECK);
        const bool skip_portfile_check = Util::Sets::contains(parsed_args.switches, OPTION_SKIP_PORTFILE_CHECK);
        const bool verbose = !add_all || Util::Sets::contains(parsed_args.switches, OPTION_VERBOSE);

        auto& fs = paths.get_filesystem();
        auto baseline_path = paths.builtin_registry_versions / "baseline.json";
        if (!fs.exists(baseline_path, IgnoreErrors{}))
        {
            Checks::msg_exit_with_error(VCPKG_LINE_INFO, msgAddVersionFileNotFound, msg::path = baseline_path);
        }

        std::vector<std::string> port_names;
        if (!parsed_args.command_arguments.empty())
        {
            if (add_all)
            {
                msg::println_warning(msgAddVersionIgnoringOptionAll, msg::option = OPTION_ALL);
            }
            port_names.emplace_back(parsed_args.command_arguments[0]);
        }
        else
        {
            Checks::msg_check_exit(VCPKG_LINE_INFO,
                                   add_all,
                                   msgAddVersionUseOptionAll,
                                   msg::command_name = "x-add-version",
                                   msg::option = OPTION_ALL);

            for (auto&& port_dir : fs.get_directories_non_recursive(paths.builtin_ports_directory(), VCPKG_LINE_INFO))
            {
                port_names.emplace_back(port_dir.stem().to_string());
            }
        }

        auto baseline_map = [&]() -> std::map<std::string, vcpkg::Version, std::less<>> {
            if (!fs.exists(baseline_path, IgnoreErrors{}))
            {
                std::map<std::string, vcpkg::Version, std::less<>> ret;
                return ret;
            }
            auto maybe_baseline_map = vcpkg::get_builtin_baseline(paths);
            return maybe_baseline_map.value_or_exit(VCPKG_LINE_INFO);
        }();

        // Get tree-ish from local repository state.
        auto maybe_git_tree_map = paths.git_get_local_port_treeish_map();
        auto git_tree_map = maybe_git_tree_map.value_or_exit(VCPKG_LINE_INFO);

        // Find ports with uncommited changes
        std::set<std::string> changed_ports;
        auto git_config = paths.git_builtin_config();
        auto maybe_changes = git_ports_with_uncommitted_changes(git_config);
        if (auto changes = maybe_changes.get())
        {
            changed_ports.insert(changes->begin(), changes->end());
        }
        else if (verbose)
        {
            msg::println_warning(msgAddVersionDetectLocalChangesError);
        }

        for (auto&& port_name : port_names)
        {
            auto port_dir = paths.builtin_ports_directory() / port_name;

            if (!fs.exists(port_dir, IgnoreErrors{}))
            {
                msg::println_error(msgPortDoesNotExist, msg::package_name = port_name);
                Checks::check_exit(VCPKG_LINE_INFO, !add_all);
                continue;
            }

            auto maybe_scf = Paragraphs::try_load_port(fs, paths.builtin_ports_directory() / port_name);
            if (!maybe_scf)
            {
                msg::println_error(msgAddVersionLoadPortFailed, msg::package_name = port_name);
                print_error_message(maybe_scf.error());
                Checks::check_exit(VCPKG_LINE_INFO, !add_all);
                continue;
            }

            SourceControlFileAndLocation scf{std::move(maybe_scf).value(VCPKG_LINE_INFO),
                                             paths.builtin_ports_directory() / port_name};

            if (!skip_formatting_check)
            {
                // check if manifest file is property formatted
                const auto path_to_manifest = paths.builtin_ports_directory() / port_name / "vcpkg.json";
                if (fs.exists(path_to_manifest, IgnoreErrors{}))
                {
                    const auto current_file_content = fs.read_contents(path_to_manifest, VCPKG_LINE_INFO);
                    const auto json = serialize_manifest(*scf.source_control_file);
                    const auto formatted_content = Json::stringify(json);
                    if (current_file_content != formatted_content)
                    {
                        auto command_line = fmt::format("vcpkg format-manifest ports/{}/vcpkg.json", port_name);
                        msg::println_error(
                            msg::format(msgAddVersionPortHasImproperFormat, msg::package_name = port_name)
                                .append_raw('\n')
                                .append(msgAddVersionFormatPortSuggestion, msg::command_line = command_line)
                                .append_raw('\n')
                                .append(msgAddVersionCommitResultReminder)
                                .append_raw('\n'));
                        Checks::check_exit(VCPKG_LINE_INFO, !add_all);
                        continue;
                    }
                }
            }

            // find local uncommitted changes on port
            if (Util::Sets::contains(changed_ports, port_name))
            {
                msg::println_warning(msgAddVersionUncommittedChanges, msg::package_name = port_name);
            }

            const auto& schemed_version = scf.source_control_file->to_schemed_version();

            auto git_tree_it = git_tree_map.find(port_name);
            if (git_tree_it == git_tree_map.end())
            {
                msg::println_warning(msg::format(msgAddVersionNoGitSha, msg::package_name = port_name)
                                         .append_raw("\n-- ")
                                         .append(msgAddVersionCommitChangesReminder)
                                         .append_raw("\n***")
                                         .append(msgAddVersionNoFilesUpdated)
                                         .append_raw("***"));
                if (add_all) continue;
                Checks::exit_fail(VCPKG_LINE_INFO);
            }
            const auto& git_tree = git_tree_it->second;

            char prefix[] = {port_name[0], '-', '\0'};
            auto port_versions_path = paths.builtin_registry_versions / prefix / Strings::concat(port_name, ".json");
            auto updated_versions_file = update_version_db_file(paths,
                                                                port_name,
                                                                schemed_version,
                                                                git_tree,
                                                                port_versions_path,
                                                                overwrite_version,
                                                                verbose,
                                                                add_all,
                                                                skip_version_format_check,
                                                                skip_license_check,
                                                                skip_portfile_check,
                                                                scf);
            auto updated_baseline_file = update_baseline_version(
                paths, port_name, schemed_version.version, baseline_path, baseline_map, verbose);
            if (verbose && updated_versions_file == UpdateResult::NotUpdated &&
                updated_baseline_file == UpdateResult::NotUpdated)
            {
                msg::println(msgAddVersionNoFilesUpdatedForPort, msg::package_name = port_name);
            }
        }
        Checks::exit_success(VCPKG_LINE_INFO);
    }
}
