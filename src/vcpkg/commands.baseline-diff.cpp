#include <vcpkg/base/fwd/message_sinks.h>

#include <vcpkg/base/checks.h>
#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/diagnostics.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/git.h>
#include <vcpkg/base/lineinfo.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/path.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/stringview.h>
#include <vcpkg/base/util.h>

#include <vcpkg/binarycaching.h>
#include <vcpkg/cmakevars.h>
#include <vcpkg/commands.baseline-diff.h>
#include <vcpkg/commands.install.h>
#include <vcpkg/configuration.h>
#include <vcpkg/dependencies.h>
#include <vcpkg/installeddatabase.h>
#include <vcpkg/installedpaths.h>
#include <vcpkg/paragraphs.h>
#include <vcpkg/portfileprovider.h>
#include <vcpkg/tools.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace vcpkg;

namespace
{
    constexpr CommandSwitch switches[] = {
        {SwitchXNoDefaultFeatures, msgHelpTxtOptManifestNoDefault},
    };

    static constexpr CommandMultiSetting multisettings[] = {
        {SwitchXFeature, msgHelpTxtOptManifestFeature},
    };

    bool print_lines(const msg::MessageT<>& header, std::vector<std::string>&& lines)
    {
        if (lines.empty())
        {
            return false;
        }

        Util::sort_unique_erase(lines);
        msg::print(msg::format(header).append_raw(":\n"));
        for (const auto& line : lines)
        {
            msg::write_unlocalized_text(Color::none, line);
            msg::write_unlocalized_text(Color::none, "\n");
        }
        msg::write_unlocalized_text(Color::none, "\n");
        return true;
    }

} // unnamed namespace

namespace vcpkg
{
    constexpr CommandMetadata CommandBaselineDiffMetadata{
        "x-baseline-diff",
        msgCmdBaselineDiffSynopsis,
        {msgCmdBaselineDiffExample1, "vcpkg x-baseline-diff 2026.02.27 2026.03.18"},
        "https://learn.microsoft.com/vcpkg/commands/baseline-diff",
        AutocompletePriority::Public,
        1,
        2,
        {switches, {}, multisettings},
        nullptr,
    };

