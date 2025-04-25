#include <vcpkg/base/fwd/message_sinks.h>

#include <vcpkg/base/checks.h>
#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/git.h>
#include <vcpkg/base/message_sinks.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.ci-verify-versions.h>
#include <vcpkg/paragraphs.h>
#include <vcpkg/registries.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

using namespace vcpkg;

namespace
{
    StringLiteral get_scheme_name(VersionScheme scheme)
    {
        switch (scheme)
        {
            case VersionScheme::Relaxed: return JsonIdVersion;
            case VersionScheme::Semver: return JsonIdVersionSemver;
            case VersionScheme::String: return JsonIdVersionString;
            case VersionScheme::Date: return JsonIdVersionDate;
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }
    }

    bool verify_git_tree(MessageSink& errors_sink,
                         MessageSink& success_sink,
                         const VcpkgPaths& paths,
                         const std::string& port_name,
                         const Path& versions_file_path,
                         const GitVersionDbEntry& version_entry)
    {
        bool success = true;
        auto maybe_extracted_tree = paths.versions_dot_git_dir().then(
            [&](Path&& dot_git) { return paths.git_checkout_port(port_name, version_entry.git_tree, dot_git); });
        auto extracted_tree = maybe_extracted_tree.get();
        if (!extracted_tree)
        {
            success = false;
            errors_sink.println(Color::error,
                                LocalizedString::from_raw(versions_file_path)
                                    .append_raw(": ")
                                    .append(maybe_extracted_tree.error())
                                    .append_raw('\n')
                                    .append_raw(NotePrefix)
                                    .append(msgWhileValidatingVersion, msg::version = version_entry.version.version));
            return success;
        }

        auto load_result = Paragraphs::try_load_port_required(
            paths.get_filesystem(),
            port_name,
            PortLocation(*extracted_tree,
                         Paragraphs::builtin_git_tree_spdx_location(version_entry.git_tree),
                         PortSourceKind::Git));
        auto scfl = load_result.maybe_scfl.get();
        if (!scfl)
        {
            success = false;
            // This output is technically wrong as it prints both the versions file path and the temporary extracted
            // path, like this:
            //
            // C:\Dev\vcpkg\versions\a-\abseil.json:
            // C:\Dev\vcpkg\buildtrees\versioning_\versions\abseil\28fa609b06eec70bb06e61891e94b94f35f7d06e\vcpkg.json:
            // error: $.features: mismatched type: expected a set of features note: while validating version:
            // 2020-03-03#7
            //
            // However including both paths likely helps investigation and there isn't an easy way to replace only that
            // file path right now
            errors_sink.println(Color::error,
                                LocalizedString::from_raw(versions_file_path)
                                    .append_raw(": ")
                                    .append(load_result.maybe_scfl.error())
                                    .append_raw('\n')
                                    .append_raw(NotePrefix)
                                    .append(msgWhileValidatingVersion, msg::version = version_entry.version.version));
            return success;
        }

        auto&& git_tree_version = scfl->source_control_file->to_schemed_version();
        auto version_entry_spec = VersionSpec{port_name, version_entry.version.version};
        auto scfl_spec = scfl->source_control_file->to_version_spec();
        if (version_entry_spec != scfl_spec)
        {
            success = false;
            errors_sink.println(Color::error,
                                LocalizedString::from_raw(versions_file_path)
                                    .append_raw(": ")
                                    .append_raw(ErrorPrefix)
                                    .append(msgVersionInDeclarationDoesNotMatch,
                                            msg::git_tree_sha = version_entry.git_tree,
                                            msg::expected = version_entry_spec,
                                            msg::actual = scfl_spec));
        }

        if (version_entry.version.scheme != git_tree_version.scheme)
        {
            success = false;
            errors_sink.println(Color::error,
                                LocalizedString::from_raw(versions_file_path)
                                    .append_raw(": ")
                                    .append_raw(ErrorPrefix)
                                    .append(msgVersionSchemeMismatch1Old,
                                            msg::version = version_entry.version.version,
                                            msg::expected = get_scheme_name(version_entry.version.scheme),
                                            msg::actual = get_scheme_name(git_tree_version.scheme),
                                            msg::package_name = port_name,
                                            msg::git_tree_sha = version_entry.git_tree)
                                    .append_raw('\n')
                                    .append_raw(scfl->control_path)
                                    .append_raw(": ")
                                    .append_raw(NotePrefix)
                                    .append(msgPortDeclaredHere, msg::package_name = port_name)
                                    .append_raw('\n')
                                    .append_raw(NotePrefix)
                                    .append(msgVersionSchemeMismatch2));
        }

        if (success)
        {
            success_sink.println(LocalizedString::from_raw(versions_file_path)
                                     .append_raw(": ")
                                     .append_raw(MessagePrefix)
                                     .append(msgVersionVerifiedOK,
                                             msg::version_spec = VersionSpec{port_name, version_entry.version.version},
                                             msg::git_tree_sha = version_entry.git_tree));
        }

        return success;
    }

