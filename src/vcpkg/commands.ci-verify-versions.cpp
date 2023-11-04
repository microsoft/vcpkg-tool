#include <vcpkg/base/fwd/message_sinks.h>

#include <vcpkg/base/checks.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/json.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.ci-verify-versions.h>
#include <vcpkg/paragraphs.h>
#include <vcpkg/registries.h>
#include <vcpkg/sourceparagraph.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>
#include <vcpkg/versiondeserializers.h>

using namespace vcpkg;

namespace
{
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

    ExpectedL<LocalizedString> verify_version_in_db(const VcpkgPaths& paths,
                                                    const std::map<std::string, Version, std::less<>> baseline,
                                                    StringView port_name,
                                                    const Path& port_path,
                                                    const Path& versions_file_path,
                                                    const std::string& local_git_tree,
                                                    bool verify_git_trees)
    {
        auto maybe_maybe_versions = vcpkg::get_builtin_versions(paths, port_name);
        const auto maybe_versions = maybe_maybe_versions.get();
        if (!maybe_versions)
        {
            return {msg::format_error(
                        msgWhileParsingVersionsForPort, msg::package_name = port_name, msg::path = versions_file_path)
                        .append(std::move(maybe_maybe_versions).error()),
                    expected_right_tag};
        }

        const auto versions = maybe_versions->get();
        if (!versions || versions->empty())
        {
            return {msg::format_error(
                        msgWhileParsingVersionsForPort, msg::package_name = port_name, msg::path = versions_file_path)
                        .append_raw('\n')
                        .append(msgInvalidNoVersions),
                    expected_right_tag};
        }

        if (verify_git_trees)
        {
            for (auto&& version_entry : *versions)
            {
                bool version_ok = false;
                for (StringView control_file : {"CONTROL", "vcpkg.json"})
                {
                    auto treeish = Strings::concat(version_entry.git_tree, ':', control_file);
                    auto maybe_file = paths.git_show(Strings::concat(treeish),
                                                     paths.versions_dot_git_dir().value_or_exit(VCPKG_LINE_INFO));
                    if (!maybe_file) continue;

                    const auto& file = maybe_file.value_or_exit(VCPKG_LINE_INFO);
                    auto maybe_scf = control_file == "vcpkg.json"
                                         ? Paragraphs::try_load_port_manifest_text(file, treeish, stdout_sink)
                                         : Paragraphs::try_load_control_file_text(file, treeish);
                    auto scf = maybe_scf.get();
                    if (!scf)
                    {
                        return {msg::format_error(msgWhileParsingVersionsForPort,
                                                  msg::package_name = port_name,
                                                  msg::path = versions_file_path)
                                    .append_raw('\n')
                                    .append(msgWhileValidatingVersion, msg::version = version_entry.version.version)
                                    .append_raw('\n')
                                    .append(msgWhileLoadingPortFromGitTree, msg::commit_sha = treeish)
                                    .append_raw('\n')
                                    .append(maybe_scf.error()),
                                expected_right_tag};
                    }

                    auto&& git_tree_version = (**scf).to_schemed_version();
                    if (version_entry.version.version != git_tree_version.version)
                    {
                        return {
                            msg::format_error(msgWhileParsingVersionsForPort,
                                              msg::package_name = port_name,
                                              msg::path = versions_file_path)
                                .append_raw('\n')
                                .append(msgWhileValidatingVersion, msg::version = version_entry.version.version)
                                .append_raw('\n')
                                .append(msgVersionInDeclarationDoesNotMatch, msg::version = git_tree_version.version)
                                .append_raw('\n')
                                .append(msgCheckedOutGitSha, msg::commit_sha = version_entry.git_tree),
                            expected_right_tag};
                    }
                    version_ok = true;
                    break;
                }

                if (!version_ok)
                {
                    return {msg::format_error(msgWhileParsingVersionsForPort,
                                              msg::package_name = port_name,
                                              msg::path = versions_file_path)
                                .append_raw('\n')
                                .append(msgWhileValidatingVersion, msg::version = version_entry.version.version)
                                .append_raw('\n')
                                .append(msgCheckedOutObjectMissingManifest)
                                .append_raw('\n')
                                .append(msgCheckedOutGitSha, msg::commit_sha = version_entry.git_tree),
                            expected_right_tag};
                }
            }
        }

        auto maybe_scfl =
            Paragraphs::try_load_port_required(paths.get_filesystem(), port_name, PortLocation{port_path});
        auto scfl = maybe_scfl.get();
        if (!scfl)
        {
            return {msg::format_error(msgWhileLoadingLocalPort, msg::package_name = port_name)
                        .append_raw('\n')
                        .append(maybe_scfl.error()),
                    expected_right_tag};
        }

        const auto local_port_version = scfl->source_control_file->to_schemed_version();

        auto versions_end = versions->end();
        auto it = std::find_if(versions->begin(), versions_end, [&](const GitVersionDbEntry& entry) {
            return entry.version.version == local_port_version.version;
        });
        if (it == versions_end)
        {
            return {msg::format_error(
                        msgWhileParsingVersionsForPort, msg::package_name = port_name, msg::path = versions_file_path)
                        .append_raw('\n')
                        .append(msgVersionNotFoundInVersionsFile,
                                msg::version = local_port_version.version,
                                msg::package_name = port_name),
                    expected_right_tag};
        }
        auto& entry = *it;

        if (entry.version.scheme != local_port_version.scheme)
        {
            return {msg::format_error(
                        msgWhileParsingVersionsForPort, msg::package_name = port_name, msg::path = versions_file_path)
                        .append_raw('\n')
                        .append(msgVersionSchemeMismatch,
                                msg::version = entry.version.version,
                                msg::expected = get_scheme_name(entry.version.scheme),
                                msg::actual = get_scheme_name(local_port_version.scheme),
                                msg::path = port_path,
                                msg::package_name = port_name),
                    expected_right_tag};
        }

        if (local_git_tree != entry.git_tree)
        {
            return {msg::format_error(
                        msgWhileParsingVersionsForPort, msg::package_name = port_name, msg::path = versions_file_path)
                        .append_raw('\n')
                        .append(msgVersionShaMismatch,
                                msg::version = entry.version.version,
                                msg::expected = entry.git_tree,
                                msg::actual = local_git_tree,
                                msg::package_name = port_name),
                    expected_right_tag};
        }

        auto maybe_baseline = baseline.find(port_name);
        if (maybe_baseline == baseline.end())
        {
            return {msg::format_error(
                        msgWhileParsingVersionsForPort, msg::package_name = port_name, msg::path = versions_file_path)
                        .append_raw('\n')
                        .append(msgBaselineMissing,
                                msg::package_name = port_name,
                                msg::version = local_port_version.version),
                    expected_right_tag};
        }

        auto&& baseline_version = maybe_baseline->second;
        if (baseline_version != entry.version.version)
        {
            return {msg::format_error(
                        msgWhileParsingVersionsForPort, msg::package_name = port_name, msg::path = versions_file_path)
                        .append_raw('\n')
                        .append(msgVersionBaselineMismatch,
                                msg::expected = entry.version.version,
                                msg::actual = baseline_version,
                                msg::package_name = port_name),
                    expected_right_tag};
        }

        return {
            message_prefix().append(msgVersionVerifiedOK,
                                    msg::version_spec = Strings::concat(port_name, '@', entry.version.version),
                                    msg::git_tree_sha = entry.git_tree),
            expected_left_tag,
        };
    }

