#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/expected.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/util.h>
#include <vcpkg/base/xmlserializer.h>

#include <vcpkg/cmakevars.h>
#include <vcpkg/commands.depend-info.h>
#include <vcpkg/dependencies.h>
#include <vcpkg/input.h>
#include <vcpkg/packagespec.h>
#include <vcpkg/portfileprovider.h>
#include <vcpkg/registries.h>
#include <vcpkg/triplet.h>
#include <vcpkg/vcpkgcmdarguments.h>

#include <limits.h>

using namespace vcpkg;

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
            msg::write_unlocalized_text(Color::none, prefix_buf);
            prefix_buf.resize(original_size);
        }
        else
        {
            printed.insert(currDepend);

            for (auto i = currPos->dependencies.begin(); i != currPos->dependencies.end() - 1; ++i)
            {
                // Print the current level
                Strings::append(prefix_buf, "+-- ", *i, "\n");
                msg::write_unlocalized_text(Color::none, prefix_buf);
                prefix_buf.resize(original_size);

                // Recurse
                prefix_buf.append("|   ");
                print_dep_tree(prefix_buf, *i, allDepends, printed);
                prefix_buf.resize(original_size);
            }

            // Print the last of the current level
            Strings::append(prefix_buf, "+-- ", currPos->dependencies.back(), "\n");
            msg::write_unlocalized_text(Color::none, prefix_buf);
            prefix_buf.resize(original_size);

            // Recurse
            prefix_buf.append("    ");
            print_dep_tree(prefix_buf, currPos->dependencies.back(), allDepends, printed);
            prefix_buf.resize(original_size);
        }
    }

    constexpr CommandSwitch DEPEND_SWITCHES[] = {
        {SwitchDot, {}},
        {SwitchDgml, {}},
        {SwitchShowDepth, msgCmdDependInfoOptDepth},
    };

    constexpr CommandSetting DEPEND_SETTINGS[] = {
        {SwitchMaxRecurse, msgCmdDependInfoOptMaxRecurse},
        {SwitchSort, msgCmdDependInfoOptSort},
        {SwitchFormat, msgCmdDependInfoFormatHelp},
    };

    void assign_depth_to_dependencies(const std::vector<PackageDependInfo>& packages,
                                      const std::map<std::string, PackageDependInfo&>& dependencies_map)
    {
        for (auto it = packages.rbegin(), last = packages.rend(); it != last; ++it)
        {
            int new_depth = it->depth + 1;
            for (const auto& dependency : it->dependencies)
            {
                auto match = dependencies_map.find(dependency);
                if (match == dependencies_map.end())
                {
                    Checks::unreachable(VCPKG_LINE_INFO, fmt::format("Not found in dependency graph: {}", dependency));
                }

                if (match->second.depth < new_depth)
                {
                    match->second.depth = new_depth;
                }
            }
        };
    }

    std::vector<PackageDependInfo> extract_depend_info(const std::vector<const InstallPlanAction*>& install_actions,
                                                       const Triplet& default_triplet,
                                                       const Triplet& host_triplet,
                                                       const int max_depth)
    {
        const bool is_native = default_triplet == host_triplet;
        auto decorated_name = [&default_triplet, &host_triplet, is_native](const PackageSpec& spec) -> std::string {
            if (!is_native && spec.triplet() == host_triplet) return spec.name() + ":host";
            if (spec.triplet() == default_triplet) return spec.name();
            return spec.name() + ':' + spec.triplet().canonical_name();
        };

        std::vector<PackageDependInfo> out;
        out.reserve(install_actions.size());

        std::map<std::string, PackageDependInfo&> package_dependencies;
        for (const InstallPlanAction* pia : install_actions)
        {
            const InstallPlanAction& install_action = *pia;

            const std::vector<std::string> dependencies =
                Util::fmap(install_action.package_dependencies,
                           [&decorated_name](const PackageSpec& spec) { return decorated_name(spec); });

            std::unordered_set<std::string> features{install_action.feature_list.begin(),
                                                     install_action.feature_list.end()};
            features.erase(FeatureNameCore.to_string());

            out.push_back({decorated_name(install_action.spec), 0, std::move(features), std::move(dependencies)});

            package_dependencies.emplace(out.back().package, out.back());
        }

        assign_depth_to_dependencies(out, package_dependencies);
        Util::erase_remove_if(out, [max_depth](auto&& info) { return info.depth > max_depth; });
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

namespace vcpkg
{
    namespace
    {
        const char* get_dot_element_style(StringView label)
        {
            if (!label.contains(':')) return "";

            if (label.ends_with(":host")) return " [color=gray51 fontcolor=gray51]";

            return " [color=blue fontcolor=blue]";
        }
    }

    std::string create_dot_as_string(const std::vector<PackageDependInfo>& depend_info)
    {
        int empty_node_count = 0;

        std::string s = "digraph G{ rankdir=LR; node [fontname=Sans]; edge [minlen=3]; overlap=false;\n";

        for (const auto& package : depend_info)
        {
            fmt::format_to(
                std::back_inserter(s), "\"{}\"{};\n", package.package, get_dot_element_style(package.package));
            if (package.dependencies.empty())
            {
                empty_node_count++;
                continue;
            }

            for (const auto& d : package.dependencies)
            {
                fmt::format_to(
                    std::back_inserter(s), "\"{}\" -> \"{}\"{};\n", package.package, d, get_dot_element_style(d));
            }
        }

        fmt::format_to(std::back_inserter(s), "\"{} singletons...\";\n}}", empty_node_count);
        return s;
    }

    std::string create_dgml_as_string(const std::vector<PackageDependInfo>& depend_info)
    {
        XmlSerializer xml;
        xml.emit_declaration().open_tag(R"(DirectedGraph xmlns="http://schemas.microsoft.com/vs/2009/dgml")");

        XmlSerializer nodes, links;
        nodes.open_tag("Nodes");
        links.open_tag("Links");
        for (const auto& package : depend_info)
        {
            const std::string& name = package.package;
            nodes.start_complex_open_tag("Node").attr("Id", name).finish_self_closing_complex_tag();

            // Iterate over dependencies.
            for (const auto& d : package.dependencies)
            {
                links.start_complex_open_tag("Link")
                    .attr("Source", name)
                    .attr("Target", d)
                    .finish_self_closing_complex_tag();
            }
        }
        nodes.close_tag("Nodes");
        links.close_tag("Links");
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

    constexpr CommandMetadata CommandDependInfoMetadata{
        "depend-info",
        msgHelpDependInfoCommand,
        {msgCmdDependInfoExample1, "vcpkg depend-info zlib"},
        "https://learn.microsoft.com/vcpkg/commands/depend-info",
        AutocompletePriority::Public,
        1,
        SIZE_MAX,
        {DEPEND_SWITCHES, DEPEND_SETTINGS},
        nullptr,
    };

    ExpectedL<DependInfoStrategy> determine_depend_info_mode(const ParsedArguments& args)
    {
        static constexpr StringLiteral SwitchFormatList = "list";
        static constexpr StringLiteral SwitchFormatTree = "tree";
        static constexpr StringLiteral SwitchFormatDot = "dot";
        static constexpr StringLiteral SwitchFormatDgml = "dgml";
        static constexpr StringLiteral SwitchFormatMermaid = "mermaid";

        auto& settings = args.settings;

        Optional<DependInfoFormat> maybe_format;
        {
            auto it = settings.find(SwitchFormat);
            if (it != settings.end())
            {
                auto as_lower = Strings::ascii_to_lowercase(it->second);
                if (as_lower == SwitchFormatList)
                {
                    maybe_format.emplace(DependInfoFormat::List);
                }
                else if (as_lower == SwitchFormatTree)
                {
                    maybe_format.emplace(DependInfoFormat::Tree);
                }
                else if (as_lower == SwitchFormatDot)
                {
                    maybe_format.emplace(DependInfoFormat::Dot);
                }
                else if (as_lower == SwitchFormatDgml)
                {
                    maybe_format.emplace(DependInfoFormat::Dgml);
                }
                else if (as_lower == SwitchFormatMermaid)
                {
                    maybe_format.emplace(DependInfoFormat::Mermaid);
                }
                else
                {
                    return msg::format_error(msgCmdDependInfoFormatInvalid, msg::value = it->second);
                }
            }
        }

        if (Util::Sets::contains(args.switches, SwitchDot))
        {
            if (emplace_inconsistent(maybe_format, DependInfoFormat::Dot))
            {
                return msg::format_error(msgCmdDependInfoFormatConflict);
            }
        }

        if (Util::Sets::contains(args.switches, SwitchDgml))
        {
            if (emplace_inconsistent(maybe_format, DependInfoFormat::Dgml))
            {
                return msg::format_error(msgCmdDependInfoFormatConflict);
            }
        }

        Optional<DependInfoSortMode> maybe_sort_mode;
        {
            auto it = settings.find(SwitchSort);
            if (it != settings.end())
            {
                auto as_lower = Strings::ascii_to_lowercase(it->second);
                if (as_lower == SortLexicographical)
                {
                    maybe_sort_mode.emplace(DependInfoSortMode::Lexicographical);
                }
                else if (as_lower == SortTopological)
                {
                    maybe_sort_mode.emplace(DependInfoSortMode::Topological);
                }
                else if (as_lower == SortReverse)
                {
                    maybe_sort_mode.emplace(DependInfoSortMode::ReverseTopological);
                }
                else if (as_lower == SortXTree)
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
                                  Util::Sets::contains(args.switches, SwitchShowDepth)};

        {
            auto it = settings.find(SwitchMaxRecurse);
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
                    return msg::format_error(msgOptionMustBeInteger, msg::option = SwitchMaxRecurse);
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

    void command_depend_info_and_exit(const VcpkgCmdArguments& args,
                                      const VcpkgPaths& paths,
                                      Triplet default_triplet,
                                      Triplet host_triplet)
    {
        msg::default_output_stream = OutputStream::StdErr;
        const ParsedArguments options = args.parse_arguments(CommandDependInfoMetadata);
        const auto strategy = determine_depend_info_mode(options).value_or_exit(VCPKG_LINE_INFO);

        const std::vector<FullPackageSpec> specs = Util::fmap(options.command_arguments, [&](const std::string& arg) {
            return check_and_get_full_package_spec(arg, default_triplet, paths.get_triplet_db())
                .value_or_exit(VCPKG_LINE_INFO);
        });

        auto& fs = paths.get_filesystem();
        auto registry_set = paths.make_registry_set();
        PathsPortFileProvider provider(*registry_set, make_overlay_provider(fs, paths.overlay_ports));
        auto var_provider_storage = CMakeVars::make_triplet_cmake_var_provider(paths);
        auto& var_provider = *var_provider_storage;

        // By passing an empty status_db, we should get a plan containing all dependencies.
        // All actions in the plan should be install actions, as there's no installed packages to remove.
        StatusParagraphs status_db;
        PackagesDirAssigner packages_dir_assigner{paths.packages()};
        auto action_plan = create_feature_install_plan(
            provider,
            var_provider,
            specs,
            status_db,
            packages_dir_assigner,
            {nullptr, host_triplet, UnsupportedPortAction::Warn, UseHeadVersion::No, Editable::No});
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

        std::vector<PackageDependInfo> depend_info =
            extract_depend_info(install_actions, default_triplet, host_triplet, strategy.max_depth);

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
            std::set<std::string> printed;
            for (auto&& info : depend_info)
            {
                if (info.depth != 0) continue;

                std::string features = Strings::join(", ", info.features);

                if (strategy.show_depth)
                {
                    msg::write_unlocalized_text(Color::error, "(0)"); // legacy
                }

                auto end_of_name = info.package.find(':');
                msg::write_unlocalized_text(Color::success, info.package.substr(0, end_of_name));
                if (!features.empty())
                {
                    msg::write_unlocalized_text(Color::warning, "[" + features + "]");
                }
                if (end_of_name != std::string::npos)
                {
                    msg::write_unlocalized_text(Color::success, info.package.substr(end_of_name));
                }

                msg::write_unlocalized_text(Color::none, "\n");
                std::string prefix_buf;
                print_dep_tree(prefix_buf, info.package, depend_info, printed);
            }

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
            if (strategy.show_depth)
            {
                msg::write_unlocalized_text(Color::error, fmt::format("({})", info.depth));
            }

            auto end_of_name = info.package.find(':');
            msg::write_unlocalized_text(Color::success, info.package.substr(0, end_of_name));
            if (!info.features.empty())
            {
                msg::write_unlocalized_text(Color::warning, "[" + Strings::join(", ", info.features) + "]");
            }
            if (end_of_name != std::string::npos)
            {
                msg::write_unlocalized_text(Color::success, info.package.substr(end_of_name));
            }

            msg::write_unlocalized_text(Color::none, ": " + Strings::join(", ", info.dependencies) + "\n");
        }

        Checks::exit_success(VCPKG_LINE_INFO);
    }
} // namespace vcpkg
