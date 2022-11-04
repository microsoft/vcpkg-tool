#include <vcpkg/base/checks.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/json.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.print.h>

#include <vcpkg/commands.civerifyversions.h>
#include <vcpkg/paragraphs.h>
#include <vcpkg/registries.h>
#include <vcpkg/sourceparagraph.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>
#include <vcpkg/versiondeserializers.h>

namespace
{
    using namespace vcpkg;

    std::string get_scheme_name(VersionScheme scheme)
    {
        switch (scheme)
        {
            case VersionScheme::Relaxed: return "version";
            case VersionScheme::Semver: return "version-semver";
            case VersionScheme::String: return "version-string";
            case VersionScheme::Date: return "version-date";
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }
    }
}

namespace vcpkg::Commands::CIVerifyVersions
{
    static constexpr StringLiteral OPTION_EXCLUDE = "exclude";
    static constexpr StringLiteral OPTION_VERBOSE = "verbose";
    static constexpr StringLiteral OPTION_VERIFY_GIT_TREES = "verify-git-trees";

    static constexpr CommandSwitch VERIFY_VERSIONS_SWITCHES[]{
        {OPTION_VERBOSE, "Print result for each port instead of just errors."},
        {OPTION_VERIFY_GIT_TREES, "Verify that each git tree object matches its declared version (this is very slow)"},
    };

    static constexpr CommandSetting VERIFY_VERSIONS_SETTINGS[] = {
        {OPTION_EXCLUDE, "Comma-separated list of ports to skip"},
    };

    const CommandStructure COMMAND_STRUCTURE{
        create_example_string(R"###(x-ci-verify-versions)###"),
        0,
        SIZE_MAX,
        {{VERIFY_VERSIONS_SWITCHES}, {VERIFY_VERSIONS_SETTINGS}, {}},
        nullptr,
    };