    bool verify_local_port_matches_version_database(MessageSink& errors_sink,
                                                    MessageSink& success_sink,
                                                    const std::string& port_name,
                                                    const SourceControlFileAndLocation& scfl,
                                                    FullGitVersionsDatabase& versions_database,
                                                    const std::string& local_git_tree)
    {
        bool success = true;
        const auto& versions_database_entry = versions_database.lookup(port_name);
        auto maybe_entries = versions_database_entry.entries.get();
        if (!maybe_entries)
        {
            // exists, but parse or file I/O error happened
            return success;
        }

        auto entries = maybe_entries->get();
        if (!entries)
        {
            success = false;
            errors_sink.println(Color::error,
                                LocalizedString::from_raw(scfl.control_path)
                                    .append_raw(": ")
                                    .append_raw(ErrorPrefix)
                                    .append(msgVersionDatabaseFileMissing)
                                    .append_raw('\n')
                                    .append_raw(versions_database_entry.versions_file_path)
                                    .append_raw(": ")
                                    .append_raw(NotePrefix)
                                    .append(msgVersionDatabaseFileMissing2)
                                    .append_raw('\n')
                                    .append_raw(NotePrefix)
                                    .append(msgVersionDatabaseFileMissing3,
                                            msg::command_line = fmt::format("vcpkg x-add-version {}", port_name)));
            return success;
        }

        const auto local_port_version = scfl.source_control_file->to_schemed_version();
        const auto local_version_spec = VersionSpec{port_name, local_port_version.version};

        auto versions_end = entries->end();
        auto it = std::find_if(entries->begin(), versions_end, [&](const GitVersionDbEntry& entry) {
            return entry.version.version == local_port_version.version;
        });

        if (it == versions_end)
        {
            success = false;
            errors_sink.println(Color::error,
                                LocalizedString::from_raw(scfl.control_path)
                                    .append_raw(": ")
                                    .append_raw(ErrorPrefix)
                                    .append(msgVersionNotFoundInVersionsFile2,
                                            msg::version_spec = VersionSpec{port_name, local_port_version.version})
                                    .append_raw('\n')
                                    .append_raw(versions_database_entry.versions_file_path)
                                    .append_raw(": ")
                                    .append_raw(NotePrefix)
                                    .append(msgVersionNotFoundInVersionsFile3)
                                    .append_raw('\n')
                                    .append_raw(NotePrefix)
                                    .append(msgVersionNotFoundInVersionsFile4,
                                            msg::command_line = fmt::format("vcpkg x-add-version {}", port_name)));
            return success;
        }

        auto& version_entry = *it;
        if (version_entry.version.scheme != local_port_version.scheme)
        {
            success = false;
            // assume the port is correct, so report the error on the version database file
            errors_sink.println(
                Color::error,
                LocalizedString::from_raw(versions_database_entry.versions_file_path)
                    .append_raw(": ")
                    .append_raw(ErrorPrefix)
                    .append(msgVersionSchemeMismatch1,
                            msg::version = version_entry.version.version,
                            msg::expected = get_scheme_name(version_entry.version.scheme),
                            msg::actual = get_scheme_name(local_port_version.scheme),
                            msg::package_name = port_name)
                    .append_raw('\n')
                    .append_raw(scfl.control_path)
                    .append_raw(": ")
                    .append_raw(NotePrefix)
                    .append(msgPortDeclaredHere, msg::package_name = port_name)
                    .append_raw('\n')
                    .append_raw(NotePrefix)
                    .append(msgVersionSchemeMismatch2)
                    .append_raw('\n')
                    .append_raw(NotePrefix)
                    .append(msgVersionOverwriteVersion, msg::version_spec = local_version_spec)
                    .append_raw(fmt::format("\nvcpkg x-add-version {} --overwrite-version", port_name)));
        }

        if (local_git_tree != version_entry.git_tree)
        {
            success = false;
            errors_sink.println(Color::error,
                                LocalizedString::from_raw(versions_database_entry.versions_file_path)
                                    .append_raw(": ")
                                    .append_raw(ErrorPrefix)
                                    .append(msgVersionShaMismatch1,
                                            msg::version_spec = local_version_spec,
                                            msg::git_tree_sha = version_entry.git_tree)
                                    .append_raw('\n')
                                    .append_raw(scfl.port_directory())
                                    .append_raw(": ")
                                    .append_raw(NotePrefix)
                                    .append(msgVersionShaMismatch2, msg::git_tree_sha = local_git_tree)
                                    .append_raw('\n')
                                    .append_raw(scfl.control_path)
                                    .append_raw(": ")
                                    .append_raw(NotePrefix)
                                    .append(msgVersionShaMismatch3, msg::version_spec = local_version_spec)
                                    .append_raw('\n')
                                    .append_indent()
                                    .append_raw(fmt::format("vcpkg x-add-version {}\n", port_name))
                                    .append_indent()
                                    .append_raw("git add versions\n")
                                    .append_indent()
                                    .append(msgGitCommitUpdateVersionDatabase)
                                    .append_raw('\n')
                                    .append_raw(NotePrefix)
                                    .append(msgVersionShaMismatch4, msg::version_spec = local_version_spec)
                                    .append_raw('\n')
                                    .append_indent()
                                    .append_raw(fmt::format("vcpkg x-add-version {} --overwrite-version\n", port_name))
                                    .append_indent()
                                    .append_raw("git add versions\n")
                                    .append_indent()
                                    .append(msgGitCommitUpdateVersionDatabase));
        }

        if (success)
        {
            success_sink.println(LocalizedString::from_raw(scfl.port_directory())
                                     .append_raw(": ")
                                     .append_raw(MessagePrefix)
                                     .append(msgVersionVerifiedOK,
                                             msg::version_spec = local_version_spec,
                                             msg::git_tree_sha = version_entry.git_tree));
        }

        return success;
    }

