
#include <vcpkg/base/checks.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/json.h>

#include <vcpkg/commands.add-version.h>
#include <vcpkg/configuration.h>
#include <vcpkg/paragraphs.h>
#include <vcpkg/portfileprovider.h>
#include <vcpkg/registries.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>
#include <vcpkg/versions.h>

#include <optional>

using namespace vcpkg;

namespace
{
    constexpr StringLiteral BASELINE = "baseline";
    constexpr StringLiteral VERSION_RELAXED = "version";
    constexpr StringLiteral VERSION_SEMVER = "version-semver";
    constexpr StringLiteral VERSION_DATE = "version-date";
    constexpr StringLiteral VERSION_STRING = "version-string";

    using VersionGitTree = std::pair<SchemedVersion, std::string>;

    void insert_version_to_json_object(Json::Object& obj, const VersionT& version, StringLiteral version_field)
    {
        obj.insert(version_field, Json::Value::string(version.text()));
        obj.insert("port-version", Json::Value::integer(version.port_version()));
    }

    void insert_schemed_version_to_json_object(Json::Object& obj, const SchemedVersion& version)
    {
        if (version.scheme == Versions::Scheme::Relaxed)
        {
            return insert_version_to_json_object(obj, version.versiont, VERSION_RELAXED);
        }

        if (version.scheme == Versions::Scheme::Semver)
        {
            return insert_version_to_json_object(obj, version.versiont, VERSION_SEMVER);
        }

        if (version.scheme == Versions::Scheme::Date)
        {
            return insert_version_to_json_object(obj, version.versiont, VERSION_DATE);
        }

        if (version.scheme == Versions::Scheme::String)
        {
            return insert_version_to_json_object(obj, version.versiont, VERSION_STRING);
        }
        Checks::unreachable(VCPKG_LINE_INFO);
    }

    static Json::Object serialize_baseline(const std::map<std::string, VersionT, std::less<>>& baseline)
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
                                    const std::map<std::string, VersionT, std::less<>>& baseline_map,
                                    const path& output_path)
    {
        auto new_path = output_path;
        new_path += vcpkg::u8path(".tmp");
        std::error_code ec;
        fs.create_directories(output_path.parent_path(), VCPKG_LINE_INFO);
        fs.write_contents(new_path,
                          Json::stringify(serialize_baseline(baseline_map), Json::JsonStyle::with_spaces(2)),
                          VCPKG_LINE_INFO);
        fs.rename(new_path, output_path, VCPKG_LINE_INFO);
    }

    static void write_versions_file(Filesystem& fs,
                                    const std::vector<VersionGitTree>& versions,
                                    const path& output_path)
    {
        auto new_path = output_path;
        new_path += vcpkg::u8path(".tmp");
        std::error_code ec;
        fs.create_directories(output_path.parent_path(), VCPKG_LINE_INFO);
        fs.write_contents(
            new_path, Json::stringify(serialize_versions(versions), Json::JsonStyle::with_spaces(2)), VCPKG_LINE_INFO);
        fs.rename(new_path, output_path, VCPKG_LINE_INFO);
    }

    static void update_baseline_version(const VcpkgPaths& paths,
                                        const std::string& port_name,
                                        const VersionT& version,
                                        const path& baseline_path,
                                        std::map<std::string, vcpkg::VersionT, std::less<>>& baseline_map,
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
                    vcpkg::printf(
                        Color::success, "Version `%s` is already in `%s`\n", version, vcpkg::u8string(baseline_path));
                }
                return;
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
            vcpkg::printf(
                Color::success, "Added version `%s` to `%s`.\n", version.to_string(), vcpkg::u8string(baseline_path));
        }
        return;
    }

    static void update_version_db_file(const VcpkgPaths& paths,
                                       const std::string& port_name,
                                       const SchemedVersion& version,
                                       const std::string& git_tree,
                                       const path& version_db_file_path,
                                       bool overwrite_version,
                                       bool print_success,
                                       bool keep_going)
    {
        auto& fs = paths.get_filesystem();
        if (!fs.exists(VCPKG_LINE_INFO, version_db_file_path))
        {
            std::vector<VersionGitTree> new_entry{{version, git_tree}};
            write_versions_file(fs, new_entry, version_db_file_path);
            if (print_success)
            {
                vcpkg::printf(Color::success,
                              "Added version `%s` to `%s` (new file).\n",
                              version.versiont,
                              vcpkg::u8string(version_db_file_path));
            }
            return;
        }

        auto maybe_versions = get_versions(paths.get_filesystem(), paths.current_registry_versions_dir(), port_name);
        if (auto versions = maybe_versions.get())
        {
            const auto& versions_end = versions->end();

            auto found_same_sha = std::find_if(
                versions->begin(), versions_end, [&](auto&& entry) -> bool { return entry.second == git_tree; });
            if (found_same_sha != versions_end)
            {
                if (found_same_sha->first.versiont == version.versiont)
                {
                    if (print_success)
                    {
                        vcpkg::printf(Color::success,
                                      "Version `%s` is already in `%s`\n",
                                      version.versiont,
                                      vcpkg::u8string(version_db_file_path));
                    }
                    return;
                }
                vcpkg::printf(Color::warning,
                              "Warning: Local port files SHA is the same as version `%s` in `%s`.\n"
                              "-- SHA: %s\n"
                              "-- Did you remember to commit your changes?\n"
                              "***No files were updated.***\n",
                              found_same_sha->first.versiont,
                              vcpkg::u8string(version_db_file_path),
                              git_tree);
                if (keep_going) return;
                Checks::exit_fail(VCPKG_LINE_INFO);
            }

            auto it = std::find_if(versions->begin(), versions_end, [&](auto&& entry) -> bool {
                return entry.first.versiont == version.versiont;
            });

            if (it != versions_end)
            {
                if (!overwrite_version)
                {
                    vcpkg::printf(Color::error,
                                  "Error: Local changes detected for %s but no changes to version or port version.\n"
                                  "-- Version: %s\n"
                                  "-- Old SHA: %s\n"
                                  "-- New SHA: %s\n"
                                  "-- Did you remember to update the version or port version?\n"
                                  "-- Pass `--overwrite-version` to bypass this check.\n"
                                  "***No files were updated.***\n",
                                  port_name,
                                  version.versiont,
                                  it->second,
                                  git_tree);
                    if (keep_going) return;
                    Checks::exit_fail(VCPKG_LINE_INFO);
                }

                it->first = version;
                it->second = git_tree;
            }
            else
            {
                versions->insert(versions->begin(), std::make_pair(version, git_tree));
            }

            write_versions_file(fs, *versions, version_db_file_path);
            if (print_success)
            {
                vcpkg::printf(Color::success,
                              "Added version `%s` to `%s`.\n",
                              version.versiont,
                              vcpkg::u8string(version_db_file_path));
            }
            return;
        }

        vcpkg::printf(Color::error,
                      "Error: Unable to parse versions file %s.\n%s\n",
                      vcpkg::u8string(version_db_file_path),
                      maybe_versions.error());
        Checks::exit_fail(VCPKG_LINE_INFO);
    }
}

