#include <vcpkg/base/util.h>

#include <vcpkg/commands.h>
#include <vcpkg/dependencies.h>
#include <vcpkg/help.h>
#include <vcpkg/input.h>
#include <vcpkg/installedpaths.h>
#include <vcpkg/paragraphs.h>
#include <vcpkg/portfileprovider.h>
#include <vcpkg/remove.h>
#include <vcpkg/update.h>
#include <vcpkg/vcpkglib.h>
#include <vcpkg/vcpkgpaths.h>

namespace vcpkg::Remove
{
    using Update::OutdatedPackage;

    REGISTER_MESSAGE(RemovingPackage);
    static void remove_package(Filesystem& fs,
                               const InstalledPaths& installed,
                               const PackageSpec& spec,
                               StatusParagraphs* status_db)
    {
        auto maybe_ipv = status_db->get_installed_package_view(spec);

        Checks::msg_check_exit(VCPKG_LINE_INFO, maybe_ipv.has_value(), msgPackageAlreadyRemoved, msg::spec = spec);

        auto&& ipv = maybe_ipv.value_or_exit(VCPKG_LINE_INFO);

        std::vector<StatusParagraph> spghs = ipv.all_status_paragraphs();

        for (auto&& spgh : spghs)
        {
            spgh.want = Want::PURGE;
            spgh.state = InstallState::HALF_INSTALLED;
            write_update(fs, installed, spgh);
        }

        auto maybe_lines = fs.read_lines(installed.listfile_path(ipv.core->package));
        if (auto lines = maybe_lines.get())
        {
            std::vector<Path> dirs_touched;
            for (auto&& suffix : *lines)
            {
                auto target = installed.root() / suffix;

                std::error_code ec;
                const auto status = fs.symlink_status(target, ec);
                if (ec)
                {
                    msg::println_error(format_filesystem_call_error(ec, "symlink_status", {target}));
                    continue;
                }

                if (vcpkg::is_directory(status))
                {
                    dirs_touched.push_back(target);
                }
                else if (vcpkg::is_regular_file(status) || vcpkg::is_symlink(status))
                {
                    fs.remove(target, ec);
                    if (ec)
                    {
                        msg::println_error(format_filesystem_call_error(ec, "remove", {target}));
                    }
                }
                else if (vcpkg::exists(status))
                {
                    Checks::unreachable(VCPKG_LINE_INFO, fmt::format("\"{}\": cannot handle file type", target));
                }
                else
                {
                    msg::println_warning(msgFileNotFound, msg::path = target);
                }
            }

            auto b = dirs_touched.rbegin();
            const auto e = dirs_touched.rend();
            for (; b != e; ++b)
            {
                if (fs.is_empty(*b, IgnoreErrors{}))
                {
                    std::error_code ec;
                    fs.remove(*b, ec);
                    if (ec)
                    {
                        msg::println_error(format_filesystem_call_error(ec, "remove", {*b}));
                    }
                }
            }

            fs.remove(installed.listfile_path(ipv.core->package), VCPKG_LINE_INFO);
        }

        for (auto&& spgh : spghs)
        {
            spgh.state = InstallState::NOT_INSTALLED;
            write_update(fs, installed, spgh);

            status_db->insert(std::make_unique<StatusParagraph>(std::move(spgh)));
        }
    }

    static void print_plan(const std::map<RemovePlanType, std::vector<const RemovePlanAction*>>& group_by_plan_type)
    {
        static constexpr std::array<RemovePlanType, 2> ORDER = {RemovePlanType::NOT_INSTALLED, RemovePlanType::REMOVE};

        for (const RemovePlanType plan_type : ORDER)
        {
            const auto it = group_by_plan_type.find(plan_type);
            if (it == group_by_plan_type.cend())
            {
                continue;
            }

            std::vector<const RemovePlanAction*> cont = it->second;
            std::sort(cont.begin(), cont.end(), &RemovePlanAction::compare_by_name);
            const std::string as_string = Strings::join("\n", cont, [](const RemovePlanAction* p) {
                return to_output_string(p->request_type, p->spec.to_string());
            });

            switch (plan_type)
            {
                case RemovePlanType::NOT_INSTALLED:
                    msg::println(msg::format(msgFollowingPackagesNotInstalled).append_raw(as_string));
                    continue;
                case RemovePlanType::REMOVE:
                    msg::println(msg::format(msgPackagesToRemove).append_raw('\n').append_raw(as_string));
                    continue;
                default: Checks::unreachable(VCPKG_LINE_INFO);
            }
        }
    }