    static ExpectedS<std::string> verify_version_in_db(const VcpkgPaths& paths,
                                                       const std::map<std::string, Version, std::less<>> baseline,
                                                       StringView port_name,
                                                       const Path& port_path,
                                                       const Path& versions_file_path,
                                                       const std::string& local_git_tree,
                                                       bool verify_git_trees)
    {
        auto maybe_versions = vcpkg::get_builtin_versions(paths, port_name);
        if (!maybe_versions.has_value())
        {
            return {
                Strings::format("Error: While attempting to parse versions for port %s from file: %s\n"
                                "       Found the following error(s):\n%s",
                                port_name,
                                versions_file_path,
                                maybe_versions.error()),
                expected_right_tag,
            };
        }

        const auto& versions = maybe_versions.value_or_exit(VCPKG_LINE_INFO);
        if (versions.empty())
        {
            return {
                Strings::format("Error: While reading versions for port %s from file: %s\n"
                                "       File contains no versions.",
                                port_name,
                                versions_file_path),
                expected_right_tag,
            };
        }

        if (verify_git_trees)
        {
            for (auto&& version_entry : versions)
            {
                bool version_ok = false;
                for (StringView control_file : {"CONTROL", "vcpkg.json"})
                {
                    auto treeish = Strings::concat(version_entry.second, ':', control_file);
                    auto maybe_file = paths.git_show(Strings::concat(treeish), paths.root / ".git");
                    if (!maybe_file) continue;

                    const auto& file = maybe_file.value_or_exit(VCPKG_LINE_INFO);
                    auto maybe_scf =
                        Paragraphs::try_load_port_text(file, treeish, control_file == "vcpkg.json", stdout_sink);
                    if (!maybe_scf)
                    {
                        return {
                            Strings::format("Error: While reading versions for port %s from file: %s\n"
                                            "       While validating version: %s.\n"
                                            "       While trying to load port from: %s\n"
                                            "       Found the following error(s):\n%s",
                                            port_name,
                                            versions_file_path,
                                            version_entry.first.version,
                                            treeish,
                                            maybe_scf.error()->error),
                            expected_right_tag,
                        };
                    }

                    const auto& scf = maybe_scf.value_or_exit(VCPKG_LINE_INFO);
                    auto&& git_tree_version = scf->to_schemed_version();
                    if (version_entry.first.version != git_tree_version.version)
                    {
                        return {
                            Strings::format(
                                "Error: While reading versions for port %s from file: %s\n"
                                "       While validating version: %s.\n"
                                "       The version declared in file does not match checked-out version: %s\n"
                                "       Checked out Git SHA: %s",
                                port_name,
                                versions_file_path,
                                version_entry.first.version,
                                git_tree_version.version,
                                version_entry.second),
                            expected_right_tag,
                        };
                    }
                    version_ok = true;
                    break;
                }

                if (!version_ok)
                {
                    return {
                        Strings::format(
                            "Error: While reading versions for port %s from file: %s\n"
                            "       While validating version: %s.\n"
                            "       The checked-out object does not contain a CONTROL file or vcpkg.json file.\n"
                            "       Checked out Git SHA: %s",
                            port_name,
                            versions_file_path,
                            version_entry.first.version,
                            version_entry.second),
                        expected_right_tag,
                    };
                }
            }
        }

        auto maybe_scf = Paragraphs::try_load_port(paths.get_filesystem(), port_path);
        if (!maybe_scf)
        {
            return {
                Strings::format("Error: While attempting to load local port %s.\n"
                                "       Found the following error(s):\n%s",
                                port_name,
                                maybe_scf.error()->error),
                expected_right_tag,
            };
        }

        const auto local_port_version = maybe_scf.value_or_exit(VCPKG_LINE_INFO)->to_schemed_version();

        auto versions_end = versions.end();
        auto it =
            std::find_if(versions.begin(), versions_end, [&](const std::pair<SchemedVersion, std::string>& entry) {
                return entry.first.version == local_port_version.version;
            });
        if (it == versions_end)
        {
            return {
                Strings::format("Error: While reading versions for port %s from file: %s\n"
                                "       Version `%s` was not found in versions file.\n"
                                "       Run:\n\n"
                                "           vcpkg x-add-version %s\n\n"
                                "       to add the new port version.",
                                port_name,
                                versions_file_path,
                                local_port_version.version,
                                port_name),
                expected_right_tag,
            };
        }
        auto& entry = *it;

        if (entry.first.scheme != local_port_version.scheme)
        {
            return {
                Strings::format("Error: While reading versions for port %s from file: %s\n"
                                "       File declares version `%s` with scheme: `%s`.\n"
                                "       But local port declares the same version with a different scheme: `%s`.\n"
                                "       Version must be unique even between different schemes.\n"
                                "       Run:\n\n"
                                "           vcpkg x-add-version %s --overwrite-version\n\n"
                                "       to overwrite the declared version's scheme.",
                                port_name,
                                versions_file_path,
                                entry.first.version,
                                get_scheme_name(entry.first.scheme),
                                get_scheme_name(local_port_version.scheme),
                                port_name),
                expected_right_tag,
            };
        }

        if (local_git_tree != entry.second)
        {
            return {
                Strings::format("Error: While reading versions for port %s from file: %s\n"
                                "       File declares version `%s` with SHA: %s\n"
                                "       But local port with the same version has a different SHA: %s\n"
                                "       Please update the port's version fields and then run:\n\n"
                                "           vcpkg x-add-version %s\n"
                                "           git add versions\n"
                                "           git commit -m \"Update version database\"\n\n"
                                "       to add a new version.",
                                port_name,
                                versions_file_path,
                                entry.first.version,
                                entry.second,
                                local_git_tree,
                                port_name),
                expected_right_tag,
            };
        }

        auto maybe_baseline = baseline.find(port_name);
        if (maybe_baseline == baseline.end())
        {
            return {
                Strings::format("Error: While reading baseline version for port %s.\n"
                                "       Baseline version not found.\n"
                                "       Run:\n\n"
                                "           vcpkg x-add-version %s\n"
                                "           git add versions\n"
                                "           git commit -m \"Update version database\"\n\n"
                                "       to set version %s as the baseline version.",
                                port_name,
                                port_name,
                                local_port_version.version),
                expected_right_tag,
            };
        }

        auto&& baseline_version = maybe_baseline->second;
        if (baseline_version != entry.first.version)
        {
            return {
                Strings::format("Error: While reading baseline version for port %s.\n"
                                "       While validating latest version from file: %s\n"
                                "       Baseline file declares version: %s.\n"
                                "       But the latest version in version files is: %s.\n"
                                "       Run:\n\n"
                                "           vcpkg x-add-version %s\n"
                                "           git add versions\n"
                                "           git commit -m \"Update version database\"\n\n"
                                "       to update the baseline version.",
                                port_name,
                                versions_file_path,
                                baseline_version,
                                entry.first.version,
                                port_name),
                expected_right_tag,
            };
        }

        return {
            Strings::format("OK: %s\t%s -> %s\n", entry.second, port_name, entry.first.version),
            expected_left_tag,
        };
    }

    void perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        auto parsed_args = args.parse_arguments(COMMAND_STRUCTURE);

        bool verbose = Util::Sets::contains(parsed_args.switches, OPTION_VERBOSE);
        bool verify_git_trees = Util::Sets::contains(parsed_args.switches, OPTION_VERIFY_GIT_TREES);

        std::set<std::string> exclusion_set;
        auto settings = parsed_args.settings;
        auto it_exclusions = settings.find(OPTION_EXCLUDE);
        if (it_exclusions != settings.end())
        {
            auto exclusions = Strings::split(it_exclusions->second, ',');
            exclusion_set.insert(exclusions.begin(), exclusions.end());
        }

