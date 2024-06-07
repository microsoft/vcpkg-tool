
#include <vcpkg/base/checks.h>
#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/git.h>
#include <vcpkg/base/json.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/system.process.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.add-version.h>
#include <vcpkg/paragraphs.h>
#include <vcpkg/registries.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>
#include <vcpkg/versiondeserializers.h>
#include <vcpkg/versions.h>

using namespace vcpkg;

namespace
{
    enum class UpdateResult
    {
        Updated,
        NotUpdated
    };

    void insert_version_to_json_object(Json::Object& obj, const Version& version, StringLiteral version_field)
    {
        obj.insert(version_field, Json::Value::string(version.text));
        obj.insert(JsonIdPortVersion, Json::Value::integer(version.port_version));
    }

    void insert_schemed_version_to_json_object(Json::Object& obj, const SchemedVersion& version)
    {
        if (version.scheme == VersionScheme::Relaxed)
        {
            return insert_version_to_json_object(obj, version.version, JsonIdVersion);
        }

        if (version.scheme == VersionScheme::Semver)
        {
            return insert_version_to_json_object(obj, version.version, JsonIdVersionSemver);
        }

        if (version.scheme == VersionScheme::Date)
        {
            return insert_version_to_json_object(obj, version.version, JsonIdVersionDate);
        }

        if (version.scheme == VersionScheme::String)
        {
            return insert_version_to_json_object(obj, version.version, JsonIdVersionString);
        }
        Checks::unreachable(VCPKG_LINE_INFO);
    }

    void check_used_version_scheme(const SchemedVersion& version, const std::string& port_name)
    {
        if (version.scheme == VersionScheme::String)
        {
            if (DateVersion::try_parse(version.version.text))
            {
                Checks::msg_exit_with_message(
                    VCPKG_LINE_INFO, msgAddVersionSuggestVersionDate, msg::package_name = port_name);
            }
            if (DotVersion::try_parse_relaxed(version.version.text))
            {
                Checks::msg_exit_with_message(
                    VCPKG_LINE_INFO, msgAddVersionSuggestVersionRelaxed, msg::package_name = port_name);
            }
        }
    }

    Json::Object serialize_baseline(const std::map<std::string, Version, std::less<>>& baseline)
    {
        Json::Object port_entries_obj;
        for (auto&& kv_pair : baseline)
        {
            Json::Object baseline_version_obj;
            insert_version_to_json_object(baseline_version_obj, kv_pair.second, JsonIdBaseline);
            port_entries_obj.insert(kv_pair.first, baseline_version_obj);
        }

        Json::Object baseline_obj;
        baseline_obj.insert("default", port_entries_obj);
        return baseline_obj;
    }

    Json::Object serialize_versions(const std::vector<GitVersionDbEntry>& versions)
    {
        Json::Array versions_array;
        for (auto&& version : versions)
        {
            Json::Object version_obj;
            version_obj.insert(JsonIdGitTree, Json::Value::string(version.git_tree));
            insert_schemed_version_to_json_object(version_obj, version.version);
            versions_array.push_back(std::move(version_obj));
        }

        Json::Object output_object;
        output_object.insert(JsonIdVersions, versions_array);
        return output_object;
    }

    void write_baseline_file(const Filesystem& fs,
                             const std::map<std::string, Version, std::less<>>& baseline_map,
                             const Path& output_path)
    {
        auto new_path = output_path + ".tmp";
        fs.create_directories(output_path.parent_path(), VCPKG_LINE_INFO);
        fs.write_contents(new_path, Json::stringify(serialize_baseline(baseline_map)), VCPKG_LINE_INFO);
        fs.rename(new_path, output_path, VCPKG_LINE_INFO);
    }

    void write_versions_file(const Filesystem& fs,
                             const std::vector<GitVersionDbEntry>& versions,
                             const Path& output_path)
    {
        auto new_path = output_path + ".tmp";
        fs.create_directories(output_path.parent_path(), VCPKG_LINE_INFO);
        fs.write_contents(new_path, Json::stringify(serialize_versions(versions)), VCPKG_LINE_INFO);
        fs.rename(new_path, output_path, VCPKG_LINE_INFO);
    }