    constexpr StringLiteral OPTION_EXCLUDE = "exclude";
    constexpr StringLiteral OPTION_VERBOSE = "verbose";
    constexpr StringLiteral OPTION_VERIFY_GIT_TREES = "verify-git-trees";

    constexpr CommandSwitch VERIFY_VERSIONS_SWITCHES[]{
        {OPTION_VERBOSE, msgCISettingsVerifyVersion},
        {OPTION_VERIFY_GIT_TREES, msgCISettingsVerifyGitTree},
    };

    constexpr CommandSetting VERIFY_VERSIONS_SETTINGS[] = {
        {OPTION_EXCLUDE, msgCISettingsExclude},
    };
} // unnamed namespace

namespace vcpkg
{
    constexpr CommandMetadata CommandCiVerifyVersionsMetadata{
        "x-ci-verify-versions",
        msgCmdCiVerifyVersionsSynopsis,
        {"vcpkg x-ci-verify-versions"},
        Undocumented,
        AutocompletePriority::Internal,
        0,
        SIZE_MAX,
        {VERIFY_VERSIONS_SWITCHES, VERIFY_VERSIONS_SETTINGS},
        nullptr,
    };

    void command_ci_verify_versions_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        auto parsed_args = args.parse_arguments(CommandCiVerifyVersionsMetadata);