    void command_baseline_diff_and_exit(const VcpkgCmdArguments& args,
                                        const VcpkgPaths& paths,
                                        Triplet default_triplet,
                                        Triplet host_triplet)
    {
        const auto* manifest = paths.get_manifest();
        if (manifest == nullptr)
        {
            Checks::msg_exit_with_message(VCPKG_LINE_INFO, msgMissingManifestFile);
        }
        auto options = args.parse_arguments(CommandBaselineDiffMetadata);

        auto& fs = paths.get_filesystem();
        InstallAndBuildDatabaseLock installed_lock{
            fs, paths.installed(), paths.buildtrees(), paths.packages(), args.wait_for_lock, args.ignore_lock_failures};
        auto var_provider_storage = CMakeVars::make_triplet_cmake_var_provider(paths, installed_lock);
        auto& var_provider = *var_provider_storage;

        auto configuration = paths.get_configuration();
        auto registry_set = configuration.instantiate_registry_set(paths);
        // is_default_builtin_files_registry() only matches BuiltinFilesRegistry (kind JsonIdBuiltinFiles),
        // but when the manifest carries a builtin-baseline, instantiate_registry_set synthesises a
        // RegistryConfig with kind "builtin" that creates BuiltinGitRegistry (kind JsonIdBuiltinGit) instead.
        // Both are the local vcpkg clone, so treat either builtin kind as "is default builtin".
        bool is_default_builtin =
            registry_set->default_registry() && (registry_set->default_registry()->kind() == JsonIdBuiltinFiles ||
                                                 registry_set->default_registry()->kind() == JsonIdBuiltinGit);
        auto manifest_scf =
            parse_manifest_scf_or_exit(*manifest, paths, registry_set->is_default_builtin_files_registry());

        // Determine the two baseline refs. If only one arg, use the manifest's builtin-baseline as the old one.
        StringView old_baseline_ref;
        StringView new_baseline_ref;
        if (options.command_arguments.size() == 2)
        {
            old_baseline_ref = options.command_arguments[0];
            new_baseline_ref = options.command_arguments[1];
        }
        else
        {
            // Prefer builtin-baseline from the manifest; fall back to the baseline field of
            // an explicitly configured default registry (e.g. a git registry in
            // vcpkg-configuration.json).
            const std::string* old_ref = manifest_scf->core_paragraph->builtin_baseline.get();
            if (!old_ref)
            {
                auto* default_reg = configuration.config.default_reg.get();
                if (default_reg)
                {
                    old_ref = default_reg->baseline.get();
                    if (!old_ref)
                    {
                        Checks::msg_exit_with_message(VCPKG_LINE_INFO, msgCmdBaselineDiffMissingRegistryBaseline);
                    }
                }
                else
                {
                    Checks::msg_exit_with_message(VCPKG_LINE_INFO, msgCmdBaselineDiffMissingBuiltinBaseline);
                }
            }
            old_baseline_ref = *old_ref;
            new_baseline_ref = options.command_arguments[0];
        }

        // Resolve tags and branch names to full SHAs when a local vcpkg clone is available.
        // This covers two cases:
        //   1. The builtin registry (no explicit default-registry configured, cloned mode).
        //   2. An explicit git registry pointing at https://github.com/microsoft/vcpkg while
        //      running from a local clone — the clone already has the same commits/tags.
        // In both cases we use the git dir belonging to the versions directory so that
        // --x-builtin-registry-versions-dir works correctly in tests.
        std::string old_baseline;
        std::string new_baseline;

        // Determine whether the configured default registry is the vcpkg GitHub repo.
        bool is_builtin_git_registry = false;
        if (!is_default_builtin)
        {
            if (auto* default_reg = configuration.config.default_reg.get())
            {
                auto* kind = default_reg->kind.get();
                auto* repo = default_reg->repo.get();
                is_builtin_git_registry = kind && *kind == JsonIdGit && repo && *repo == builtin_registry_git_url;
            }
        }

        // Resolve tags and branch names via the local vcpkg clone when available.
        // - Builtin registry (cloned mode): resolution is required; use paths.root.
        // - Git registry pointing at the vcpkg GitHub repo while in cloned mode: try
        //   paths.root opportunistically; fall back to raw strings if unavailable.
        // - Any other registry: no resolution, arguments must already be full SHAs.
        //
        // We use CurrentDirectory with paths.root (the vcpkg root) rather than
        // DotGitDir so that git can locate packed-refs and tag objects reliably.
        // Use the git root for ref resolution when a local vcpkg clone is available.
        // versions_dot_git_dir() walks up from the builtin registry versions dir to find the
        // repo root, so --x-builtin-registry-versions-dir is respected in tests.
        const bool need_resolve = is_default_builtin || is_builtin_git_registry;
        Path local_git_root; // non-empty when a usable local clone was found
        if (need_resolve)
        {
            auto git_dir_result = paths.versions_dot_git_dir();
            if (auto* p = git_dir_result.get(); p && !p->empty())
            {
                // parent_path() of the .git dir is the repo working directory
                local_git_root = p->parent_path();
            }
        }

        if (!local_git_root.empty())
        {
            const auto* git_exe = paths.get_tool_path(console_diagnostic_context, Tools::GIT);
            if (!git_exe) Checks::exit_fail(VCPKG_LINE_INFO);

            GitRepoLocator locator{GitRepoLocatorKind::CurrentDirectory, local_git_root};
            const auto resolve_baseline_ref = [&](StringView baseline) {
                if (is_git_sha(baseline))
                {
                    return baseline.to_string();
                }
                auto maybe_resolved = git_resolve_to_full_sha(console_diagnostic_context, *git_exe, locator, baseline);
                Checks::msg_check_exit(
                    VCPKG_LINE_INFO, maybe_resolved.has_value(), msgInvalidGitRef, msg::value = baseline);
                return *maybe_resolved.get();
            };
            old_baseline = resolve_baseline_ref(old_baseline_ref);
            new_baseline = resolve_baseline_ref(new_baseline_ref);
        }
        else // no local git clone available or not a resolvable registry
        {
            old_baseline = old_baseline_ref.to_string();
            new_baseline = new_baseline_ref.to_string();

            // Filesystem registries use named string keys as baselines, not commit SHAs.
            // For builtin and git registries the argument must be a full 40-character SHA.
            bool needs_sha_validation = true;
            if (auto* default_reg = configuration.config.default_reg.get())
            {
                auto* kind = default_reg->kind.get();
                needs_sha_validation = !kind || *kind != JsonIdFilesystem;
            }
            if (needs_sha_validation)
            {
                Checks::msg_check_exit(
                    VCPKG_LINE_INFO, is_git_sha(old_baseline), msgInvalidCommitId, msg::commit_sha = old_baseline);
                Checks::msg_check_exit(
                    VCPKG_LINE_INFO, is_git_sha(new_baseline), msgInvalidCommitId, msg::commit_sha = new_baseline);
            }
        }

        const std::string* baselines[2] = {&old_baseline, &new_baseline};

        const auto& manifest_core = *manifest_scf->core_paragraph;
        PackageSpec toplevel{manifest_core.name, default_triplet};
        auto features = get_manifest_features(options, manifest_core, var_provider, toplevel, host_triplet);

        auto dependencies = get_manifest_dependencies(*manifest_scf, features);

        ActionPlan plan[2];
        for (size_t i = 0; i < 2; ++i)
        {
            if (auto default_reg = configuration.config.default_reg.get())
            {
                default_reg->baseline = *baselines[i];
            }
            else
            {
                RegistryConfig synthesized_registry;
                synthesized_registry.kind = JsonIdBuiltin.to_string();
                synthesized_registry.baseline = *baselines[i];
                configuration.config.default_reg.emplace(synthesized_registry);
            }

            auto updated_registry_set = configuration.instantiate_registry_set(paths);
            auto verprovider = make_versioned_portfile_provider(*updated_registry_set);
            auto baseprovider = make_baseline_provider(*updated_registry_set);

            auto extended_overlay_port_directories = paths.overlay_ports;

            auto oprovider = make_manifest_provider(fs,
                                                    extended_overlay_port_directories,
                                                    manifest->path,
                                                    std::make_unique<SourceControlFile>(manifest_scf->clone()));
            PackagesDirAssigner packages_dir_assigner{paths.packages()};
            const CreateInstallPlanOptions create_options{
                nullptr, host_triplet, UnsupportedPortAction::Warn, UseHeadVersion::No, Editable::No};
            plan[i] = create_versioned_install_plan(*verprovider,
                                                    *baseprovider,
                                                    *oprovider,
                                                    var_provider,
                                                    dependencies,
                                                    manifest_core.overrides,
                                                    toplevel,
                                                    packages_dir_assigner,
                                                    create_options)
                          .value_or_exit(VCPKG_LINE_INFO);
        }

        std::map<PackageSpec, std::string> versions;
        for (const auto& action : plan[0].install_actions)
        {
            versions[action.spec] = action.version.to_string();
        }
        std::vector<std::string> user_requested;
        std::vector<std::string> transitive;
        for (const auto& action : plan[1].install_actions)
        {
            auto oldIter = versions.find(action.spec);
            auto newVersion = action.version.to_string();
            auto& vec = action.request_type == RequestType::USER_REQUESTED ? user_requested : transitive;
            if (oldIter == versions.end())
            {
                vec.push_back(fmt::format("{}: new: {}", action.spec.name(), newVersion));
            }
            else if (oldIter->second != newVersion)
            {
                vec.push_back(fmt::format("{}: {} -> {}", action.spec.name(), oldIter->second, newVersion));
            }
        }
        bool any_changes = false;
        any_changes |= print_lines(msgDirectDependencies, std::move(user_requested));
        any_changes |= print_lines(msgTransitiveDependencies, std::move(transitive));
        if (!any_changes)
        {
            msg::println(msgBaselineDiffNoChange);
        }

        Checks::exit_success(VCPKG_LINE_INFO);
    }
} // namespace vcpkg