    void perform_remove_plan_action(const VcpkgPaths& paths,
                                    const RemovePlanAction& action,
                                    const Purge purge,
                                    StatusParagraphs* status_db)
    {
        Filesystem& fs = paths.get_filesystem();

        const std::string display_name = action.spec.to_string();

        switch (action.plan_type)
        {
            case RemovePlanType::NOT_INSTALLED: break;
            case RemovePlanType::REMOVE: remove_package(fs, paths.installed(), action.spec, status_db); break;
            case RemovePlanType::UNKNOWN:
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }

        if (purge == Purge::YES)
        {
            fs.remove_all(paths.packages() / action.spec.dir(), VCPKG_LINE_INFO);
        }
    }

    static constexpr StringLiteral OPTION_PURGE = "purge";
    static constexpr StringLiteral OPTION_NO_PURGE = "no-purge";
    static constexpr StringLiteral OPTION_RECURSE = "recurse";
    static constexpr StringLiteral OPTION_DRY_RUN = "dry-run";
    static constexpr StringLiteral OPTION_OUTDATED = "outdated";

    static constexpr std::array<CommandSwitch, 5> SWITCHES = {{
        {OPTION_PURGE, nullptr},
        {OPTION_NO_PURGE, nullptr},
        {OPTION_RECURSE, []() { return msg::format(msgCmdRemoveOptRecurse); }},
        {OPTION_DRY_RUN, []() { return msg::format(msgCmdRemoveOptDryRun); }},
        {OPTION_OUTDATED, []() { return msg::format(msgCmdRemoveOptOutdated); }},
    }};

    static std::vector<std::string> valid_arguments(const VcpkgPaths& paths)
    {
        const StatusParagraphs status_db = database_load_check(paths.get_filesystem(), paths.installed());
        auto installed_packages = get_installed_ports(status_db);

        return Util::fmap(installed_packages, [](auto&& pgh) -> std::string { return pgh.spec().to_string(); });
    }

    const CommandStructure COMMAND_STRUCTURE = {
        create_example_string("remove zlib zlib:x64-windows curl boost"),
        0,
        SIZE_MAX,
        {SWITCHES, {}},
        &valid_arguments,
    };

