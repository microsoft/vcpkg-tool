#include <vcpkg/base/system.print.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.h>
#include <vcpkg/dependencies.h>
#include <vcpkg/help.h>
#include <vcpkg/input.h>
#include <vcpkg/paragraphs.h>
#include <vcpkg/portfileprovider.h>
#include <vcpkg/remove.h>
#include <vcpkg/update.h>
#include <vcpkg/vcpkglib.h>

namespace vcpkg::Remove
{
    using Dependencies::RemovePlanAction;
    using Dependencies::RemovePlanType;
    using Dependencies::RequestType;
    using Update::OutdatedPackage;

    void remove_package(const VcpkgPaths& paths, const PackageSpec& spec, StatusParagraphs* status_db)
    {
        auto& fs = paths.get_filesystem();
        auto maybe_ipv = status_db->get_installed_package_view(spec);

        Checks::check_exit(
            VCPKG_LINE_INFO, maybe_ipv.has_value(), "unable to remove package %s: already removed", spec);

        auto&& ipv = maybe_ipv.value_or_exit(VCPKG_LINE_INFO);

        std::vector<StatusParagraph> spghs;
        spghs.emplace_back(*ipv.core);
        for (auto&& feature : ipv.features)
        {
            spghs.emplace_back(*feature);
        }

        for (auto&& spgh : spghs)
        {
            spgh.want = Want::PURGE;
            spgh.state = InstallState::HALF_INSTALLED;
            write_update(paths, spgh);
        }

        auto maybe_lines = fs.read_lines(paths.listfile_path(ipv.core->package));

        if (const auto lines = maybe_lines.get())
        {
            std::vector<path> dirs_touched;
            for (auto&& suffix : *lines)
            {
                if (!suffix.empty() && suffix.back() == '\r') suffix.pop_back();

                std::error_code ec;

                auto target = paths.installed / suffix;

                const auto status = fs.symlink_status(target, ec);
                if (ec)
                {
                    print2(Color::error, "failed: status(", vcpkg::u8string(target), "): ", ec.message(), "\n");
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
                        // TODO: this is racy; should we ignore this error?
#if defined(_WIN32)
                        stdfs::permissions(target, stdfs::perms::owner_all | stdfs::perms::group_all, ec);
                        fs.remove(target, ec);
                        if (ec)
                        {
                            vcpkg::printf(
                                Color::error, "failed: remove(%s): %s\n", vcpkg::u8string(target), ec.message());
                        }
#else
                        vcpkg::printf(Color::error, "failed: remove(%s): %s\n", vcpkg::u8string(target), ec.message());
#endif
                    }
                }
                else if (!vcpkg::exists(status))
                {
                    vcpkg::printf(Color::warning, "Warning: %s: file not found\n", vcpkg::u8string(target));
                }
                else
                {
                    vcpkg::printf(Color::warning, "Warning: %s: cannot handle file type\n", vcpkg::u8string(target));
                }
            }

            auto b = dirs_touched.rbegin();
            const auto e = dirs_touched.rend();
            for (; b != e; ++b)
            {
                if (fs.is_empty(*b))
                {
                    std::error_code ec;
                    fs.remove(*b, ec);
                    if (ec)
                    {
                        print2(Color::error, "failed: ", ec.message(), "\n");
                    }
                }
            }

            fs.remove(paths.listfile_path(ipv.core->package), VCPKG_LINE_INFO);
        }

