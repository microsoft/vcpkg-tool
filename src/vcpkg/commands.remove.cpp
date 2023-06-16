#include <vcpkg/base/strings.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.h>
#include <vcpkg/commands.help.h>
#include <vcpkg/commands.remove.h>
#include <vcpkg/commands.update.h>
#include <vcpkg/dependencies.h>
#include <vcpkg/input.h>
#include <vcpkg/installedpaths.h>
#include <vcpkg/paragraphs.h>
#include <vcpkg/portfileprovider.h>
#include <vcpkg/registries.h>
#include <vcpkg/vcpkglib.h>
#include <vcpkg/vcpkgpaths.h>

namespace vcpkg::Remove
{
    using Update::OutdatedPackage;

    void remove_package(const Filesystem& fs,
                        const InstalledPaths& installed,
                        const PackageSpec& spec,
                        StatusParagraphs& status_db)
    {
        auto maybe_ipv = status_db.get_installed_package_view(spec);

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

            status_db.insert(std::make_unique<StatusParagraph>(std::move(spgh)));
        }
    }

    constexpr struct OpAddressOf
    {
        template<class T>
        T* operator()(T& t) const
        {
            return &t;
        }
    } op_address_of;

    static void print_plan(const RemovePlan& plan)
    {
        if (!plan.not_installed.empty())
        {
            std::vector<const NotInstalledAction*> not_installed = Util::fmap(plan.not_installed, op_address_of);
            Util::sort(not_installed, &BasicAction::compare_by_name);
            LocalizedString msg;
            msg.append(msgFollowingPackagesNotInstalled).append_raw("\n");
            for (auto p : not_installed)
            {
                msg.append_raw(request_type_indent(RequestType::USER_REQUESTED)).append_raw(p->spec).append_raw("\n");
            }
            msg::print(msg);
        }
        if (!plan.remove.empty())
        {
            std::vector<const RemovePlanAction*> remove = Util::fmap(plan.remove, op_address_of);
            Util::sort(remove, &BasicAction::compare_by_name);
            LocalizedString msg;
            msg.append(msgPackagesToRemove).append_raw("\n");
            for (auto p : remove)
            {
                msg.append_raw(request_type_indent(p->request_type)).append_raw(p->spec).append_raw("\n");
            }
            msg::print(msg);
        }
    }

    static constexpr StringLiteral OPTION_PURGE = "purge";
    static constexpr StringLiteral OPTION_RECURSE = "recurse";
    static constexpr StringLiteral OPTION_DRY_RUN = "dry-run";
    static constexpr StringLiteral OPTION_OUTDATED = "outdated";

    static constexpr std::array<CommandSwitch, 4> SWITCHES = {{
        {OPTION_PURGE, nullptr},
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
        [] { return create_example_string("remove zlib zlib:x64-windows curl boost"); },
        0,
        SIZE_MAX,
        {SWITCHES, {}},
        &valid_arguments,
    };

    void perform_and_exit(const VcpkgCmdArguments& args,
                          const VcpkgPaths& paths,
                          Triplet default_triplet,
                          Triplet host_triplet)
    {
        (void)host_triplet;
        if (paths.manifest_mode_enabled())
        {
            Checks::msg_exit_maybe_upgrade(VCPKG_LINE_INFO, msgRemoveDependencies);
        }
        const ParsedArguments options = args.parse_arguments(COMMAND_STRUCTURE);

        StatusParagraphs status_db = database_load_check(paths.get_filesystem(), paths.installed());
        std::vector<PackageSpec> specs;
        if (Util::Sets::contains(options.switches, OPTION_OUTDATED))
        {
            if (options.command_arguments.size() != 0)
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
            if (options.command_arguments.size() < 1)
            {
                msg::println_error(msgInvalidOptionForRemove);
                Checks::exit_fail(VCPKG_LINE_INFO);
            }

            bool default_triplet_used = false;
            specs = Util::fmap(options.command_arguments, [&](auto&& arg) {
                return check_and_get_package_spec(std::string(arg),
                                                  default_triplet,
                                                  default_triplet_used,
                                                  COMMAND_STRUCTURE.get_example_text(),
                                                  paths);
            });

            if (default_triplet_used)
            {
                print_default_triplet_warning(args);
            }
        }

        const Purge purge = Util::Sets::contains(options.switches, OPTION_PURGE) ? Purge::YES : Purge::NO;
        const bool is_recursive = Util::Sets::contains(options.switches, OPTION_RECURSE);
        const bool dry_run = Util::Sets::contains(options.switches, OPTION_DRY_RUN);

        const auto plan = create_remove_plan(specs, status_db);

        if (plan.empty())
        {
            Checks::unreachable(VCPKG_LINE_INFO, "Remove plan cannot be empty");
        }

        print_plan(plan);

        if (plan.has_non_user_requested())
        {
            msg::println_warning(msgAdditionalPackagesToRemove);

            if (!is_recursive)
            {
                msg::println_warning(msgAddRecurseOption);
                Checks::exit_fail(VCPKG_LINE_INFO);
            }
        }

        std::map<std::string, PackageSpec> not_installed_names;
        for (auto&& action : plan.not_installed)
        {
            // Only keep one spec per name
            not_installed_names.emplace(action.spec.name(), action.spec);
        }
        if (!not_installed_names.empty())
        {
            // The user requested removing a package that was not installed. If the port is installed for another
            // triplet, warn the user that they may have meant that other package.
            for (const auto& package : status_db)
            {
                if (package->is_installed() && !package->package.is_feature())
                {
                    auto it = not_installed_names.find(package->package.spec.name());
                    if (it != not_installed_names.end())
                    {
                        msg::println_warning(msgRemovePackageConflict,
                                             msg::package_name = it->first,
                                             msg::spec = it->second,
                                             msg::triplet = package->package.spec.triplet());
                    }
                }
            }
        }

        if (dry_run)
        {
            Checks::exit_success(VCPKG_LINE_INFO);
        }

        const Filesystem& fs = paths.get_filesystem();
        if (purge == Purge::YES)
        {
            for (auto&& action : plan.not_installed)
            {
                fs.remove_all(paths.package_dir(action.spec), VCPKG_LINE_INFO);
            }
        }

        for (std::size_t idx = 0; idx < plan.remove.size(); ++idx)
        {
            const RemovePlanAction& action = plan.remove[idx];
            msg::println(msgRemovingPackage,
                         msg::action_index = idx + 1,
                         msg::count = plan.remove.size(),
                         msg::spec = action.spec);
            remove_package(fs, paths.installed(), action.spec, status_db);
            if (purge == Purge::YES)
            {
                fs.remove_all(paths.package_dir(action.spec), VCPKG_LINE_INFO);
            }
        }

        Checks::exit_success(VCPKG_LINE_INFO);
    }
}