    bool verify_local_port_matches_baseline(MessageSink& errors_sink,
                                            MessageSink& success_sink,
                                            const std::map<std::string, Version, std::less<>> baseline,
                                            const Path& baseline_path,
                                            const std::string& port_name,
                                            const SourceControlFileAndLocation& scfl)
    {
        const auto local_port_version = scfl.source_control_file->to_schemed_version();
        auto maybe_baseline = baseline.find(port_name);
        if (maybe_baseline == baseline.end())
        {
            errors_sink.println(Color::error,
                                LocalizedString::from_raw(baseline_path)
                                    .append_raw(": ")
                                    .append_raw(ErrorPrefix)
                                    .append(msgBaselineMissing, msg::package_name = port_name)
                                    .append_raw('\n')
                                    .append_raw(scfl.control_path)
                                    .append_raw(": ")
                                    .append_raw(NotePrefix)
                                    .append(msgPortDeclaredHere, msg::package_name = port_name)
                                    .append_raw('\n')
                                    .append_raw(NotePrefix)
                                    .append(msgAddVersionInstructions, msg::package_name = port_name)
                                    .append_raw('\n')
                                    .append_indent()
                                    .append_raw(fmt::format("vcpkg x-add-version {}\n", port_name))
                                    .append_indent()
                                    .append_raw("git add versions\n")
                                    .append_indent()
                                    .append(msgGitCommitUpdateVersionDatabase));
            return false;
        }

        auto&& baseline_version = maybe_baseline->second;
        if (baseline_version == local_port_version.version)
        {
            success_sink.println(LocalizedString::from_raw(baseline_path)
                                     .append_raw(": ")
                                     .append_raw(MessagePrefix)
                                     .append(msgVersionBaselineMatch,
                                             msg::version_spec = VersionSpec{port_name, local_port_version.version}));
            return true;
        }

        // assume the port is correct, so report the error on the baseline.json file
        errors_sink.println(Color::error,
                            LocalizedString::from_raw(baseline_path)
                                .append_raw(": ")
                                .append_raw(ErrorPrefix)
                                .append(msgVersionBaselineMismatch,
                                        msg::expected = local_port_version.version,
                                        msg::actual = baseline_version,
                                        msg::package_name = port_name)
                                .append_raw('\n')
                                .append_raw(scfl.control_path)
                                .append_raw(": ")
                                .append_raw(NotePrefix)
                                .append(msgPortDeclaredHere, msg::package_name = port_name)
                                .append_raw('\n')
                                .append_raw(NotePrefix)
                                .append(msgAddVersionInstructions, msg::package_name = port_name)
                                .append_raw('\n')
                                .append_indent()
                                .append_raw(fmt::format("vcpkg x-add-version {}\n", port_name))
                                .append_indent()
                                .append_raw("git add versions\n")
                                .append_indent()
                                .append(msgGitCommitUpdateVersionDatabase));
        return false;
    }