        auto maybe_port_git_tree_map = paths.git_get_local_port_treeish_map();
        if (!maybe_port_git_tree_map)
        {
            Checks::msg_exit_with_error(VCPKG_LINE_INFO,
                                        msg::format(msgFailedToObtainLocalPortGitSha)
                                            .append_raw('\n')
                                            .append_raw(maybe_port_git_tree_map.error()));
        }

        auto port_git_tree_map = maybe_port_git_tree_map.value_or_exit(VCPKG_LINE_INFO);

        // Baseline is required.
        auto baseline = get_builtin_baseline(paths).value_or_exit(VCPKG_LINE_INFO);
        auto& fs = paths.get_filesystem();
        std::set<std::string> errors;
        for (const auto& port_path : fs.get_directories_non_recursive(paths.builtin_ports_directory(), VCPKG_LINE_INFO))
        {
            auto port_name = port_path.stem();
            if (Util::Sets::contains(exclusion_set, port_name.to_string()))
            {
                if (verbose) msg::write_unlocalized_text_to_stdout(Color::error, fmt::format("SKIP: {}\n", port_name));
                continue;
            }
            auto git_tree_it = port_git_tree_map.find(port_name);
            if (git_tree_it == port_git_tree_map.end())
            {
                msg::write_unlocalized_text_to_stdout(Color::error, fmt::format("FAIL: {}\n", port_name));
                errors.emplace(Strings::format("Error: While validating port %s.\n"
                                               "       Missing Git SHA.\n"
                                               "       Run:\n\n"
                                               "           git add %s\n"
                                               "           git commit -m \"wip\"\n"
                                               "           vcpkg x-add-version %s\n"
                                               "           git add versions\n"
                                               "           git commit --amend -m \"[%s] Add new port\"\n\n"
                                               "       to commit the new port and create its version file.",
                                               port_name,
                                               port_path,
                                               port_name,
                                               port_name));
                continue;
            }
            auto git_tree = git_tree_it->second;

            auto control_path = port_path / "CONTROL";
            auto manifest_path = port_path / "vcpkg.json";
            auto manifest_exists = fs.exists(manifest_path, IgnoreErrors{});
            auto control_exists = fs.exists(control_path, IgnoreErrors{});

            if (manifest_exists && control_exists)
            {
                msg::write_unlocalized_text_to_stdout(Color::error, fmt::format("FAIL: {}\n", port_name));
                errors.emplace(
                    Strings::format("Error: While validating port %s.\n"
                                    "       Both a manifest file and a CONTROL file exist in port directory: %s",
                                    port_name,
                                    port_path));
                continue;
            }

            if (!manifest_exists && !control_exists)
            {
                msg::write_unlocalized_text_to_stdout(Color::error, fmt::format("FAIL: {}\n", port_name));
                errors.emplace(Strings::format("Error: While validating port %s.\n"
                                               "       No manifest file or CONTROL file exist in port directory: %s",
                                               port_name,
                                               port_path));
                continue;
            }

            const char prefix[] = {port_name[0], '-', '\0'};
            auto versions_file_path = paths.builtin_registry_versions / prefix / Strings::concat(port_name, ".json");
            if (!fs.exists(versions_file_path, IgnoreErrors{}))
            {
                msg::write_unlocalized_text_to_stdout(Color::error, fmt::format("FAIL: {}\n", port_name));
                errors.emplace(Strings::format("Error: While validating port %s.\n"
                                               "       Missing expected versions file at: %s\n"
                                               "       Run:\n\n"
                                               "           vcpkg x-add-version %s\n\n"
                                               "       to create the versions file.",
                                               port_name,
                                               versions_file_path,
                                               port_name));
                continue;
            }

            auto maybe_ok = verify_version_in_db(
                paths, baseline, port_name, port_path, versions_file_path, git_tree, verify_git_trees);

            if (!maybe_ok)
            {
                msg::write_unlocalized_text_to_stdout(Color::error, fmt::format("FAIL: {}\n", port_name));
                errors.emplace(maybe_ok.error());
                continue;
            }

            if (verbose) msg::write_unlocalized_text_to_stdout(Color::none, maybe_ok.value_or_exit(VCPKG_LINE_INFO));
        }

        if (!errors.empty())
        {
            auto message = msg::format(msgErrorsFound);
            for (auto&& error : errors)
            {
                message.append_raw('\n').append_indent().append_raw(error);
            }

            message.append_raw('\n').append(
                msgSuggestResolution, msg::command_name = "x-add-version", msg::option = "all");

            msg::println_error(message);

            Checks::exit_fail(VCPKG_LINE_INFO);
        }
        Checks::exit_success(VCPKG_LINE_INFO);
    }

    void CIVerifyVersionsCommand::perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths) const
    {
        CIVerifyVersions::perform_and_exit(args, paths);
    }
}
