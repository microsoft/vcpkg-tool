#include <vcpkg/base/strings.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/util.h>
#include <vcpkg/base/xmlserializer.h>

#include <vcpkg/cmakevars.h>
#include <vcpkg/commands.dependinfo.h>
#include <vcpkg/commands.help.h>
#include <vcpkg/commands.install.h>
#include <vcpkg/dependencies.h>
#include <vcpkg/input.h>
#include <vcpkg/packagespec.h>
#include <vcpkg/portfileprovider.h>
#include <vcpkg/registries.h>
#include <vcpkg/vcpkgcmdarguments.h>

#include <limits.h>

#include <vector>

namespace vcpkg::Commands::DependInfo
{
    namespace
    {
        // invariant: prefix_buf is equivalent on exitv (but may have been reallocated)
        void print_dep_tree(std::string& prefix_buf,
                            const std::string& currDepend,
                            const std::vector<PackageDependInfo>& allDepends,
                            std::set<std::string>& printed)
        {
            if (prefix_buf.size() > 400)
            {
                Checks::msg_exit_with_message(VCPKG_LINE_INFO, msgExceededRecursionDepth);
            }
            auto currPos = std::find_if(
                allDepends.begin(), allDepends.end(), [&currDepend](const auto& p) { return p.package == currDepend; });
            Checks::check_exit(VCPKG_LINE_INFO, currPos != allDepends.end(), "internal vcpkg error");
            if (currPos->dependencies.empty())
            {
                return;
            }

            const size_t original_size = prefix_buf.size();

            if (Util::Sets::contains(printed, currDepend))
            {
                // If we've already printed the set of dependencies, print an elipsis instead
                Strings::append(prefix_buf, "+- ...\n");
                msg::write_unlocalized_text_to_stdout(Color::none, prefix_buf);
                prefix_buf.resize(original_size);
            }
            else
            {
                printed.insert(currDepend);

                for (auto i = currPos->dependencies.begin(); i != currPos->dependencies.end() - 1; ++i)
                {
                    // Print the current level
                    Strings::append(prefix_buf, "+-- ", *i, "\n");
                    msg::write_unlocalized_text_to_stdout(Color::none, prefix_buf);
                    prefix_buf.resize(original_size);

                    // Recurse
                    prefix_buf.append("|   ");
                    print_dep_tree(prefix_buf, *i, allDepends, printed);
                    prefix_buf.resize(original_size);
                }

                // Print the last of the current level
                Strings::append(prefix_buf, "+-- ", currPos->dependencies.back(), "\n");
                msg::write_unlocalized_text_to_stdout(Color::none, prefix_buf);
                prefix_buf.resize(original_size);

                // Recurse
                prefix_buf.append("    ");
                print_dep_tree(prefix_buf, currPos->dependencies.back(), allDepends, printed);
                prefix_buf.resize(original_size);
            }
        }

        constexpr StringLiteral OPTION_DOT = "dot";
        constexpr StringLiteral OPTION_DGML = "dgml";
        constexpr StringLiteral OPTION_SHOW_DEPTH = "show-depth";
        constexpr StringLiteral OPTION_MAX_RECURSE = "max-recurse";
        constexpr StringLiteral OPTION_SORT = "sort";
        constexpr StringLiteral OPTION_FORMAT = "format";

        constexpr std::array<CommandSwitch, 3> DEPEND_SWITCHES = {{
            {OPTION_DOT, nullptr},
            {OPTION_DGML, nullptr},
            {OPTION_SHOW_DEPTH, []() { return msg::format(msgCmdDependInfoOptDepth); }},
        }};

        constexpr std::array<CommandSetting, 3> DEPEND_SETTINGS = {{
            {OPTION_MAX_RECURSE, []() { return msg::format(msgCmdDependInfoOptMaxRecurse); }},
            {OPTION_SORT, []() { return msg::format(msgCmdDependInfoOptSort); }},
            {OPTION_FORMAT, [] { return msg::format(msgCmdDependInfoFormatHelp); }},
        }};

        void assign_depth_to_dependencies(const std::string& package,
                                          const int depth,
                                          const int max_depth,
                                          std::map<std::string, PackageDependInfo>& dependencies_map)
        {
            auto iter = dependencies_map.find(package);
            if (iter == dependencies_map.end())
            {
                Checks::unreachable(VCPKG_LINE_INFO, fmt::format("Not found in dependency graph: {}", package));
            }

            PackageDependInfo& info = iter->second;

            if (depth > info.depth)
            {
                info.depth = depth;
                if (depth < max_depth)
                {
                    for (auto&& dependency : info.dependencies)
                    {
                        assign_depth_to_dependencies(dependency, depth + 1, max_depth, dependencies_map);
                    }
                }
            }
        }