        for (auto&& spgh : spghs)
        {
            spgh.state = InstallState::NOT_INSTALLED;
            write_update(paths, spgh);

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
                return Dependencies::to_output_string(p->request_type, p->spec.to_string());
            });

            switch (plan_type)
            {
                case RemovePlanType::NOT_INSTALLED:
                    print2("The following packages are not installed, so not removed:\n", as_string, "\n");
                    continue;
                case RemovePlanType::REMOVE:
                    print2("The following packages will be removed:\n", as_string, "\n");
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
        const std::string display_name = action.spec.to_string();

        switch (action.plan_type)
        {
            case RemovePlanType::NOT_INSTALLED:
                vcpkg::printf(Color::success, "Package %s is not installed\n", display_name);
                break;
            case RemovePlanType::REMOVE:
                vcpkg::printf("Removing package %s...\n", display_name);
                remove_package(paths, action.spec, status_db);
                vcpkg::printf(Color::success, "Removing package %s... done\n", display_name);
                break;
            case RemovePlanType::UNKNOWN:
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }

        if (purge == Purge::YES)
        {
            Filesystem& fs = paths.get_filesystem();
            fs.remove_all(paths.packages / action.spec.dir(), VCPKG_LINE_INFO);
        }
    }

    static constexpr StringLiteral OPTION_PURGE = "purge";
    static constexpr StringLiteral OPTION_NO_PURGE = "no-purge";
    static constexpr StringLiteral OPTION_RECURSE = "recurse";
    static constexpr StringLiteral OPTION_DRY_RUN = "dry-run";
    static constexpr StringLiteral OPTION_OUTDATED = "outdated";

    static constexpr std::array<CommandSwitch, 5> SWITCHES = {{
        {OPTION_PURGE, "Remove the cached copy of the package (default)"},
        {OPTION_NO_PURGE, "Do not remove the cached copy of the package (deprecated)"},
        {OPTION_RECURSE, "Allow removal of packages not explicitly specified on the command line"},
        {OPTION_DRY_RUN, "Print the packages to be removed, but do not remove them"},
        {OPTION_OUTDATED, "Select all packages with versions that do not match the portfiles"},
    }};

    static std::vector<std::string> valid_arguments(const VcpkgPaths& paths)
    {
        const StatusParagraphs status_db = database_load_check(paths);
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
            Checks::exit_maybe_upgrade(
                VCPKG_LINE_INFO,
                "To remove dependencies in manifest mode, edit your manifest (vcpkg.json) and run 'install'.");
        }
        const ParsedArguments options = args.parse_arguments(COMMAND_STRUCTURE);

        StatusParagraphs status_db = database_load_check(paths);
        std::vector<PackageSpec> specs;
        if (Util::Sets::contains(options.switches, OPTION_OUTDATED))
        {
            if (args.command_arguments.size() != 0)
            {
                print2(Color::error, "Error: 'remove' accepts either libraries or '--outdated'\n");
                Checks::exit_fail(VCPKG_LINE_INFO);
            }

            // Load ports from ports dirs
            PortFileProvider::PathsPortFileProvider provider(paths, args.overlay_ports);

            specs = Util::fmap(Update::find_outdated_packages(provider, status_db),
                               [](auto&& outdated) { return outdated.spec; });

            if (specs.empty())
            {
                print2(Color::success, "There are no outdated packages.\n");
                Checks::exit_success(VCPKG_LINE_INFO);
            }
        }
        else
        {
            if (args.command_arguments.size() < 1)
            {
                print2(Color::error, "Error: 'remove' accepts either libraries or '--outdated'\n");
                Checks::exit_fail(VCPKG_LINE_INFO);
            }
            specs = Util::fmap(args.command_arguments, [&](auto&& arg) {
                return Input::check_and_get_package_spec(
                    std::string(arg), default_triplet, COMMAND_STRUCTURE.example_text);
            });

            for (auto&& spec : specs)
                Input::check_triplet(spec.triplet(), paths);
        }

        const bool no_purge_was_passed = Util::Sets::contains(options.switches, OPTION_NO_PURGE);
        const bool purge_was_passed = Util::Sets::contains(options.switches, OPTION_PURGE);
        if (purge_was_passed && no_purge_was_passed)
        {
            print2(Color::error, "Error: cannot specify both --no-purge and --purge.\n");
            print2(COMMAND_STRUCTURE.example_text);
            Checks::exit_fail(VCPKG_LINE_INFO);
        }
        const Purge purge = to_purge(purge_was_passed || !no_purge_was_passed);
        const bool is_recursive = Util::Sets::contains(options.switches, OPTION_RECURSE);
        const bool dry_run = Util::Sets::contains(options.switches, OPTION_DRY_RUN);

        const std::vector<RemovePlanAction> remove_plan = Dependencies::create_remove_plan(specs, status_db);
        Checks::check_exit(VCPKG_LINE_INFO, !remove_plan.empty(), "Remove plan cannot be empty");

        std::map<RemovePlanType, std::vector<const RemovePlanAction*>> group_by_plan_type;
        Util::group_by(remove_plan, &group_by_plan_type, [](const RemovePlanAction& p) { return p.plan_type; });
        print_plan(group_by_plan_type);

        const bool has_non_user_requested_packages =
            Util::find_if(remove_plan, [](const RemovePlanAction& package) -> bool {
                return package.request_type != RequestType::USER_REQUESTED;
            }) != remove_plan.cend();

        if (has_non_user_requested_packages)
        {
            print2(Color::warning, "Additional packages (*) need to be removed to complete this operation.\n");

            if (!is_recursive)
            {
                print2(Color::warning,
                       "If you are sure you want to remove them, run the command with the --recurse option\n");
                Checks::exit_fail(VCPKG_LINE_INFO);
            }
        }

        for (const auto& action : remove_plan)
        {
            if (action.plan_type == RemovePlanType::NOT_INSTALLED && action.request_type == RequestType::USER_REQUESTED)
            {
                // The user requested removing a package that was not installed. If the port is installed for another
                // triplet, warn the user that they may have meant that other package.
                for (const auto& package : status_db)
                {
                    if (package->is_installed() && !package->package.is_feature() &&
                        package->package.spec.name() == action.spec.name())
                    {
                        print2(Color::warning,
                               "Another installed package matches the name of an unmatched request. Did you mean ",
                               package->package.spec,
                               "?\n");
                    }
                }
            }
        }

        if (dry_run)
        {
            Checks::exit_success(VCPKG_LINE_INFO);
        }

        for (const RemovePlanAction& action : remove_plan)
        {
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
