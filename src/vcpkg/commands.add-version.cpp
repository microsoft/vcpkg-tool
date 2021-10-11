
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

using namespace vcpkg;

namespace
{
    constexpr StringLiteral BASELINE = "baseline";
    constexpr StringLiteral VERSION_RELAXED = "version";
    constexpr StringLiteral VERSION_SEMVER = "version-semver";
    constexpr StringLiteral VERSION_DATE = "version-date";
    constexpr StringLiteral VERSION_STRING = "version-string";

    using VersionGitTree = std::pair<SchemedVersion, std::string>;

    DECLARE_AND_REGISTER_MESSAGE(VersionAlreadyInBaseline, "", "Version `{version}` is already in `{file}`\n",
        msg::version,
        msg::file);
    DECLARE_AND_REGISTER_MESSAGE(VersionAddedToBaseline, "", "Added version `{version}` to `{file}`.\n",
        msg::version,
        msg::file);
    DECLARE_AND_REGISTER_MESSAGE(NoLocalGitShaFoundForPort, "",
R"(Warning: No local Git SHA was found for port `{port}`.
-- Did you remember to commit your changes?
***No files were updated.***)",
        msg::port);
    DECLARE_AND_REGISTER_MESSAGE(PortNotProperlyFormatted, "",
R"(Error: The port `{port}` is not properly formatted.
Run `vcpkg format-manifest ports/{port}/vcpkg.json` to format the file.
Don't forget to commit the result!)",
                                      msg::port);
    DECLARE_AND_REGISTER_MESSAGE(CouldntLoadPort, "", "Error: Couldn't load port `{port}`.", msg::port);
    DECLARE_AND_REGISTER_MESSAGE(AddVersionUseOptionAll, "",
        "Error: Use option `--{option}` to update version files for all ports at once.",
        msg::option);
    DECLARE_AND_REGISTER_MESSAGE(AddVersionIgnoringOptionAll, "",
        "Warning: Ignoring option `--{option}` since a port name argument was provided.",
        msg::option);


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
                                    const Path& output_path)
    {
        auto new_path = output_path + ".tmp";
        std::error_code ec;
        fs.create_directories(output_path.parent_path(), VCPKG_LINE_INFO);
        fs.write_contents(new_path,
                          Json::stringify(serialize_baseline(baseline_map), Json::JsonStyle::with_spaces(2)),
                          VCPKG_LINE_INFO);
        fs.rename(new_path, output_path, VCPKG_LINE_INFO);
    }

    static void write_versions_file(Filesystem& fs,
                                    const std::vector<VersionGitTree>& versions,
                                    const Path& output_path)
    {
        auto new_path = output_path + ".tmp";
        std::error_code ec;
        fs.create_directories(output_path.parent_path(), VCPKG_LINE_INFO);
        fs.write_contents(
            new_path, Json::stringify(serialize_versions(versions), Json::JsonStyle::with_spaces(2)), VCPKG_LINE_INFO);
        fs.rename(new_path, output_path, VCPKG_LINE_INFO);
    }

    static void update_baseline_version(const VcpkgPaths& paths,
                                        const std::string& port_name,
                                        const VersionT& version,
                                        const Path& baseline_path,
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
                    msg::println(Color::Success, msgVersionAlreadyInBaseline, msg::version = version, msg::file = baseline_path);
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
            msg::println(Color::Success, msgVersionAddedToBaseline, msg::version = version, msg::file = baseline_path);
        }
        return;
    }

    static void update_version_db_file(const VcpkgPaths& paths,
                                       const std::string& port_name,
                                       const SchemedVersion& version,
                                       const std::string& git_tree,
                                       const Path& version_db_file_path,
                                       bool overwrite_version,
                                       bool print_success,
                                       bool keep_going)
    {
        auto& fs = paths.get_filesystem();
        if (!fs.exists(version_db_file_path, VCPKG_LINE_INFO))
        {
            std::vector<VersionGitTree> new_entry{{version, git_tree}};
            write_versions_file(fs, new_entry, version_db_file_path);
            if (print_success)
            {
                vcpkg::printf(
                    Color::Success, "Added version `%s` to `%s` (new file).\n", version.versiont, version_db_file_path);
            }
            return;
        }

        auto maybe_versions = get_builtin_versions(paths, port_name);
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
                        vcpkg::printf(Color::Success,
                                      "Version `%s` is already in `%s`\n",
                                      version.versiont,
                                      version_db_file_path);
                    }
                    return;
                }
                vcpkg::printf(Color::Warning,
                              "Warning: Local port files SHA is the same as version `%s` in `%s`.\n"
                              "-- SHA: %s\n"
                              "-- Did you remember to commit your changes?\n"
                              "***No files were updated.***\n",
                              found_same_sha->first.versiont,
                              version_db_file_path,
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
                    vcpkg::printf(Color::Error,
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
                vcpkg::printf(Color::Success, "Added version `%s` to `%s`.\n", version.versiont, version_db_file_path);
            }
            return;
        }

        vcpkg::printf(Color::Error,
                      "Error: Unable to parse versions file %s.\n%s\n",
                      version_db_file_path,
                      maybe_versions.error());
        Checks::exit_fail(VCPKG_LINE_INFO);
    }
}