    static void perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths, Triplet default_triplet)
    {
        if (paths.manifest_mode_enabled())
        {
            Checks::msg_exit_maybe_upgrade(VCPKG_LINE_INFO, msgRemoveDependencies);
        }
        const ParsedArguments options = args.parse_arguments(COMMAND_STRUCTURE);

        StatusParagraphs status_db = database_load_check(paths.get_filesystem(), paths.installed());
        std::vector<PackageSpec> specs;
        if (Util::Sets::contains(options.switches, OPTION_OUTDATED))
        {
            if (args.command_arguments.size() != 0)
            {
                msg::println_error(msgInvalidOptionForRemove);
                Checks::exit_fail(VCPKG_LINE_INFO);
            }

            // Load ports from ports dirs
            auto& fs = paths.get_filesystem();
            auto registry_set = paths.make_registry_set();
            PathsPortFileProvider provider(
                fs, *registry_set, make_overlay_provider(fs, paths.original_cwd, paths.overlay_ports));

            specs = Util::fmap(Update::find_outdated_packages(provider, status_db),
                               [](auto&& outdated) { return outdated.spec; });

            if (specs.empty())
            {
                msg::println(Color::success, msgNoOutdatedPackages);
                Checks::exit_success(VCPKG_LINE_INFO);
            }
        }
        else
        {
            if (args.command_arguments.size() < 1)
            {
                msg::println_error(msgInvalidOptionForRemove);
                Checks::exit_fail(VCPKG_LINE_INFO);
            }
            specs = Util::fmap(args.command_arguments, [&](auto&& arg) {
                return check_and_get_package_spec(
                    std::string(arg), default_triplet, COMMAND_STRUCTURE.example_text, paths);
            });
            print_default_triplet_warning(args, args.command_arguments);
        }

        const bool no_purge = Util::Sets::contains(options.switches, OPTION_NO_PURGE);
        if (no_purge && Util::Sets::contains(options.switches, OPTION_PURGE))
        {
            msg::println_error(msgMutuallyExclusiveOption, msg::value = "no-purge", msg::option = "purge");
            msg::write_unlocalized_text_to_stdout(Color::none, COMMAND_STRUCTURE.example_text);
            Checks::exit_fail(VCPKG_LINE_INFO);
        }
        const Purge purge = no_purge ? Purge::NO : Purge::YES;

        const bool is_recursive = Util::Sets::contains(options.switches, OPTION_RECURSE);
        const bool dry_run = Util::Sets::contains(options.switches, OPTION_DRY_RUN);

        const std::vector<RemovePlanAction> remove_plan = create_remove_plan(specs, status_db);

        if (remove_plan.empty())
        {
            Checks::unreachable(VCPKG_LINE_INFO, "Remove plan cannot be empty");
        }

        std::map<RemovePlanType, std::vector<const RemovePlanAction*>> group_by_plan_type;
        Util::group_by(remove_plan, &group_by_plan_type, [](const RemovePlanAction& p) { return p.plan_type; });
        print_plan(group_by_plan_type);

        const bool has_non_user_requested_packages =
            Util::find_if(remove_plan, [](const RemovePlanAction& package) -> bool {
                return package.request_type != RequestType::USER_REQUESTED;
            }) != remove_plan.cend();

        if (has_non_user_requested_packages)
        {
            msg::println_warning(msgAdditionalPackagesToRemove);

            if (!is_recursive)
            {
                msg::println_warning(msgAddRecurseOption);
                Checks::exit_fail(VCPKG_LINE_INFO);
            }
        }

        for (const auto& action : remove_plan)
        {
            if (action.plan_type == RemovePlanType::NOT_INSTALLED && action.request_type == RequestType::USER_REQUESTED)
            {
                // The user requested removing a package that was not installed. If the port is installed for another
                // triplet, warn the user that they may have meant that other package.
                const auto& action_spec = action.spec;
                const auto& action_package_name = action_spec.name();
                for (const auto& package : status_db)
                {
                    if (package->is_installed() && !package->package.is_feature() &&
                        package->package.spec.name() == action_package_name)
                    {
                        msg::println_warning(msgRemovePackageConflict,
                                             msg::package_name = action_package_name,
                                             msg::spec = action.spec,
                                             msg::triplet = package->package.spec.triplet());
                    }
                }
            }
        }

        if (dry_run)
        {
            Checks::exit_success(VCPKG_LINE_INFO);
        }

        // note that we try to "remove" things that aren't installed to trigger purge actions
        for (std::size_t idx = 0; idx < remove_plan.size(); ++idx)
        {
            const RemovePlanAction& action = remove_plan[idx];
            msg::println(msgRemovingPackage,
                         msg::action_index = idx + 1,
                         msg::count = remove_plan.size(),
                         msg::spec = action.spec);
            perform_remove_plan_action(paths, action, purge, &status_db);
        }

        Checks::exit_success(VCPKG_LINE_INFO);
    }

    void RemoveCommand::perform_and_exit(const VcpkgCmdArguments& args,
                                         const VcpkgPaths& paths,
                                         Triplet default_triplet,
                                         Triplet /*host_triplet*/) const
    {
        Remove::perform_and_exit(args, paths, default_triplet);
    }
}