namespace vcpkg::Commands::AddVersion
{
    static constexpr StringLiteral OPTION_ALL = "all";
    static constexpr StringLiteral OPTION_OVERWRITE_VERSION = "overwrite-version";
    static constexpr StringLiteral OPTION_SKIP_FORMATTING_CHECK = "skip-formatting-check";
    static constexpr StringLiteral OPTION_COMMIT = "commit";
    static constexpr StringLiteral OPTION_COMMIT_AMEND = "amend";
    static constexpr StringLiteral OPTION_COMMIT_MESSAGE = "commit-message";
    static constexpr StringLiteral OPTION_VERBOSE = "verbose";

    const CommandSwitch COMMAND_SWITCHES[] = {
        {OPTION_ALL, "Process versions for all ports."},
        {OPTION_OVERWRITE_VERSION, "Overwrite `git-tree` of an existing version."},
        {OPTION_SKIP_FORMATTING_CHECK, "Skips the formatting check of vcpkg.json files."},
        {OPTION_COMMIT, "Commits the results."},
        {OPTION_COMMIT_AMEND, "Amend the result to the last commit instead of creating a new one."},
        {OPTION_VERBOSE, "Print success messages instead of just errors."},
    };

    const CommandSetting COMMAND_SETTINGS[] = {
        {OPTION_COMMIT_MESSAGE, "The commit message when creating a new commit."},
    };

    const CommandStructure COMMAND_STRUCTURE{
        create_example_string(R"###(x-add-version <port name>)###"),
        0,
        1,
        {{COMMAND_SWITCHES}, {COMMAND_SETTINGS}, {}},
        nullptr,
    };

    void perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        auto parsed_args = args.parse_arguments(COMMAND_STRUCTURE);
        const bool add_all = Util::Sets::contains(parsed_args.switches, OPTION_ALL);
        const bool overwrite_version = Util::Sets::contains(parsed_args.switches, OPTION_OVERWRITE_VERSION);
        const bool skip_formatting_check = Util::Sets::contains(parsed_args.switches, OPTION_SKIP_FORMATTING_CHECK);
        const bool verbose = Util::Sets::contains(parsed_args.switches, OPTION_VERBOSE);
        const bool commit = Util::Sets::contains(parsed_args.switches, OPTION_COMMIT);
        const bool amend = Util::Sets::contains(parsed_args.switches, OPTION_COMMIT_AMEND);

        std::optional<std::string> commit_message;
        const auto iter_commit_message = parsed_args.settings.find(OPTION_COMMIT_MESSAGE);
        if (iter_commit_message != parsed_args.settings.end())
        {
            commit_message.emplace(iter_commit_message->second);
            if (commit_message.value().empty())
            {
                print2(Color::error, "Error: The specified commit message must be not empty.\n.");
                Checks::exit_fail(VCPKG_LINE_INFO);
            }
        }

        if ((amend || commit_message) && !commit)
        {
            printf(Color::warning,
                   "Warning: `--%s` or `--%s` was specified, assuming `--%s`\n",
                   OPTION_COMMIT_AMEND,
                   OPTION_COMMIT_MESSAGE,
                   OPTION_COMMIT);
        }