        std::vector<PackageDependInfo> extract_depend_info(const std::vector<const InstallPlanAction*>& install_actions,
                                                           const int max_depth)
        {
            std::map<std::string, PackageDependInfo> package_dependencies;
            for (const InstallPlanAction* pia : install_actions)
            {
                const InstallPlanAction& install_action = *pia;

                const std::vector<std::string> dependencies = Util::fmap(
                    install_action.package_dependencies, [](const PackageSpec& spec) { return spec.name(); });

                std::unordered_set<std::string> features{install_action.feature_list.begin(),
                                                         install_action.feature_list.end()};
                features.erase("core");

                auto& port_name = install_action.spec.name();

                PackageDependInfo info{port_name, -1, features, dependencies};
                package_dependencies.emplace(port_name, std::move(info));
            }

            const InstallPlanAction& init = *install_actions.back();
            assign_depth_to_dependencies(init.spec.name(), 0, max_depth, package_dependencies);

            std::vector<PackageDependInfo> out =
                Util::fmap(package_dependencies, [](auto&& kvpair) -> PackageDependInfo { return kvpair.second; });
            Util::erase_remove_if(out, [](auto&& info) { return info.depth < 0; });
            return out;
        }

        // Try to emplace candidate into maybe_target. If that would be inconsistent, return true.
        // An engaged maybe_target is consistent with candidate if the contained value equals candidate.
        template<typename T>
        bool emplace_inconsistent(Optional<T>& maybe_target, const T& candidate)
        {
            if (auto target = maybe_target.get())
            {
                return *target != candidate;
            }

            maybe_target.emplace(candidate);
            return false;
        }
    } // unnamed namespace

    std::string create_dot_as_string(const std::vector<PackageDependInfo>& depend_info)
    {
        int empty_node_count = 0;

        std::string s = "digraph G{ rankdir=LR; edge [minlen=3]; overlap=false;";

        for (const auto& package : depend_info)
        {
            if (package.dependencies.empty())
            {
                empty_node_count++;
                continue;
            }

            const std::string name = Strings::replace_all(std::string{package.package}, "-", "_");
            fmt::format_to(std::back_inserter(s), "{};", name);
            for (const auto& d : package.dependencies)
            {
                const std::string dependency_name = Strings::replace_all(std::string{d}, "-", "_");
                fmt::format_to(std::back_inserter(s), "{} -> {};", name, dependency_name);
            }
        }

        fmt::format_to(std::back_inserter(s), "empty [label=\"{} singletons...\"]; }}", empty_node_count);
        return s;
    }

