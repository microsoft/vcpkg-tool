#include <vcpkg/base/fwd/message_sinks.h>

#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/parse.h>
#include <vcpkg/base/path.h>
#include <vcpkg/base/strings.h>
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

    void check_for_valid_sha(StringView sha)
    {
        if (sha.size() < 7 || !Util::all_of(sha, ParserBase::is_hex_digit_lower))
        {
            Checks::msg_exit_with_error(VCPKG_LINE_INFO, msgInvalidCommitId, msg::commit_sha = sha);
        }
    }

} // unnamed namespace

namespace vcpkg
{
    constexpr CommandMetadata CommandBaselineDiffMetadata{
        "x-baseline-diff",
        msgCmdBaselineDiffSynopsis,
        {"vcpkg x-baseline-diff <old_commit_sha> <newer_commit_sha>",
         "vcpkg x-baseline-diff $(git rev-parse 2026.02.27) $(git rev-parse 2026.03.18)"},
        Undocumented,
        AutocompletePriority::Public,
        2,
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
        check_for_valid_sha(options.command_arguments.at(0));
        check_for_valid_sha(options.command_arguments.at(1));

        auto& fs = paths.get_filesystem();
        InstalledDatabaseLock installed_lock{fs, paths.installed(), args.wait_for_lock, args.ignore_lock_failures};
        auto var_provider_storage = CMakeVars::make_triplet_cmake_var_provider(paths, installed_lock);
        auto& var_provider = *var_provider_storage;

        auto configuration = paths.get_configuration();
        bool is_default_builtin = configuration.instantiate_registry_set(paths)->is_default_builtin_registry();
        auto manifest_scf = parse_manifest_scf_or_exit(*manifest, paths, is_default_builtin);

        const auto& manifest_core = *manifest_scf->core_paragraph;
        PackageSpec toplevel{manifest_core.name, default_triplet};
        auto features = get_manifest_features(options, manifest_core, var_provider, toplevel, host_triplet);

        auto dependencies = get_manifest_dependencies(*manifest_scf, features);

        ActionPlan plan[2];
        for (int i = 0; i < 2; ++i)
        {
            if (auto default_reg = configuration.config.default_reg.get())
            {
                default_reg->baseline = options.command_arguments.at(i);
            }
            else
            {
                RegistryConfig synthesized_registry;
                synthesized_registry.kind = JsonIdBuiltin.to_string();
                synthesized_registry.baseline = options.command_arguments.at(i);
                configuration.config.default_reg.emplace(synthesized_registry);
            }

            auto registry_set = configuration.instantiate_registry_set(paths);
            auto verprovider = make_versioned_portfile_provider(*registry_set);
            auto baseprovider = make_baseline_provider(*registry_set);

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
