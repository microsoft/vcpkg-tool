#include <vcpkg/base/fwd/message_sinks.h>

#include <vcpkg/base/checks.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/message_sinks.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.ci-verify-versions.h>
#include <vcpkg/paragraphs.h>
#include <vcpkg/registries.h>
#include <vcpkg/sourceparagraph.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

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

    struct CiVerifyVersionsDbEntry
    {
        SourceControlFileAndLocation scf;
        Path port_directory;
        std::vector<GitVersionDbEntry> entries;
        Path versions_file_path;
    };

    void add_parse_from_git_tree_failure_notes(LocalizedString& target,
                                               const std::string& port_name,
                                               const Path& versions_file_path,
                                               const SchemedVersion& version,
                                               StringView treeish)
    {
        target.append_raw('\n')
            .append(msgNoteMessage)
            .append(msgWhileLoadingPortFromGitTree, msg::commit_sha = treeish)
            .append_raw('\n')
            .append(msgNoteMessage)
            .append(msgWhileValidatingVersion, msg::version = version.version)
            .append_raw('\n')
            .append(msgNoteMessage)
            .append(msgWhileParsingVersionsForPort, msg::package_name = port_name, msg::path = versions_file_path);
    }

    void verify_git_tree(LocalizedString& errors,
                         MessageSink& success_sink,
                         const VcpkgPaths& paths,
                         const std::string& port_name,
                         const Path& versions_file_path,
                         const GitVersionDbEntry& version_entry)
    {
        bool success = true;
        auto treeish = version_entry.git_tree + ":CONTROL";
        auto maybe_maybe_loaded_manifest =
            paths.git_show(treeish, paths.root / ".git")
                .then([&](std::string&& file) -> ExpectedL<SourceControlFileAndLocation> {
                    auto maybe_scf = Paragraphs::try_load_control_file_text(file, treeish);
                    if (!maybe_scf)
                    {
                        add_parse_from_git_tree_failure_notes(
                            maybe_scf.error(), port_name, versions_file_path, version_entry.version, treeish);
                    }

                    return maybe_scf;
                });

        auto maybe_loaded_manifest = maybe_maybe_loaded_manifest.get();
        if (!maybe_loaded_manifest)
        {
            success = false;
            errors.append(std::move(maybe_maybe_loaded_manifest).error()).append_raw('\n');
            return;
        }

        if (!maybe_loaded_manifest->source_control_file)
        {
            treeish = version_entry.git_tree + ":vcpkg.json";
            paths.git_show(treeish, paths.root / ".git")
                .then([&](std::string&& file) -> ExpectedL<SourceControlFileAndLocation> {
                    auto maybe_scf = Paragraphs::try_load_port_manifest_text(file, treeish, stdout_sink);
                    if (!maybe_scf)
                    {
                        add_parse_from_git_tree_failure_notes(
                            maybe_scf.error(), port_name, versions_file_path, version_entry.version, treeish);
                    }

                    return maybe_scf;
                });

            maybe_loaded_manifest = maybe_maybe_loaded_manifest.get();
            if (!maybe_loaded_manifest)
            {
                success = false;
                errors.append(std::move(maybe_maybe_loaded_manifest).error()).append_raw('\n');
                return;
            }

            if (!maybe_loaded_manifest->source_control_file)
            {
                success = false;
                errors.append_raw(versions_file_path)
                    .append_raw(": ")
                    .append(msgErrorMessage)
                    .append(msgCheckedOutObjectMissingManifest)
                    .append_raw('\n')
                    .append(msgNoteMessage)
                    .append(msgCheckedOutGitSha, msg::commit_sha = treeish)
                    .append_raw('\n')
                    .append(msgNoteMessage)
                    .append(msgWhileValidatingVersion, msg::version = version_entry.version.version)
                    .append_raw('\n');
                return;
            }
        }

        auto& scf = *maybe_loaded_manifest->source_control_file;
        auto&& git_tree_version = scf.to_schemed_version();
        if (version_entry.version.version != git_tree_version.version)
        {
            success = false;
            errors.append_raw(versions_file_path)
                .append_raw(": ")
                .append(msgErrorMessage)
                .append(msgVersionInDeclarationDoesNotMatch, msg::version = git_tree_version.version)
                .append_raw('\n')
                .append(msgNoteMessage)
                .append(msgCheckedOutGitSha, msg::commit_sha = treeish)
                .append_raw('\n')
                .append(msgNoteMessage)
                .append(msgWhileValidatingVersion, msg::version = version_entry.version.version)
                .append_raw('\n');
        }

        if (version_entry.version.scheme != git_tree_version.scheme)
        {
            success = false;
            errors.append_raw(versions_file_path)
                .append_raw(": ")
                .append(msgErrorMessage)
                .append(msgVersionSchemeMismatch,
                        msg::version = version_entry.version.version,
                        msg::expected = get_scheme_name(version_entry.version.scheme),
                        msg::actual = get_scheme_name(git_tree_version.scheme),
                        msg::path = treeish,
                        msg::package_name = port_name)
                .append_raw('\n');
        }

        if (success)
        {
            success_sink.println(LocalizedString::from_raw(versions_file_path)
                                     .append_raw(": ")
                                     .append(msgVersionVerifiedOK2,
                                             msg::version_spec = VersionSpec{port_name, version_entry.version.version},
                                             msg::commit_sha = version_entry.git_tree));
        }
    }

    void verify_all_historical_git_trees(LocalizedString& errors,
                                         MessageSink& success_sink,
                                         const VcpkgPaths& paths,
                                         const std::string& port_name,
                                         const CiVerifyVersionsDbEntry& versions)
    {
        for (auto&& version_entry : versions.entries)
        {
            verify_git_tree(errors, success_sink, paths, port_name, versions.versions_file_path, version_entry);
        }
    }

    void verify_local_port_matches_version_database(LocalizedString& errors,
                                                    MessageSink& success_sink,
                                                    const std::string& port_name,
                                                    const CiVerifyVersionsDbEntry& db,
                                                    const std::string& local_git_tree)
    {
        bool success = true;
        if (db.entries.empty())
        {
            success = false;
            errors.append_raw(db.versions_file_path)
                .append_raw(": ")
                .append(msgErrorMessage)
                .append(msgInvalidNoVersions)
                .append_raw('\n');
        }

        const auto local_port_version = db.scf.source_control_file->to_schemed_version();

        auto versions_end = db.entries.end();
        auto it = std::find_if(db.entries.begin(), versions_end, [&](const GitVersionDbEntry& entry) {
            return entry.version.version == local_port_version.version;
        });

        if (it == versions_end)
        {
            success = false;
            errors.append_raw(db.scf.control_location)
                .append_raw(": ")
                .append(msgErrorMessage)
                .append(msgVersionNotFoundInVersionsFile2,
                        msg::version_spec = VersionSpec{port_name, local_port_version.version},
                        msg::package_name = port_name,
                        msg::path = db.versions_file_path)
                .append_raw('\n');
            return;
        }

        auto& version_entry = *it;
        if (version_entry.version.scheme != local_port_version.scheme)
        {
            success = false;
            // assume the port is correct, so report the error on the version database file
            errors.append_raw(db.versions_file_path)
                .append_raw(": ")
                .append(msgErrorMessage)
                .append(msgVersionSchemeMismatch,
                        msg::version = version_entry.version.version,
                        msg::expected = get_scheme_name(version_entry.version.scheme),
                        msg::actual = get_scheme_name(local_port_version.scheme),
                        msg::path = db.port_directory,
                        msg::package_name = port_name)
                .append_raw('\n');
        }

        if (local_git_tree != version_entry.git_tree)
        {
            success = false;
            errors.append_raw(db.versions_file_path)
                .append_raw(": ")
                .append(msgErrorMessage)
                .append(msgVersionShaMismatch1,
                        msg::version_spec = VersionSpec{port_name, local_port_version.version},
                        msg::expected = version_entry.git_tree,
                        msg::actual = local_git_tree,
                        msg::package_name = port_name,
                        msg::path = db.port_directory)
                .append_raw('\n')
                .append_indent()
                .append_raw("vcpkg x-add-version " + port_name + "\n")
                .append_indent()
                .append_raw("git add versions\n")
                .append_indent()
                .append(msgGitCommitUpdateVersionDatabase)
                .append_raw('\n')
                .append(msgVersionShaMismatch2, msg::version_spec = VersionSpec{port_name, local_port_version.version})
                .append_raw('\n')
                .append_indent()
                .append_raw("vcpkg x-add-version " + port_name + " --overwrite-version\n")
                .append_indent()
                .append_raw("git add versions\n")
                .append_indent()
                .append(msgGitCommitUpdateVersionDatabase)
                .append_raw('\n');
        }

        if (success)
        {
            success_sink.println(LocalizedString::from_raw(db.port_directory)
                                     .append_raw(": ")
                                     .append(msgVersionVerifiedOK2,
                                             msg::version_spec = VersionSpec{port_name, local_port_version.version},
                                             msg::commit_sha = version_entry.git_tree));
        }
    }

    void verify_local_port_matches_baseline(LocalizedString& errors,
                                            MessageSink& success_sink,
                                            const std::map<std::string, Version, std::less<>> baseline,
                                            const Path& baseline_path,
                                            const std::string& port_name,
                                            const CiVerifyVersionsDbEntry& db)
    {
        const auto local_port_version = db.scf.source_control_file->to_schemed_version();
        auto maybe_baseline = baseline.find(port_name);
        if (maybe_baseline == baseline.end())
        {
            errors.append_raw(db.scf.control_location)
                .append_raw(": ")
                .append(msgErrorMessage)
                .append(msgBaselineMissing, msg::package_name = port_name, msg::version = local_port_version.version)
                .append_raw('\n');
            return;
        }

        auto&& baseline_version = maybe_baseline->second;
        if (baseline_version == local_port_version.version)
        {
            success_sink.println(LocalizedString::from_raw(baseline_path)
                                     .append_raw(": ")
                                     .append(msgVersionBaselineMatch,
                                             msg::version_spec = VersionSpec{port_name, local_port_version.version}));
        }
        else
        {
            // assume the port is correct, so report the error on the baseline.json file
            errors.append_raw(baseline_path)
                .append_raw(": ")
                .append(msgErrorMessage)
                .append(msgVersionBaselineMismatch,
                        msg::expected = local_port_version.version,
                        msg::actual = baseline_version,
                        msg::package_name = port_name)
                .append_raw('\n');
        }
    }

    bool verify_dependency_and_version_constraint(
        const Dependency& dependency,
        const std::string* feature_name,
        LocalizedString& errors,
        const CiVerifyVersionsDbEntry& db,
        const std::map<std::string, CiVerifyVersionsDbEntry, std::less<>>& versions_database)
    {
        auto dependent_versions = versions_database.find(dependency.name);
        if (dependent_versions == versions_database.end())
        {
            errors.append_raw(db.scf.control_location)
                .append_raw(": ")
                .append(msgErrorMessage)
                .append(msgDependencyNotInVersionDatabase, msg::package_name = dependency.name)
                .append_raw('\n');
            if (feature_name)
            {
                errors.append(msgNoteMessage)
                    .append(msgDependencyInFeature, msg::feature = *feature_name)
                    .append_raw('\n');
            }

            return false;
        }

        auto maybe_minimum_version = dependency.constraint.try_get_minimum_version();
        auto minimum_version = maybe_minimum_version.get();
        if (minimum_version && Util::none_of(dependent_versions->second.entries, [=](const GitVersionDbEntry& entry) {
                return entry.version.version == *minimum_version;
            }))
        {
            errors.append_raw(db.scf.control_location)
                .append_raw(": ")
                .append(msgErrorMessage)
                .append(msgVersionConstraintNotInDatabase,
                        msg::package_name = dependency.name,
                        msg::version = *minimum_version,
                        msg::path = dependent_versions->second.versions_file_path)
                .append_raw('\n');
            if (feature_name)
            {
                errors.append(msgNoteMessage)
                    .append(msgDependencyInFeature, msg::feature = *feature_name)
                    .append_raw('\n');
            }

            return false;
        }

        return true;
    }

    void verify_all_dependencies_and_version_constraints(
        LocalizedString& errors,
        MessageSink& success_sink,
        const CiVerifyVersionsDbEntry& db,
        const std::map<std::string, CiVerifyVersionsDbEntry, std::less<>>& versions_database)
    {
        bool success = true;

        for (auto&& core_dependency : db.scf.source_control_file->core_paragraph->dependencies)
        {
            success &=
                verify_dependency_and_version_constraint(core_dependency, nullptr, errors, db, versions_database);
        }

        for (auto&& feature : db.scf.source_control_file->feature_paragraphs)
        {
            for (auto&& feature_dependency : feature->dependencies)
            {
                success &= verify_dependency_and_version_constraint(
                    feature_dependency, &feature->name, errors, db, versions_database);
            }
        }

        for (auto&& override_ : db.scf.source_control_file->core_paragraph->overrides)
        {
            auto override_versions = versions_database.find(override_.name);
            if (override_versions == versions_database.end())
            {
                success = false;
                errors.append_raw(db.scf.control_location)
                    .append_raw(": ")
                    .append(msgErrorMessage)
                    .append(msgVersionOverrideNotInVersionDatabase, msg::package_name = override_.name)
                    .append_raw('\n');
                continue;
            }

            if (Util::none_of(override_versions->second.entries, [&](const GitVersionDbEntry& entry) {
                    return entry.version.version == override_.version.version;
                }))
            {
                success = false;
                errors.append_raw(db.scf.control_location)
                    .append_raw(": ")
                    .append(msgErrorMessage)
                    .append(msgVersionOverrideVersionNotInVersionDatabase,
                            msg::package_name = override_.name,
                            msg::version = override_.version.version,
                            msg::path = override_versions->second.versions_file_path)
                    .append_raw('\n');
            }
        }

        if (success)
        {
            success_sink.println(
                LocalizedString::from_raw(db.scf.control_location).append_raw(": ").append(msgVersionConstraintOk));
        }
    }

    constexpr StringLiteral OPTION_VERBOSE = "verbose";
    constexpr StringLiteral OPTION_VERIFY_GIT_TREES = "verify-git-trees";

    constexpr CommandSwitch VERIFY_VERSIONS_SWITCHES[]{
        {OPTION_VERBOSE, msgCISettingsVerifyVersion},
        {OPTION_VERIFY_GIT_TREES, msgCISettingsVerifyGitTree},
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
        {VERIFY_VERSIONS_SWITCHES},
        nullptr,
    };

    void command_ci_verify_versions_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        auto parsed_args = args.parse_arguments(CommandCiVerifyVersionsMetadata);

        bool verbose = Util::Sets::contains(parsed_args.switches, OPTION_VERBOSE);
        bool verify_git_trees = Util::Sets::contains(parsed_args.switches, OPTION_VERIFY_GIT_TREES);

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
        LocalizedString errors;
        std::map<std::string, CiVerifyVersionsDbEntry, std::less<>> versions_database;
        for (auto&& port_path : fs.get_directories_non_recursive(paths.builtin_ports_directory(), VCPKG_LINE_INFO))
        {
            auto port_name = port_path.stem().to_string();
            auto maybe_loaded_port = Paragraphs::try_load_port_required(fs, port_name, port_path).maybe_scfl;
            auto loaded_port = maybe_loaded_port.get();
            if (!loaded_port)
            {
                errors.append(std::move(maybe_loaded_port).error()).append_raw('\n');
                continue;
            }

            auto load_versions_result = load_git_versions_file(fs, paths.builtin_registry_versions, port_name);
            auto maybe_versions_db = load_versions_result.entries.get();
            if (!maybe_versions_db)
            {
                errors.append(std::move(load_versions_result.entries).error()).append_raw('\n');
                continue;
            }

            auto versions_db = maybe_versions_db->get();
            if (!versions_db)
            {
                errors.append_raw(loaded_port->control_location)
                    .append_raw(": ")
                    .append(msgErrorMessage)
                    .append(msgVersionDatabaseFileMissing,
                            msg::package_name = port_name,
                            msg::path = load_versions_result.versions_file_path)
                    .append_raw('\n');
                continue;
            }

            versions_database.emplace(std::move(port_name),
                                      CiVerifyVersionsDbEntry{std::move(*loaded_port),
                                                              std::move(port_path),
                                                              std::move(*versions_db),
                                                              std::move(load_versions_result.versions_file_path)});
        }

        auto& success_sink = verbose ? stdout_sink : null_sink;
        for (const auto& port : versions_database)
        {
            const auto& port_name = port.first;
            const auto& db = port.second;
            auto git_tree_it = port_git_tree_map.find(port_name);
            if (git_tree_it == port_git_tree_map.end())
            {
                errors.append_raw(db.scf.control_location)
                    .append_raw(": ")
                    .append(msgErrorMessage)
                    .append(msgVersionShaMissing, msg::package_name = port_name, msg::path = db.port_directory)
                    .append_raw('\n');
            }
            else
            {
                verify_local_port_matches_version_database(errors, success_sink, port_name, db, git_tree_it->second);
            }

            verify_local_port_matches_baseline(
                errors, success_sink, baseline, paths.builtin_registry_versions / "baseline.json", port_name, db);

            if (verify_git_trees)
            {
                verify_all_historical_git_trees(errors, success_sink, paths, port_name, db);
            }

            verify_all_dependencies_and_version_constraints(errors, success_sink, db, versions_database);
        }

        if (!errors.empty())
        {
            errors.append_raw('\n');
            errors.append(msgSuggestResolution, msg::command_name = "x-add-version", msg::option = "all");
            Checks::msg_exit_with_message(VCPKG_LINE_INFO, errors);
        }

        Checks::exit_success(VCPKG_LINE_INFO);
    }
} // namespace vcpkg