    UpdateResult update_baseline_version(const VcpkgPaths& paths,
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

    UpdateResult update_version_db_file(const VcpkgPaths& paths,
                                        const std::string& port_name,
                                        const SchemedVersion& port_version,
                                        const std::string& git_tree,
                                        bool overwrite_version,
                                        bool print_success,
                                        bool keep_going,
                                        bool skip_version_format_check)
    {
        auto& fs = paths.get_filesystem();
        auto maybe_maybe_versions = load_git_versions_file(fs, paths.builtin_registry_versions, port_name);
        auto maybe_versions = maybe_maybe_versions.entries.get();
        if (!maybe_versions)
        {
            msg::println(Color::error, maybe_maybe_versions.entries.error());
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        auto versions = maybe_versions->get();
        if (!versions)
        {
            if (!skip_version_format_check)
            {
                check_used_version_scheme(port_version, port_name);
            }
            std::vector<GitVersionDbEntry> new_entry{{port_version, git_tree}};
            write_versions_file(fs, new_entry, maybe_maybe_versions.versions_file_path);
            if (print_success)
            {
                msg::println(Color::success,
                             msg::format(msgAddVersionAddedVersionToFile,
                                         msg::version = port_version.version,
                                         msg::path = maybe_maybe_versions.versions_file_path)
                                 .append_raw(' ')
                                 .append(msgAddVersionNewFile));
            }
            return UpdateResult::Updated;
        }

        const auto& versions_end = versions->end();
        auto found_same_sha = std::find_if(
            versions->begin(), versions_end, [&](auto&& entry) -> bool { return entry.git_tree == git_tree; });
        if (found_same_sha != versions_end)
        {
            if (found_same_sha->version.version == port_version.version)
            {
                if (print_success)
                {
                    msg::println(Color::success,
                                 msgAddVersionVersionAlreadyInFile,
                                 msg::version = port_version.version,
                                 msg::path = maybe_maybe_versions.versions_file_path);
                }
                return UpdateResult::NotUpdated;
            }
            msg::println_warning(msg::format(msgAddVersionPortFilesShaUnchanged,
                                             msg::package_name = port_name,
                                             msg::version = found_same_sha->version.version)
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

        auto it = std::find_if(versions->begin(), versions_end, [&](const GitVersionDbEntry& entry) -> bool {
            return entry.version.version == port_version.version;
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
                        .append(msgAddVersionOldShaIs, msg::commit_sha = it->git_tree)
                        .append_raw('\n')
                        .append(msgAddVersionNewShaIs, msg::commit_sha = git_tree)
                        .append_raw('\n')
                        .append(msgAddVersionUpdateVersionReminder)
                        .append_raw('\n')
                        .append(msgAddVersionOverwriteOptionSuggestion, msg::option = SwitchOverwriteVersion)
                        .append_raw("\n***")
                        .append(msgAddVersionNoFilesUpdated)
                        .append_raw("***"));
                if (keep_going) return UpdateResult::NotUpdated;
                Checks::exit_fail(VCPKG_LINE_INFO);
            }

            it->version = port_version;
            it->git_tree = git_tree;
        }
        else
        {
            versions->insert(versions->begin(), GitVersionDbEntry{port_version, git_tree});
        }

        if (!skip_version_format_check)
        {
            check_used_version_scheme(port_version, port_name);
        }

        write_versions_file(fs, *versions, maybe_maybe_versions.versions_file_path);
        if (print_success)
        {
            msg::println(Color::success,
                         msgAddVersionAddedVersionToFile,
                         msg::version = port_version.version,
                         msg::path = maybe_maybe_versions.versions_file_path);
        }
        return UpdateResult::Updated;
    }

    constexpr CommandSwitch AddVersionSwitches[] = {
        {SwitchAll, msgCmdAddVersionOptAll},
        {SwitchOverwriteVersion, msgCmdAddVersionOptOverwriteVersion},
        {SwitchSkipFormattingCheck, msgCmdAddVersionOptSkipFormatChk},
        {SwitchSkipVersionFormatCheck, msgCmdAddVersionOptSkipVersionFormatChk},
        {SwitchVerbose, msgCmdAddVersionOptVerbose},
    };
} // unnamed namespace

namespace vcpkg
{
    constexpr CommandMetadata CommandAddVersionMetadata{
        "x-add-version",
        msgCmdAddVersionSynopsis,
        {msgCmdAddVersionExample1, "vcpkg x-add-version curl --overwrite-version"},
        Undocumented,
        AutocompletePriority::Public,
        0,
        1,
        {AddVersionSwitches},
        nullptr,
    };

    void command_add_version_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        auto parsed_args = args.parse_arguments(CommandAddVersionMetadata);
        const bool add_all = Util::Sets::contains(parsed_args.switches, SwitchAll);
        const bool overwrite_version = Util::Sets::contains(parsed_args.switches, SwitchOverwriteVersion);
        const bool skip_formatting_check = Util::Sets::contains(parsed_args.switches, SwitchSkipFormattingCheck);
        const bool skip_version_format_check = Util::Sets::contains(parsed_args.switches, SwitchSkipVersionFormatCheck);
        const bool verbose = !add_all || Util::Sets::contains(parsed_args.switches, SwitchVerbose);

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
                msg::println_warning(msgAddVersionIgnoringOptionAll, msg::option = SwitchAll);
            }
            port_names.emplace_back(parsed_args.command_arguments[0]);
        }
        else
        {
            Checks::msg_check_exit(VCPKG_LINE_INFO,
                                   add_all,
                                   msgAddVersionUseOptionAll,
                                   msg::command_name = "x-add-version",
                                   msg::option = SwitchAll);

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
        auto& git_tree_map = maybe_git_tree_map.value_or_exit(VCPKG_LINE_INFO);

        // Find ports with uncommitted changes
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

            auto maybe_scfl = Paragraphs::try_load_port_required(
                                  fs, port_name, PortLocation{paths.builtin_ports_directory() / port_name})
                                  .maybe_scfl;
            auto scfl = maybe_scfl.get();
            if (!scfl)
            {
                msg::println(Color::error, maybe_scfl.error());
                Checks::check_exit(VCPKG_LINE_INFO, !add_all);
                continue;
            }

            if (!skip_formatting_check)
            {
                // check if manifest file is property formatted

                if (scfl->control_path.filename() == FileVcpkgDotJson)
                {
                    const auto current_file_content = fs.read_contents(scfl->control_path, VCPKG_LINE_INFO);
                    const auto json = serialize_manifest(*scfl->source_control_file);
                    const auto formatted_content = Json::stringify(json);
                    if (current_file_content != formatted_content)
                    {
                        std::string command_line = "vcpkg format-manifest ";
                        append_shell_escaped(command_line, scfl->control_path);
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

            auto schemed_version = scfl->source_control_file->to_schemed_version();
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
            auto updated_versions_file = update_version_db_file(paths,
                                                                port_name,
                                                                schemed_version,
                                                                git_tree,
                                                                overwrite_version,
                                                                verbose,
                                                                add_all,
                                                                skip_version_format_check);
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
} // namespace vcpkg