    bool verify_dependency_and_version_constraint(const Dependency& dependency,
                                                  const std::string* feature_name,
                                                  MessageSink& errors_sink,
                                                  const SourceControlFileAndLocation& scfl,
                                                  FullGitVersionsDatabase& versions_database)
    {
        auto dependent_versions_db_entry = versions_database.lookup(dependency.name);
        auto maybe_dependent_entries = dependent_versions_db_entry.entries.get();
        if (!maybe_dependent_entries)
        {
            // versions database parse or I/O error
            return false;
        }

        auto dependent_entries = maybe_dependent_entries->get();
        if (!dependent_entries)
        {
            auto this_error = LocalizedString::from_raw(scfl.control_path)
                                  .append_raw(": ")
                                  .append_raw(ErrorPrefix)
                                  .append(msgDependencyNotInVersionDatabase, msg::package_name = dependency.name);
            if (feature_name)
            {
                this_error.append_raw('\n')
                    .append_raw(NotePrefix)
                    .append(msgDependencyInFeature, msg::feature = *feature_name);
            }

            errors_sink.println(Color::error, std::move(this_error));
            return false;
        }

        auto maybe_minimum_version = dependency.constraint.try_get_minimum_version();
        auto minimum_version = maybe_minimum_version.get();
        if (minimum_version && Util::none_of(*dependent_entries, [=](const GitVersionDbEntry& entry) {
                return entry.version.version == *minimum_version;
            }))
        {
            auto this_error = LocalizedString::from_raw(scfl.control_path)
                                  .append_raw(": ")
                                  .append_raw(ErrorPrefix)
                                  .append(msgVersionConstraintNotInDatabase1,
                                          msg::package_name = dependency.name,
                                          msg::version = *minimum_version)
                                  .append_raw('\n')
                                  .append_raw(dependent_versions_db_entry.versions_file_path)
                                  .append_raw(": ")
                                  .append_raw(NotePrefix)
                                  .append(msgVersionConstraintNotInDatabase2);
            if (feature_name)
            {
                this_error.append_raw('\n')
                    .append_raw(NotePrefix)
                    .append(msgDependencyInFeature, msg::feature = *feature_name);
            }

            errors_sink.println(Color::error, std::move(this_error));
            return false;
        }

        return true;
    }

    bool verify_all_dependencies_and_version_constraints(MessageSink& errors_sink,
                                                         MessageSink& success_sink,
                                                         const SourceControlFileAndLocation& scfl,
                                                         FullGitVersionsDatabase& versions_database)
    {
        bool success = true;

        for (auto&& core_dependency : scfl.source_control_file->core_paragraph->dependencies)
        {
            success &= verify_dependency_and_version_constraint(
                core_dependency, nullptr, errors_sink, scfl, versions_database);
        }

        for (auto&& feature : scfl.source_control_file->feature_paragraphs)
        {
            for (auto&& feature_dependency : feature->dependencies)
            {
                success &= verify_dependency_and_version_constraint(
                    feature_dependency, &feature->name, errors_sink, scfl, versions_database);
            }
        }

        for (auto&& override_ : scfl.source_control_file->core_paragraph->overrides)
        {
            auto override_versions_db_entry = versions_database.lookup(override_.name);
            auto maybe_override_entries = override_versions_db_entry.entries.get();
            if (!maybe_override_entries)
            {
                success = false;
                continue;
            }

            auto override_entries = maybe_override_entries->get();
            if (!override_entries)
            {
                success = false;
                errors_sink.println(
                    Color::error,
                    LocalizedString::from_raw(scfl.control_path)
                        .append_raw(": ")
                        .append_raw(ErrorPrefix)
                        .append(msgVersionOverrideNotInVersionDatabase, msg::package_name = override_.name));
                continue;
            }

            if (Util::none_of(*override_entries, [&](const GitVersionDbEntry& entry) {
                    return entry.version.version == override_.version;
                }))
            {
                success = false;
                errors_sink.println(Color::error,
                                    LocalizedString::from_raw(scfl.control_path)
                                        .append_raw(": ")
                                        .append_raw(ErrorPrefix)
                                        .append(msgVersionOverrideVersionNotInVersionDatabase1,
                                                msg::package_name = override_.name,
                                                msg::version = override_.version)
                                        .append_raw('\n')
                                        .append_raw(override_versions_db_entry.versions_file_path)
                                        .append_raw(": ")
                                        .append_raw(NotePrefix)
                                        .append(msgVersionOverrideVersionNotInVersionDatabase2));
            }
        }

        if (success)
        {
            success_sink.println(LocalizedString::from_raw(scfl.control_path)
                                     .append_raw(": ")
                                     .append_raw(MessagePrefix)
                                     .append(msgVersionConstraintOk));
        }

        return success;
    }

