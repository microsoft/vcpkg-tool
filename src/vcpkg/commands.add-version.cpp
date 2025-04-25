
#include <vcpkg/base/checks.h>
#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/git.h>
#include <vcpkg/base/json.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/system.process.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.add-version.h>
#include <vcpkg/documentation.h>
#include <vcpkg/paragraphs.h>
#include <vcpkg/registries.h>
#include <vcpkg/tools.h>
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

    bool check_used_version_scheme(const SchemedVersion& version, const std::string& port_name)
    {
        if (version.scheme == VersionScheme::String)
        {
            if (DateVersion::try_parse(version.version.text))
            {
                msg::println(msg::format(msgAddVersionSuggestVersionDate, msg::package_name = port_name)
                                 .append_raw("\n")
                                 .append(msgSeeURL, msg::url = docs::version_schemes));
                return true;
            }

            if (DotVersion::try_parse_relaxed(version.version.text))
            {
                msg::println(msg::format(msgAddVersionSuggestVersionRelaxed, msg::package_name = port_name)
                                 .append_raw("\n")
                                 .append(msgSeeURL, msg::url = docs::version_schemes));
                return true;
            }
        }

        return false;
    }

    Json::Object serialize_baseline(const std::map<std::string, Version, std::less<>>& baseline)
    {
        Json::Object port_entries_obj;
        for (auto&& kv_pair : baseline)
        {
            Json::Object baseline_version_obj;
            insert_version_to_json_object(baseline_version_obj, kv_pair.second, JsonIdBaseline);
            port_entries_obj.insert(kv_pair.first, std::move(baseline_version_obj));
        }

        Json::Object baseline_obj;
        baseline_obj.insert(JsonIdDefault, std::move(port_entries_obj));
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
        output_object.insert(JsonIdVersions, std::move(versions_array));
        return output_object;
    }

    static void write_json_file(const Filesystem& fs, const Json::Object& obj, const Path& output_path)
    {
        auto new_path = output_path + ".tmp";
        fs.create_directories(output_path.parent_path(), VCPKG_LINE_INFO);
        fs.write_contents(new_path, Json::stringify(obj), VCPKG_LINE_INFO);
        fs.rename(new_path, output_path, VCPKG_LINE_INFO);
    }

    static void write_versions_file(const Filesystem& fs,
                                    const std::vector<GitVersionDbEntry>& versions,
                                    const Path& output_path)
    {
        write_json_file(fs, serialize_versions(versions), output_path);
    }

    UpdateResult update_baseline_version(const Filesystem& fs,
                                         const std::string& port_name,
                                         const Version& version,
                                         const Path& baseline_path,
                                         std::map<std::string, vcpkg::Version, std::less<>>& baseline_map,
                                         bool print_success)
    {
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

        write_json_file(fs, serialize_baseline(baseline_map), baseline_path);
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
                if (check_used_version_scheme(port_version, port_name))
                {
                    if (!keep_going)
                    {
                        return UpdateResult::NotUpdated;
                    }

                    Checks::exit_fail(VCPKG_LINE_INFO);
                }

                if (port_version.version.port_version != 0)
                {
                    msg::println_warning(msgAddVersionPortVersionShouldBeGone,
                                         msg::package_name = port_name,
                                         msg::version = port_version.version.text);
                    if (keep_going)
                    {
                        return UpdateResult::NotUpdated;
                    }

                    Checks::exit_fail(VCPKG_LINE_INFO);
                }
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

        const GitVersionDbEntry* exactly_matching_sha_version_entry = nullptr;
        GitVersionDbEntry* exactly_matching_version_entry = nullptr;
        const GitVersionDbEntry* highest_matching_version_entry = nullptr;
        for (auto&& version_entry : *versions)
        {
            if (version_entry.version.version.text == port_version.version.text)
            {
                if (version_entry.version.version.port_version == port_version.version.port_version)
                {
                    exactly_matching_version_entry = &version_entry;
                }

                if (!highest_matching_version_entry || highest_matching_version_entry->version.version.port_version <
                                                           version_entry.version.version.port_version)
                {
                    highest_matching_version_entry = &version_entry;
                }
            }

            if (version_entry.git_tree == git_tree)
            {
                exactly_matching_sha_version_entry = &version_entry;
            }
        }

        if (exactly_matching_sha_version_entry)
        {
            if (exactly_matching_version_entry == exactly_matching_sha_version_entry)
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
                                             msg::version = port_version.version)
                                     .append_raw("\n-- SHA: ")
                                     .append_raw(git_tree)
                                     .append_raw("\n-- ")
                                     .append(msgAddVersionCommitChangesReminder)
                                     .append_raw("\n*** ")
                                     .append(msgAddVersionNoFilesUpdated)
                                     .append_raw("\n*** ")
                                     .append(msgSeeURL, msg::url = docs::add_version_command_url)
                                     .append_raw("\n***"));
            if (keep_going)
            {
                return UpdateResult::NotUpdated;
            }

            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        if (exactly_matching_version_entry)
        {
            if (!overwrite_version)
            {
                msg::println_error(
                    msg::format(msgAddVersionPortFilesShaChanged, msg::package_name = port_name)
                        .append_raw('\n')
                        .append(msgAddVersionVersionIs, msg::version = port_version.version)
                        .append_raw('\n')
                        .append(msgAddVersionOldShaIs, msg::commit_sha = exactly_matching_version_entry->git_tree)
                        .append_raw('\n')
                        .append(msgAddVersionNewShaIs, msg::commit_sha = git_tree)
                        .append_raw('\n')
                        .append(msgAddVersionUpdateVersionReminder)
                        .append_raw('\n')
                        .append(msgAddVersionOverwriteOptionSuggestion, msg::option = SwitchOverwriteVersion)
                        .append_raw('\n')
                        .append(msgSeeURL, msg::url = docs::add_version_command_overwrite_version_opt_url)
                        .append_raw("\n***")
                        .append(msgAddVersionNoFilesUpdated)
                        .append_raw("***"));
                if (keep_going)
                {
                    return UpdateResult::NotUpdated;
                }

                Checks::exit_fail(VCPKG_LINE_INFO);
            }

            exactly_matching_version_entry->git_tree = git_tree;
        }
        else if (!skip_version_format_check && port_version.version.port_version != 0 &&
                 !highest_matching_version_entry)
        {
            msg::println_warning(msgAddVersionPortVersionShouldBeGone,
                                 msg::package_name = port_name,
                                 msg::version = port_version.version.text);
            if (keep_going)
            {
                return UpdateResult::NotUpdated;
            }

            Checks::exit_fail(VCPKG_LINE_INFO);
        }
        else if (!skip_version_format_check && port_version.version.port_version != 0 &&
                 highest_matching_version_entry->version.version.port_version !=
                     (port_version.version.port_version - 1))
        {
            msg::println_warning(msgAddVersionPortVersionShouldBeOneMore,
                                 msg::package_name = port_name,
                                 msg::version = port_version.version.text,
                                 msg::count = highest_matching_version_entry->version.version.port_version,
                                 msg::expected_version =
                                     highest_matching_version_entry->version.version.port_version + 1,
                                 msg::actual_version = port_version.version.port_version);
            if (keep_going)
            {
                return UpdateResult::NotUpdated;
            }

            Checks::exit_fail(VCPKG_LINE_INFO);
        }
        else
        {
            versions->insert(versions->begin(), GitVersionDbEntry{port_version, git_tree});
        }

        if (!skip_version_format_check)
        {
            if (check_used_version_scheme(port_version, port_name))
            {
                if (!keep_going)
                {
                    return UpdateResult::NotUpdated;
                }

                Checks::exit_fail(VCPKG_LINE_INFO);
            }
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
        SIZE_MAX,
        {AddVersionSwitches},
        nullptr,
    };

    void command_add_version_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        auto parsed_args = args.parse_arguments(CommandAddVersionMetadata);
        bool add_all = Util::Sets::contains(parsed_args.switches, SwitchAll);
        const bool overwrite_version = Util::Sets::contains(parsed_args.switches, SwitchOverwriteVersion);
        const bool skip_formatting_check = Util::Sets::contains(parsed_args.switches, SwitchSkipFormattingCheck);
        const bool skip_version_format_check = Util::Sets::contains(parsed_args.switches, SwitchSkipVersionFormatCheck);
        const bool verbose = !add_all || Util::Sets::contains(parsed_args.switches, SwitchVerbose);

        auto& fs = paths.get_filesystem();
        const auto& builtin_ports_directory = paths.builtin_ports_directory();
        auto baseline_path = paths.builtin_registry_versions / "baseline.json";
        if (!fs.exists(baseline_path, IgnoreErrors{}))
        {
            Checks::msg_exit_with_error(VCPKG_LINE_INFO, msgAddVersionFileNotFound, msg::path = baseline_path);
        }

        if (parsed_args.command_arguments.empty())
        {
            Checks::msg_check_exit(
                VCPKG_LINE_INFO,
                add_all,
                msg::format(msgAddVersionUseOptionAll, msg::command_name = "x-add-version", msg::option = SwitchAll)
                    .append_raw('\n')
                    .append(msgSeeURL, msg::url = docs::add_version_command_url));
        }

        std::vector<GitLSTreeEntry> port_git_trees =
            paths.get_builtin_ports_directory_trees(console_diagnostic_context).value_or_exit(VCPKG_LINE_INFO);

        if (!parsed_args.command_arguments.empty())
        {
            if (add_all)
            {
                msg::println_warning(msgAddVersionIgnoringOptionAll, msg::option = SwitchAll);
                add_all = false;
            }

            // note that this doesn't use Util::erase_remove_if to preserve processing the ports in supplied order
            // rather than alphabetical order
            std::set<std::string> seen_arguments;
            std::vector<GitLSTreeEntry> selected_git_trees;
            for (auto&& port_name : parsed_args.command_arguments)
            {
                if (!seen_arguments.emplace(port_name).second)
                {
                    continue;
                }

                auto it = Util::find_if(port_git_trees,
                                        [&](const GitLSTreeEntry& entry) { return entry.file_name == port_name; });

                if (it == port_git_trees.end())
                {
                    console_diagnostic_context.report_error(
                        msg::format(msgPortDoesNotExist, msg::package_name = port_name));
                    Checks::exit_fail(VCPKG_LINE_INFO);
                }

                selected_git_trees.push_back(std::move(*it));
            }

            swap(selected_git_trees, port_git_trees);
        }

        auto baseline_map = vcpkg::get_builtin_baseline(paths).value_or_exit(VCPKG_LINE_INFO);

        // Find ports with uncommitted changes
        for (auto&& port_git_tree_entry : port_git_trees)
        {
            auto& port_name = port_git_tree_entry.file_name;
            auto load_result = Paragraphs::try_load_builtin_port_required(fs, port_name, builtin_ports_directory);
            auto& maybe_scfl = load_result.maybe_scfl;
            auto scfl = maybe_scfl.get();
            if (!scfl)
            {
                msg::println(Color::error, maybe_scfl.error());
                if (!add_all)
                {
                    Checks::exit_fail(VCPKG_LINE_INFO);
                }

                continue;
            }

            if (!skip_formatting_check)
            {
                // check if manifest file is property formatted

                if (scfl->control_path.filename() == FileVcpkgDotJson)
                {
                    const auto json = serialize_manifest(*scfl->source_control_file);
                    const auto formatted_content = Json::stringify(json);
                    if (load_result.on_disk_contents != formatted_content)
                    {
                        std::string command_line = "vcpkg format-manifest ";
                        append_shell_escaped(command_line, scfl->control_path);
                        msg::println_error(
                            msg::format(msgAddVersionPortHasImproperFormat, msg::package_name = port_name)
                                .append_raw('\n')
                                .append(msgAddVersionFormatPortSuggestion, msg::command_line = command_line)
                                .append_raw('\n')
                                .append(msgSeeURL, msg::url = docs::format_manifest_command_url));
                        if (!add_all)
                        {
                            Checks::exit_fail(VCPKG_LINE_INFO);
                        }

                        continue;
                    }
                }
            }

            auto schemed_version = scfl->source_control_file->to_schemed_version();
            auto updated_versions_file = update_version_db_file(paths,
                                                                port_name,
                                                                schemed_version,
                                                                port_git_tree_entry.git_tree_sha,
                                                                overwrite_version,
                                                                verbose,
                                                                add_all,
                                                                skip_version_format_check);
            auto updated_baseline_file =
                update_baseline_version(fs, port_name, schemed_version.version, baseline_path, baseline_map, verbose);
            if (verbose && updated_versions_file == UpdateResult::NotUpdated &&
                updated_baseline_file == UpdateResult::NotUpdated)
            {
                msg::println(msgAddVersionNoFilesUpdatedForPort, msg::package_name = port_name);
            }
        }

        Checks::exit_success(VCPKG_LINE_INFO);
    }
} // namespace vcpkg