    std::string create_dgml_as_string(const std::vector<PackageDependInfo>& depend_info)
    {
        XmlSerializer xml;
        xml.emit_declaration()
            .open_tag(R"(DirectedGraph xmlns="http://schemas.microsoft.com/vs/2009/dgml")");

        XmlSerializer nodes, links;
        nodes.open_tag("Nodes");
        links.open_tag("Link");
        for (const auto& package : depend_info)
        {
            const std::string& name = package.package;
            nodes.start_complex_open_tag("Node").attr("Id", name).finish_self_closing_complex_tag();

            // Iterate over dependencies.
            for (const auto& d : package.dependencies)
            {
                links.start_complex_open_tag("Link").attr("Source", name).attr("Target", d).finish_self_closing_complex_tag();
            }
        }
        nodes.close_tag("Nodes");
        links.close_tag("Link");
        xml.buf.append(nodes.buf).append(links.buf);
        xml.close_tag("DirectedGraph");
        return xml.buf;
    }

    std::string create_mermaid_as_string(const std::vector<PackageDependInfo>& depend_info)
    {
        std::string s = "flowchart TD;";

        for (const auto& package : depend_info)
        {
            for (const auto& dependency : package.dependencies)
            {
                s.append(fmt::format(" {} --> {};", package.package, dependency));
            }
        }

        return s;
    }

    const CommandStructure COMMAND_STRUCTURE = {
        [] { return create_example_string("depend-info sqlite3"); },
        1,
        1,
        {DEPEND_SWITCHES, DEPEND_SETTINGS},
        nullptr,
    };

    ExpectedL<DependInfoStrategy> determine_depend_info_mode(const ParsedArguments& args)
    {
        static constexpr StringLiteral OPTION_FORMAT_LIST = "list";
        static constexpr StringLiteral OPTION_FORMAT_TREE = "tree";
        static constexpr StringLiteral OPTION_FORMAT_DOT = "dot";
        static constexpr StringLiteral OPTION_FORMAT_DGML = "dgml";
        static constexpr StringLiteral OPTION_FORMAT_MERMAID = "mermaid";

        auto& settings = args.settings;

        Optional<DependInfoFormat> maybe_format;
        {
            auto it = settings.find(OPTION_FORMAT);
            if (it != settings.end())
            {
                auto as_lower = Strings::ascii_to_lowercase(it->second);
                if (as_lower == OPTION_FORMAT_LIST)
                {
                    maybe_format.emplace(DependInfoFormat::List);
                }
                else if (as_lower == OPTION_FORMAT_TREE)
                {
                    maybe_format.emplace(DependInfoFormat::Tree);
                }
                else if (as_lower == OPTION_FORMAT_DOT)
                {
                    maybe_format.emplace(DependInfoFormat::Dot);
                }
                else if (as_lower == OPTION_FORMAT_DGML)
                {
                    maybe_format.emplace(DependInfoFormat::Dgml);
                }
                else if (as_lower == OPTION_FORMAT_MERMAID)
                {
                    maybe_format.emplace(DependInfoFormat::Mermaid);
                }
                else
                {
                    return msg::format_error(msgCmdDependInfoFormatInvalid, msg::value = it->second);
                }
            }
        }

        if (Util::Sets::contains(args.switches, OPTION_DOT))
        {
            if (emplace_inconsistent(maybe_format, DependInfoFormat::Dot))
            {
                return msg::format_error(msgCmdDependInfoFormatConflict);
            }
        }

        if (Util::Sets::contains(args.switches, OPTION_DGML))
        {
            if (emplace_inconsistent(maybe_format, DependInfoFormat::Dgml))
            {
                return msg::format_error(msgCmdDependInfoFormatConflict);
            }
        }

        static constexpr StringLiteral OPTION_SORT_LEXICOGRAPHICAL = "lexicographical";
        static constexpr StringLiteral OPTION_SORT_TOPOLOGICAL = "topological";
        static constexpr StringLiteral OPTION_SORT_REVERSE = "reverse";
        static constexpr StringLiteral OPTION_SORT_TREE = "x-tree";
        Optional<DependInfoSortMode> maybe_sort_mode;
        {
            auto it = settings.find(OPTION_SORT);
            if (it != settings.end())
            {
                auto as_lower = Strings::ascii_to_lowercase(it->second);
                if (as_lower == OPTION_SORT_LEXICOGRAPHICAL)
                {
                    maybe_sort_mode.emplace(DependInfoSortMode::Lexicographical);
                }
                else if (as_lower == OPTION_SORT_TOPOLOGICAL)
                {
                    maybe_sort_mode.emplace(DependInfoSortMode::Topological);
                }
                else if (as_lower == OPTION_SORT_REVERSE)
                {
                    maybe_sort_mode.emplace(DependInfoSortMode::ReverseTopological);
                }
                else if (as_lower == OPTION_SORT_TREE)
                {
                    if (emplace_inconsistent(maybe_format, DependInfoFormat::Tree))
                    {
                        return msg::format_error(msgCmdDependInfoXtreeTree);
                    }
                }
                else
                {
                    return msg::format_error(msgInvalidCommandArgSort);
                }
            }
        }

        DependInfoStrategy result{maybe_sort_mode.value_or(DependInfoSortMode::Topological),
                                  maybe_format.value_or(DependInfoFormat::List),
                                  INT_MAX,
                                  Util::Sets::contains(args.switches, OPTION_SHOW_DEPTH)};

        {
            auto it = settings.find(OPTION_MAX_RECURSE);
            if (it != settings.end())
            {
                auto maybe_parsed = Strings::strto<int>(it->second);
                if (auto parsed = maybe_parsed.get())
                {
                    if (*parsed >= 0)
                    {
                        result.max_depth = *parsed;
                    }
                }
                else
                {
                    return msg::format_error(msgOptionMustBeInteger, msg::option = OPTION_MAX_RECURSE);
                }
            }
        }

        if (result.show_depth)
        {
            switch (result.format)
            {
                case DependInfoFormat::List:
                case DependInfoFormat::Tree:
                    // ok
                    break;
                case DependInfoFormat::Dot:
                case DependInfoFormat::Dgml:
                case DependInfoFormat::Mermaid: return msg::format_error(msgCmdDependInfoShowDepthFormatMismatch);
                default: Checks::unreachable(VCPKG_LINE_INFO);
            }
        }

        return result;
    }

    void perform_and_exit(const VcpkgCmdArguments& args,
                          const VcpkgPaths& paths,
                          Triplet default_triplet,
                          Triplet host_triplet)
    {
        const ParsedArguments options = args.parse_arguments(COMMAND_STRUCTURE);
        const auto strategy = determine_depend_info_mode(options).value_or_exit(VCPKG_LINE_INFO);

        bool default_triplet_used = false;
        const std::vector<FullPackageSpec> specs = Util::fmap(options.command_arguments, [&](auto&& arg) {
            return check_and_get_full_package_spec(arg,
                                                   default_triplet,
                                                   default_triplet_used,
                                                   COMMAND_STRUCTURE.get_example_text(),
                                                   paths.get_triplet_db());
        });

        if (default_triplet_used)
        {
            print_default_triplet_warning(args, paths.get_triplet_db());
        }

        auto& fs = paths.get_filesystem();
        auto registry_set = paths.make_registry_set();
        PathsPortFileProvider provider(
            fs, *registry_set, make_overlay_provider(fs, paths.original_cwd, paths.overlay_ports));
        auto var_provider_storage = CMakeVars::make_triplet_cmake_var_provider(paths);
        auto& var_provider = *var_provider_storage;

        // By passing an empty status_db, we should get a plan containing all dependencies.
        // All actions in the plan should be install actions, as there's no installed packages to remove.
        StatusParagraphs status_db;
        auto action_plan = create_feature_install_plan(
            provider, var_provider, specs, status_db, {host_triplet, paths.packages(), UnsupportedPortAction::Warn});
        action_plan.print_unsupported_warnings();

        if (!action_plan.remove_actions.empty())
        {
            Checks::unreachable(VCPKG_LINE_INFO, "Only install actions should exist in the plan");
        }

        std::vector<const InstallPlanAction*> install_actions =
            Util::fmap(action_plan.already_installed, [&](const auto& action) { return &action; });
        for (auto&& action : action_plan.install_actions)
        {
            install_actions.push_back(&action);
        }

        std::vector<PackageDependInfo> depend_info = extract_depend_info(install_actions, strategy.max_depth);

        if (strategy.format == DependInfoFormat::Dot)
        {
            msg::write_unlocalized_text_to_stdout(Color::none, create_dot_as_string(depend_info));
            msg::write_unlocalized_text_to_stdout(Color::none, "\n");
            Checks::exit_success(VCPKG_LINE_INFO);
        }

        if (strategy.format == DependInfoFormat::Dgml)
        {
            msg::write_unlocalized_text_to_stdout(Color::none, create_dgml_as_string(depend_info));
            msg::write_unlocalized_text_to_stdout(Color::none, "\n");
            Checks::exit_success(VCPKG_LINE_INFO);
        }

        if (strategy.format == DependInfoFormat::Mermaid)
        {
            msg::write_unlocalized_text_to_stdout(Color::none, create_mermaid_as_string(depend_info));
            msg::write_unlocalized_text_to_stdout(Color::none, "\n");
            Checks::exit_success(VCPKG_LINE_INFO);
        }

        auto lex = [](const PackageDependInfo& lhs, const PackageDependInfo& rhs) -> bool {
            return lhs.package < rhs.package;
        };
        auto topo = [](const PackageDependInfo& lhs, const PackageDependInfo& rhs) -> bool {
            return lhs.depth > rhs.depth;
        };
        auto reverse = [](const PackageDependInfo& lhs, const PackageDependInfo& rhs) -> bool {
            return lhs.depth < rhs.depth;
        };

        if (strategy.format == DependInfoFormat::Tree)
        {
            Util::sort(depend_info, reverse);
            auto first = depend_info.begin();
            std::string features = Strings::join(", ", first->features);

            if (strategy.show_depth)
            {
                msg::write_unlocalized_text_to_stdout(Color::error, fmt::format("({})", first->depth));
            }

            msg::write_unlocalized_text_to_stdout(Color::success, first->package);
            if (!features.empty())
            {
                msg::write_unlocalized_text_to_stdout(Color::warning, "[" + features + "]");
            }

            msg::write_unlocalized_text_to_stdout(Color::none, "\n");
            std::set<std::string> printed;
            std::string prefix_buf;
            print_dep_tree(prefix_buf, first->package, depend_info, printed);
            Checks::exit_success(VCPKG_LINE_INFO);
        }

        if (strategy.format != DependInfoFormat::List)
        {
            Checks::unreachable(VCPKG_LINE_INFO);
        }

        switch (strategy.sort_mode)
        {
            case DependInfoSortMode::Lexicographical: Util::sort(depend_info, lex); break;
            case DependInfoSortMode::ReverseTopological: Util::sort(depend_info, reverse); break;
            case DependInfoSortMode::Topological: Util::sort(depend_info, topo); break;
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }

        for (auto&& info : depend_info)
        {
            if (info.depth < 0)
            {
                continue;
            }

            if (strategy.show_depth)
            {
                msg::write_unlocalized_text_to_stdout(Color::error, fmt::format("({})", info.depth));
            }

            msg::write_unlocalized_text_to_stdout(Color::success, info.package);
            if (!info.features.empty())
            {
                msg::write_unlocalized_text_to_stdout(Color::warning, "[" + Strings::join(", ", info.features) + "]");
            }

            msg::write_unlocalized_text_to_stdout(Color::none, ": " + Strings::join(", ", info.dependencies) + "\n");
        }

        Checks::exit_success(VCPKG_LINE_INFO);
    }
}