    constexpr CommandSwitch VERIFY_VERSIONS_SWITCHES[]{
        {SwitchVerbose, msgCISettingsVerifyVersion},
        {SwitchVerifyGitTrees, msgCISettingsVerifyGitTree},
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

        bool verbose = Util::Sets::contains(parsed_args.switches, SwitchVerbose);
        bool verify_git_trees = Util::Sets::contains(parsed_args.switches, SwitchVerifyGitTrees);

        auto port_git_trees =
            paths.get_builtin_ports_directory_trees(console_diagnostic_context).value_or_exit(VCPKG_LINE_INFO);
        auto& fs = paths.get_filesystem();
        auto versions_database =
            load_all_git_versions_files(fs, paths.builtin_registry_versions).value_or_exit(VCPKG_LINE_INFO);
        auto baseline = get_builtin_baseline(paths).value_or_exit(VCPKG_LINE_INFO);

        std::map<std::string, SourceControlFileAndLocation, std::less<>> local_ports;

        MessageSink& errors_sink = stdout_sink;
        bool success = true;

        auto& success_sink = verbose ? stdout_sink : null_sink;
        for (auto&& tree_entry : port_git_trees)
        {
            auto& port_name = tree_entry.file_name;
            auto maybe_loaded_port =
                Paragraphs::try_load_builtin_port_required(fs, port_name, paths.builtin_ports_directory()).maybe_scfl;
            auto loaded_port = maybe_loaded_port.get();
            if (loaded_port)
            {
                success &= verify_local_port_matches_version_database(
                    errors_sink, success_sink, port_name, *loaded_port, versions_database, tree_entry.git_tree_sha);
                success &= verify_local_port_matches_baseline(errors_sink,
                                                              success_sink,
                                                              baseline,
                                                              paths.builtin_registry_versions / "baseline.json",
                                                              port_name,
                                                              *loaded_port);

                success &= verify_all_dependencies_and_version_constraints(
                    errors_sink, success_sink, *loaded_port, versions_database);
            }
            else
            {
                errors_sink.println(Color::error, std::move(maybe_loaded_port).error());
                success = false;
            }
        }

        // We run version database checks at the end in case any of the above created new cache entries
        for (auto&& versions_cache_entry : versions_database.cache())
        {
            auto&& port_name = versions_cache_entry.first;
            auto maybe_entries = versions_cache_entry.second.entries.get();
            if (!maybe_entries)
            {
                errors_sink.println(Color::error, versions_cache_entry.second.entries.error());
                success = false;
                continue;
            }

            auto entries = maybe_entries->get();
            if (!entries)
            {
                // missing version database entry is OK; should have been reported as one of the other error
                // categories by one of the other checks
                continue;
            }

            if (verify_git_trees)
            {
                for (auto&& version_entry : *entries)
                {
                    success &= verify_git_tree(errors_sink,
                                               success_sink,
                                               paths,
                                               port_name,
                                               versions_cache_entry.second.versions_file_path,
                                               version_entry);
                }
            }
        }

        if (!success)
        {
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        Checks::exit_success(VCPKG_LINE_INFO);
    }
} // namespace vcpkg
