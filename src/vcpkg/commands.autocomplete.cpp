#include <vcpkg/commands.autocomplete.h>
#include <vcpkg/commands.edit.h>
#include <vcpkg/commands.integrate.h>
#include <vcpkg/commands.upgrade.h>
#include <vcpkg/install.h>
#include <vcpkg/metrics.h>
#include <vcpkg/paragraphs.h>
#include <vcpkg/remove.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkglib.h>
#include <vcpkg/vcpkgpaths.h>

namespace vcpkg::Commands::Autocomplete
{
    [[noreturn]] static void output_sorted_results_and_exit(const LineInfo& line_info,
                                                            std::vector<std::string>&& results)
    {
        const SortedVector<std::string> sorted_results(results);
        msg::write_unlocalized_text_to_stdout(Color::none, Strings::join("\n", sorted_results));

        Checks::exit_success(line_info);
    }

    static std::vector<std::string> combine_port_with_triplets(StringView port,
                                                               const std::vector<std::string>& triplets)
    {
        return Util::fmap(triplets, [&](const std::string& triplet) { return fmt::format("{}:{}", port, triplet); });
    }

    void perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        g_should_send_metrics = false;

        auto&& command_arguments = args.get_forwardable_arguments();
        // Handles vcpkg <command>
        if (command_arguments.size() <= 1)
        {
            StringView requested_command = "";
            if (command_arguments.size() == 1)
            {
                requested_command = command_arguments[0];
            }

            // First try public commands
            std::vector<std::string> public_commands = {"install",
                                                        "search",
                                                        "remove",
                                                        "list",
                                                        "update",
                                                        "hash",
                                                        "help",
                                                        "integrate",
                                                        "export",
                                                        "edit",
                                                        "create",
                                                        "owns",
                                                        "cache",
                                                        "version",
                                                        "contact",
                                                        "upgrade"};

            Util::erase_remove_if(public_commands, [&](const std::string& s) {
                return !Strings::case_insensitive_ascii_starts_with(s, requested_command);
            });

            if (!public_commands.empty())
            {
                output_sorted_results_and_exit(VCPKG_LINE_INFO, std::move(public_commands));
            }

            // If no public commands match, try private commands
            std::vector<std::string> private_commands = {
                "build",
                "buildexternal",
                "ci",
                "depend-info",
                "env",
                "portsdiff",
            };

            Util::erase_remove_if(private_commands, [&](const std::string& s) {
                return !Strings::case_insensitive_ascii_starts_with(s, requested_command);
            });

            output_sorted_results_and_exit(VCPKG_LINE_INFO, std::move(private_commands));
        }

        // command_arguments.size() >= 2
        const auto& command_name = command_arguments[0];

        // Handles vcpkg install package:<triplet>
        if (command_name == "install")
        {
            StringView last_arg = command_arguments.back();
            auto colon = Util::find(last_arg, ':');
            if (colon != last_arg.end())
            {
                auto port_name = StringView{last_arg.begin(), colon};
                auto triplet_prefix = StringView{colon + 1, last_arg.end()};
                // TODO: Support autocomplete for ports in --overlay-ports
                auto maybe_port =
                    Paragraphs::try_load_port(paths.get_filesystem(), paths.builtin_ports_directory() / port_name);
                if (!maybe_port)
                {
                    Checks::exit_success(VCPKG_LINE_INFO);
                }

                std::vector<std::string> triplets = paths.get_available_triplets_names();
                Util::erase_remove_if(triplets, [&](const std::string& s) {
                    return !Strings::case_insensitive_ascii_starts_with(s, triplet_prefix);
                });

                auto result = combine_port_with_triplets(port_name, triplets);

                output_sorted_results_and_exit(VCPKG_LINE_INFO, std::move(result));
            }
        }

        struct CommandEntry
        {
            StringLiteral name;
            const CommandStructure& structure;
        };

        static constexpr CommandEntry COMMANDS[] = {
            CommandEntry{"install", Install::COMMAND_STRUCTURE},
            CommandEntry{"edit", Edit::COMMAND_STRUCTURE},
            CommandEntry{"remove", Remove::COMMAND_STRUCTURE},
            CommandEntry{"integrate", Integrate::COMMAND_STRUCTURE},
            CommandEntry{"upgrade", Upgrade::COMMAND_STRUCTURE},
        };

        for (auto&& command : COMMANDS)
        {
            if (command_name == command.name)
            {
                StringView prefix = command_arguments.back();
                std::vector<std::string> results;

                const bool is_option = Strings::starts_with(prefix, "-");
                if (is_option)
                {
                    for (const auto& s : command.structure.options.switches)
                    {
                        results.push_back(Strings::concat("--", s.name));
                    }
                    for (const auto& s : command.structure.options.settings)
                    {
                        results.push_back(Strings::concat("--", s.name));
                    }
                    for (const auto& s : command.structure.options.multisettings)
                    {
                        results.push_back(Strings::concat("--", s.name));
                    }
                }
                else
                {
                    if (command.structure.valid_arguments != nullptr)
                    {
                        results = command.structure.valid_arguments(paths);
                    }
                }

                Util::erase_remove_if(results, [&](const std::string& s) {
                    return !Strings::case_insensitive_ascii_starts_with(s, prefix);
                });

                if (command.name == "install" && results.size() == 1 && !is_option)
                {
                    const auto port_at_each_triplet =
                        combine_port_with_triplets(results[0], paths.get_available_triplets_names());
                    Util::Vectors::append(&results, port_at_each_triplet);
                }

                output_sorted_results_and_exit(VCPKG_LINE_INFO, std::move(results));
            }
        }

        Checks::exit_success(VCPKG_LINE_INFO);
    }

    void AutocompleteCommand::perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths) const
    {
        Autocomplete::perform_and_exit(args, paths);
    }
}
