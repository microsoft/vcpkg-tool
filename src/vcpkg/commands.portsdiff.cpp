#include <vcpkg/base/sortedvector.h>
#include <vcpkg/base/system.print.h>
#include <vcpkg/base/system.process.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.portsdiff.h>
#include <vcpkg/help.h>
#include <vcpkg/paragraphs.h>
#include <vcpkg/tools.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>
#include <vcpkg/versiont.h>

namespace vcpkg::Commands::PortsDiff
{
    struct UpdatedPort
    {
        static bool compare_by_name(const UpdatedPort& left, const UpdatedPort& right)
        {
            return left.port < right.port;
        }

        std::string port;
        VersionDiff version_diff;
    };

    template<class T>
    struct SetElementPresence
    {
        static SetElementPresence create(std::vector<T> left, std::vector<T> right)
        {
            // TODO: This can be done with one pass instead of three passes
            SetElementPresence output;
            std::set_difference(
                left.cbegin(), left.cend(), right.cbegin(), right.cend(), std::back_inserter(output.only_left));
            std::set_intersection(
                left.cbegin(), left.cend(), right.cbegin(), right.cend(), std::back_inserter(output.both));
            std::set_difference(
                right.cbegin(), right.cend(), left.cbegin(), left.cend(), std::back_inserter(output.only_right));

            return output;
        }

        std::vector<T> only_left;
        std::vector<T> both;
        std::vector<T> only_right;
    };

    static std::vector<UpdatedPort> find_updated_ports(
        const std::vector<std::string>& ports,
        const std::map<std::string, VersionT>& previous_names_and_versions,
        const std::map<std::string, VersionT>& current_names_and_versions)
    {
        std::vector<UpdatedPort> output;
        for (const std::string& name : ports)
        {
            const VersionT& previous_version = previous_names_and_versions.at(name);
            const VersionT& current_version = current_names_and_versions.at(name);
            if (previous_version == current_version)
            {
                continue;
            }

            output.push_back({name, VersionDiff(previous_version, current_version)});
        }

        return output;
    }

    static void do_print_name_and_version(const std::vector<std::string>& ports_to_print,
                                          const std::map<std::string, VersionT>& names_and_versions)
    {
        for (const std::string& name : ports_to_print)
        {
            const VersionT& version = names_and_versions.at(name);
            vcpkg::printf("    - %-14s %-16s\n", name, version);
        }
    }

    static std::map<std::string, VersionT> read_ports_from_commit(const VcpkgPaths& paths,
                                                                  const std::string& git_commit_id)
    {
        std::error_code ec;
        auto& fs = paths.get_filesystem();
        const path dot_git_dir = paths.root / ".git";
        const std::string ports_dir_name_as_string = vcpkg::u8string(paths.builtin_ports_directory().filename());
        const path temp_checkout_path = paths.root / Strings::format("%s-%s", ports_dir_name_as_string, git_commit_id);
        fs.create_directory(temp_checkout_path, ec);
        const auto checkout_this_dir =
            Strings::format(R"(.\%s)", ports_dir_name_as_string); // Must be relative to the root of the repository

        auto cmd = paths.git_cmd_builder(dot_git_dir, temp_checkout_path)
                       .string_arg("checkout")
                       .string_arg(git_commit_id)
                       .string_arg("-f")
                       .string_arg("-q")
                       .string_arg("--")
                       .string_arg(checkout_this_dir)
                       .string_arg(".vcpkg-root");
        cmd_execute_and_capture_output(cmd, get_clean_environment());
        cmd_execute_and_capture_output(paths.git_cmd_builder(dot_git_dir, temp_checkout_path).string_arg("reset"),
                                       get_clean_environment());
        const auto ports_at_commit = Paragraphs::load_overlay_ports(fs, temp_checkout_path / ports_dir_name_as_string);
        std::map<std::string, VersionT> names_and_versions;
        for (auto&& port : ports_at_commit)
        {
            const auto& core_pgh = *port.source_control_file->core_paragraph;
            names_and_versions.emplace(core_pgh.name, VersionT(core_pgh.version, core_pgh.port_version));
        }
        fs.remove_all(temp_checkout_path, VCPKG_LINE_INFO);
        return names_and_versions;
    }

    static void check_commit_exists(const VcpkgPaths& paths, const std::string& git_commit_id)
    {
        static const std::string VALID_COMMIT_OUTPUT = "commit\n";

        auto cmd = paths.git_cmd_builder(paths.root / ".git", paths.root)
                       .string_arg("cat-file")
                       .string_arg("-t")
                       .string_arg(git_commit_id);
        const ExitCodeAndOutput output = cmd_execute_and_capture_output(cmd);
        Checks::check_exit(
            VCPKG_LINE_INFO, output.output == VALID_COMMIT_OUTPUT, "Invalid commit id %s", git_commit_id);
    }

    const CommandStructure COMMAND_STRUCTURE = {
        Strings::format("The argument should be a branch/tag/hash to checkout.\n%s",
                        create_example_string("portsdiff mybranchname")),
        1,
        2,
        {},
        nullptr,
    };

    void perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        (void)args.parse_arguments(COMMAND_STRUCTURE);

        const std::string git_commit_id_for_previous_snapshot = args.command_arguments.at(0);
        const std::string git_commit_id_for_current_snapshot =
            args.command_arguments.size() < 2 ? "HEAD" : args.command_arguments.at(1);

        check_commit_exists(paths, git_commit_id_for_current_snapshot);
        check_commit_exists(paths, git_commit_id_for_previous_snapshot);

        const std::map<std::string, VersionT> current_names_and_versions =
            read_ports_from_commit(paths, git_commit_id_for_current_snapshot);
        const std::map<std::string, VersionT> previous_names_and_versions =
            read_ports_from_commit(paths, git_commit_id_for_previous_snapshot);

        // Already sorted, so set_difference can work on std::vector too
        const std::vector<std::string> current_ports = Util::extract_keys(current_names_and_versions);
        const std::vector<std::string> previous_ports = Util::extract_keys(previous_names_and_versions);

        const SetElementPresence<std::string> setp =
            SetElementPresence<std::string>::create(current_ports, previous_ports);

        const std::vector<std::string>& added_ports = setp.only_left;
        if (!added_ports.empty())
        {
            vcpkg::printf("\nThe following %zd ports were added:\n", added_ports.size());
            do_print_name_and_version(added_ports, current_names_and_versions);
        }

        const std::vector<std::string>& removed_ports = setp.only_right;
        if (!removed_ports.empty())
        {
            vcpkg::printf("\nThe following %zd ports were removed:\n", removed_ports.size());
            do_print_name_and_version(removed_ports, previous_names_and_versions);
        }

        const std::vector<std::string>& common_ports = setp.both;
        const std::vector<UpdatedPort> updated_ports =
            find_updated_ports(common_ports, previous_names_and_versions, current_names_and_versions);

        if (!updated_ports.empty())
        {
            vcpkg::printf("\nThe following %zd ports were updated:\n", updated_ports.size());
            for (const UpdatedPort& p : updated_ports)
            {
                vcpkg::printf("    - %-14s %-16s\n", p.port, p.version_diff.to_string());
            }
        }

        if (added_ports.empty() && removed_ports.empty() && updated_ports.empty())
        {
            print2("There were no changes in the ports between the two commits.\n");
        }

        Checks::exit_success(VCPKG_LINE_INFO);
    }

    void PortsDiffCommand::perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths) const
    {
        PortsDiff::perform_and_exit(args, paths);
    }
}