        bool verbose = Util::Sets::contains(parsed_args.switches, OPTION_VERBOSE);
        bool verify_git_trees = Util::Sets::contains(parsed_args.switches, OPTION_VERIFY_GIT_TREES);

        std::set<std::string> exclusion_set;
        auto& settings = parsed_args.settings;
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

        auto& port_git_tree_map = maybe_port_git_tree_map.value_or_exit(VCPKG_LINE_INFO);

        // Baseline is required.
        auto baseline = get_builtin_baseline(paths).value_or_exit(VCPKG_LINE_INFO);
        auto& fs = paths.get_filesystem();
        std::set<LocalizedString> errors;
        for (const auto& port_path : fs.get_directories_non_recursive(paths.builtin_ports_directory(), VCPKG_LINE_INFO))
        {
            auto port_name = port_path.stem();
            if (Util::Sets::contains(exclusion_set, port_name.to_string()))
            {
                if (verbose)
                {
                    msg::write_unlocalized_text_to_stdout(Color::error, fmt::format("SKIP: {}\n", port_name));
                }

                continue;
            }
            auto git_tree_it = port_git_tree_map.find(port_name);
            if (git_tree_it == port_git_tree_map.end())
            {
                msg::write_unlocalized_text_to_stdout(Color::error, fmt::format("FAIL: {}\n", port_name));
                errors.emplace(
                    msg::format_error(msgVersionShaMissing, msg::package_name = port_name, msg::path = port_path));
                continue;
            }

            auto& git_tree = git_tree_it->second;
            auto control_path = port_path / "CONTROL";
            auto manifest_path = port_path / "vcpkg.json";
            auto manifest_exists = fs.exists(manifest_path, IgnoreErrors{});
            auto control_exists = fs.exists(control_path, IgnoreErrors{});

            if (manifest_exists && control_exists)
            {
                msg::write_unlocalized_text_to_stdout(Color::error, fmt::format("FAIL: {}\n", port_name));
                errors.emplace(msg::format_error(msgControlAndManifestFilesPresent, msg::path = port_path));
                continue;
            }

            if (!manifest_exists && !control_exists)
            {
                msg::write_unlocalized_text_to_stdout(Color::error, fmt::format("FAIL: {}\n", port_name));
                errors.emplace(LocalizedString::from_raw(port_path)
                                   .append_raw(": ")
                                   .append_raw(ErrorPrefix)
                                   .append(msgPortMissingManifest2, msg::package_name = port_name));
                continue;
            }

            const char prefix[] = {port_name[0], '-', '\0'};
            auto versions_file_path = paths.builtin_registry_versions / prefix / Strings::concat(port_name, ".json");
            if (!fs.exists(versions_file_path, IgnoreErrors{}))
            {
                msg::write_unlocalized_text_to_stdout(Color::error, fmt::format("FAIL: {}\n", port_name));
                errors.emplace(msg::format_error(
                    msgVersionDatabaseFileMissing, msg::package_name = port_name, msg::path = versions_file_path));
                continue;
            }

            auto maybe_ok = verify_version_in_db(
                paths, baseline, port_name, port_path, versions_file_path, git_tree, verify_git_trees);
            if (auto ok = maybe_ok.get())
            {
                if (verbose)
                {
                    msg::println(*ok);
                }
            }
            else
            {
                msg::write_unlocalized_text_to_stdout(Color::error, fmt::format("FAIL: {}\n", port_name));
                errors.emplace(std::move(maybe_ok).error());
            }
        }

        if (!errors.empty())
        {
            auto message = msg::format(msgErrorsFound);
            for (auto&& error : errors)
            {
                message.append_raw('\n').append(error);
            }

            message.append_raw('\n').append(
                msgSuggestResolution, msg::command_name = "x-add-version", msg::option = "all");
            msg::println_error(message);
            Checks::exit_fail(VCPKG_LINE_INFO);
        }
        Checks::exit_success(VCPKG_LINE_INFO);
    }
} // namespace vcpkg