namespace vcpkg::Commands::AddVersion
{
    static constexpr StringLiteral OPTION_ALL = "all";
    static constexpr StringLiteral OPTION_OVERWRITE_VERSION = "overwrite-version";
    static constexpr StringLiteral OPTION_SKIP_FORMATTING_CHECK = "skip-formatting-check";
    static constexpr StringLiteral OPTION_VERBOSE = "verbose";

    const CommandSwitch COMMAND_SWITCHES[] = {
        {OPTION_ALL, "Process versions for all ports."},
        {OPTION_OVERWRITE_VERSION, "Overwrite `git-tree` of an existing version."},
        {OPTION_SKIP_FORMATTING_CHECK, "Skips the formatting check of vcpkg.json files."},
        {OPTION_VERBOSE, "Print success messages instead of just errors."},
    };

    const CommandStructure COMMAND_STRUCTURE{
        create_example_string(R"###(x-add-version <port name>)###"),
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
        const bool verbose = Util::Sets::contains(parsed_args.switches, OPTION_VERBOSE);

        auto& fs = paths.get_filesystem();
        auto baseline_path = paths.builtin_registry_versions / "baseline.json";
        if (!fs.exists(baseline_path, VCPKG_LINE_INFO))
        {
            vcpkg::printf(Color::Error, "Error: Couldn't find required file `%s`\n.", baseline_path);
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        std::vector<std::string> port_names;
        if (!args.command_arguments.empty())
        {
            if (add_all)
            {
                msg::println(Color::Warning, msgAddVersionIgnoringOptionAll, msg::option = OPTION_ALL);
            }
            port_names.emplace_back(args.command_arguments[0]);
        }
        else
        {
            if (!add_all)
            {
                msg::println(Color::Error, msgAddVersionUseOptionAll, msg::option = OPTION_ALL);
                Checks::exit_fail(VCPKG_LINE_INFO);
            }

            for (auto&& port_dir : fs.get_files_non_recursive(paths.builtin_ports_directory(), VCPKG_LINE_INFO))
            {
                port_names.emplace_back(port_dir.stem().to_string());
            }
        }

        auto baseline_map = [&]() -> std::map<std::string, vcpkg::VersionT, std::less<>> {
            if (!fs.exists(baseline_path, VCPKG_LINE_INFO))
            {
                std::map<std::string, vcpkg::VersionT, std::less<>> ret;
                return ret;
            }
            auto maybe_baseline_map = vcpkg::get_builtin_baseline(paths);
            return maybe_baseline_map.value_or_exit(VCPKG_LINE_INFO);
        }();

        // Get tree-ish from local repository state.
        auto maybe_git_tree_map = paths.git_get_local_port_treeish_map();
        auto git_tree_map = maybe_git_tree_map.value_or_exit(VCPKG_LINE_INFO);

        for (auto&& port_name : port_names)
        {
            // Get version information of the local port
            auto maybe_scf = Paragraphs::try_load_port(fs, paths.builtin_ports_directory() / port_name);
            if (!maybe_scf.has_value())
            {
                if (add_all) continue;
                msg::println(Color::Error, msgCouldntLoadPort, msg::port = port_name);
                Checks::exit_fail(VCPKG_LINE_INFO);
            }

            const auto& scf = maybe_scf.value_or_exit(VCPKG_LINE_INFO);

            if (!skip_formatting_check)
            {
                // check if manifest file is property formatted
                const auto path_to_manifest = paths.builtin_ports_directory() / port_name / "vcpkg.json";
                if (fs.exists(path_to_manifest, IgnoreErrors{}))
                {
                    const auto current_file_content = fs.read_contents(path_to_manifest, VCPKG_LINE_INFO);
                    const auto json = serialize_manifest(*scf);
                    const auto formatted_content = Json::stringify(json, {});
                    if (current_file_content != formatted_content)
                    {
                        msg::println(Color::Error, msgPortNotProperlyFormatted, msg::port = port_name);
                        Checks::exit_fail(VCPKG_LINE_INFO);
                    }
                }
            }
            const auto& schemed_version = scf->to_schemed_version();

            auto git_tree_it = git_tree_map.find(port_name);
            if (git_tree_it == git_tree_map.end())
            {
                msg::println(Color::Warning, msgNoLocalGitShaFoundForPort, msg::port = port_name);
                if (add_all) continue;
                Checks::exit_fail(VCPKG_LINE_INFO);
            }
            const auto& git_tree = git_tree_it->second;

            char prefix[] = {port_name[0], '-', '\0'};
            auto port_versions_path = paths.builtin_registry_versions / prefix / Strings::concat(port_name, ".json");
            update_version_db_file(
                paths, port_name, schemed_version, git_tree, port_versions_path, overwrite_version, verbose, add_all);
            update_baseline_version(paths, port_name, schemed_version.versiont, baseline_path, baseline_map, verbose);
        }
        Checks::exit_success(VCPKG_LINE_INFO);
    }

    void AddVersionCommand::perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths) const
    {
        AddVersion::perform_and_exit(args, paths);
    }
}