        auto& fs = paths.get_filesystem();
        auto baseline_path = paths.current_registry_versions_dir() / u8path("baseline.json");
        if (!fs.exists(VCPKG_LINE_INFO, baseline_path))
        {
            vcpkg::printf(Color::error, "Error: Couldn't find required file `%s`\n.", vcpkg::u8string(baseline_path));
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        std::vector<std::string> port_names;
        if (!args.command_arguments.empty())
        {
            if (add_all)
            {
                vcpkg::printf(Color::warning,
                              "Warning: Ignoring option `--%s` since a port name argument was provided.\n",
                              OPTION_ALL);
            }
            port_names.emplace_back(args.command_arguments[0]);
        }
        else
        {
            if (!add_all)
            {
                vcpkg::printf(Color::error,
                              "Error: Use option `--%s` to update version files for all ports at once.\n",
                              OPTION_ALL);
                Checks::exit_fail(VCPKG_LINE_INFO);
            }

            for (auto&& port_dir : stdfs::directory_iterator(paths.current_registry_ports_dir()))
            {
                port_names.emplace_back(vcpkg::u8string(port_dir.path().stem()));
            }
        }

        auto baseline_map = [&]() -> std::map<std::string, vcpkg::VersionT, std::less<>> {
            if (!fs.exists(VCPKG_LINE_INFO, baseline_path))
            {
                std::map<std::string, vcpkg::VersionT, std::less<>> ret;
                return ret;
            }
            auto maybe_baseline_map = vcpkg::get_baseline(paths, paths.current_registry_root);
            return maybe_baseline_map.value_or_exit(VCPKG_LINE_INFO);
        }();

        // Get tree-ish from local repository state.
        auto maybe_git_tree_map = paths.git_get_port_treeish_map(paths.current_registry_ports_dir());
        auto git_tree_map = maybe_git_tree_map.value_or_exit(VCPKG_LINE_INFO);
        std::vector<path> updated_files;

        for (auto&& port_name : port_names)
        {
            // Get version information of the local port
            auto maybe_scf = Paragraphs::try_load_port(fs, paths.current_registry_ports_dir() / u8path(port_name));
            if (!maybe_scf.has_value())
            {
                if (add_all) continue;
                vcpkg::printf(Color::error, "Error: Couldn't load port `%s`.", port_name);
                Checks::exit_fail(VCPKG_LINE_INFO);
            }

            const auto& scf = maybe_scf.value_or_exit(VCPKG_LINE_INFO);

            if (!skip_formatting_check)
            {
                // check if manifest file is property formatted
                const auto path_to_manifest =
                    paths.current_registry_ports_dir() / u8path(port_name) / u8path("vcpkg.json");
                if (fs.exists(path_to_manifest))
                {
                    const auto current_file_content = fs.read_contents(path_to_manifest, VCPKG_LINE_INFO);
                    const auto json = serialize_manifest(*scf);
                    const auto formatted_content = Json::stringify(json, {});
                    if (current_file_content != formatted_content)
                    {
                        vcpkg::printf(Color::error,
                                      "Error: The port `%s` is not properly formatted.\n"
                                      "Run `vcpkg format-manifest ports/%s/vcpkg.json` to format the file.\n"
                                      "Don't forget to commit the result!\n",
                                      port_name,
                                      port_name);
                        Checks::exit_fail(VCPKG_LINE_INFO);
                    }
                }
            }
            const auto& schemed_version = scf->to_schemed_version();

            auto git_tree_it = git_tree_map.find(port_name);
            if (git_tree_it == git_tree_map.end())
            {
                vcpkg::printf(Color::warning,
                              "Warning: No local Git SHA was found for port `%s`.\n"
                              "-- Did you remember to commit your changes?\n"
                              "***No files were updated.***\n",
                              port_name);
                if (add_all) continue;
                Checks::exit_fail(VCPKG_LINE_INFO);
            }
            const auto& git_tree = git_tree_it->second;

            auto port_versions_path = paths.current_registry_versions_dir() / u8path({port_name[0], '-'}) /
                                      u8path(Strings::concat(port_name, ".json"));
            updated_files.push_back(port_versions_path);
            update_version_db_file(
                paths, port_name, schemed_version, git_tree, port_versions_path, overwrite_version, verbose, add_all);
            update_baseline_version(paths, port_name, schemed_version.versiont, baseline_path, baseline_map, verbose);
        }
        if (!updated_files.empty()) updated_files.push_back(baseline_path);
        if (commit)
        {
            const auto result = paths.git_commit(paths.current_registry_dot_git_dir(),
                                                 std::move(updated_files),
                                                 commit_message.value_or(amend ? "" : "Add version files"),
                                                 amend);
            if (result.exit_code != 0)
            {
                printf(Color::error, "Error: Failed to commit the changes. The git output is: %s\n", result.output);
                Checks::exit_fail(VCPKG_LINE_INFO);
            }
        }
        Checks::exit_success(VCPKG_LINE_INFO);
    }

    void AddVersionCommand::perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths) const
    {
        AddVersion::perform_and_exit(args, paths);
    }
}
