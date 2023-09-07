#include <vcpkg/base/files.h>
#include <vcpkg/base/system.process.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.help.h>
#include <vcpkg/commands.portsdiff.h>
#include <vcpkg/paragraphs.h>
#include <vcpkg/tools.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>
#include <vcpkg/versions.h>

using namespace vcpkg;

namespace
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

    std::vector<UpdatedPort> find_updated_ports(const std::vector<std::string>& ports,
                                                const std::map<std::string, Version>& previous_names_and_versions,
                                                const std::map<std::string, Version>& current_names_and_versions)
    {
        std::vector<UpdatedPort> output;
        for (const std::string& name : ports)
        {
            const Version& previous_version = previous_names_and_versions.at(name);
            const Version& current_version = current_names_and_versions.at(name);
            if (previous_version == current_version)
            {
                continue;
            }

            output.push_back({name, VersionDiff(previous_version, current_version)});
        }

        return output;
    }

    void do_print_name_and_version(const std::vector<std::string>& ports_to_print,
                                   const std::map<std::string, Version>& names_and_versions)
    {
        for (const std::string& name : ports_to_print)
        {
            const Version& version = names_and_versions.at(name);
            msg::write_unlocalized_text_to_stdout(Color::none, fmt::format("\t- {:<15}{:<}\n", name, version));
        }
    }

    std::map<std::string, Version> read_ports_from_commit(const VcpkgPaths& paths, const std::string& git_commit_id)
    {
        auto& fs = paths.get_filesystem();
        const auto dot_git_dir = paths.root / ".git";
        const auto ports_dir_name = paths.builtin_ports_directory().filename();
        const auto temp_checkout_path = paths.root / fmt::format("{}-{}", ports_dir_name, git_commit_id);
        fs.create_directory(temp_checkout_path, IgnoreErrors{});
        const auto checkout_this_dir =
            fmt::format(R"(.\{})", ports_dir_name); // Must be relative to the root of the repository

        auto cmd = paths.git_cmd_builder(dot_git_dir, temp_checkout_path)
                       .string_arg("checkout")
                       .string_arg(git_commit_id)
                       .string_arg("-f")
                       .string_arg("-q")
                       .string_arg("--")
                       .string_arg(checkout_this_dir)
                       .string_arg(".vcpkg-root");
        cmd_execute_and_capture_output(cmd, default_working_directory, get_clean_environment());
        cmd_execute_and_capture_output(paths.git_cmd_builder(dot_git_dir, temp_checkout_path).string_arg("reset"),
                                       default_working_directory,
                                       get_clean_environment());
        const auto ports_at_commit = Paragraphs::load_overlay_ports(fs, temp_checkout_path / ports_dir_name);
        std::map<std::string, Version> names_and_versions;
        for (auto&& port : ports_at_commit)
        {
            const auto& core_pgh = *port.source_control_file->core_paragraph;
            names_and_versions.emplace(core_pgh.name, Version(core_pgh.raw_version, core_pgh.port_version));
        }
        fs.remove_all(temp_checkout_path, VCPKG_LINE_INFO);
        return names_and_versions;
    }

    void check_commit_exists(const VcpkgPaths& paths, const std::string& git_commit_id)
    {
        static constexpr StringLiteral VALID_COMMIT_OUTPUT = "commit\n";
        auto cmd = paths.git_cmd_builder(paths.root / ".git", paths.root)
                       .string_arg("cat-file")
                       .string_arg("-t")
                       .string_arg(git_commit_id);
        Checks::msg_check_exit(VCPKG_LINE_INFO,
                               cmd_execute_and_capture_output(cmd).value_or_exit(VCPKG_LINE_INFO).output ==
                                   VALID_COMMIT_OUTPUT,
                               msgInvalidCommitId,
                               msg::commit_sha = git_commit_id);
    }
} // unnamed namespace

namespace vcpkg
{
    constexpr CommandMetadata CommandPortsdiffMetadata{
        "portsdiff",
        msgCmdPortsdiffSynopsis,
        {msgCmdPortsdiffExample1, msgCmdPortsdiffExample2},
        Undocumented,
        AutocompletePriority::Public,
        1,
        2,
        {},
        nullptr,
    };

    void command_portsdiff_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        const auto parsed = args.parse_arguments(CommandPortsdiffMetadata);

        const std::string git_commit_id_for_previous_snapshot = parsed.command_arguments.at(0);
        const std::string git_commit_id_for_current_snapshot =
            parsed.command_arguments.size() < 2 ? "HEAD" : parsed.command_arguments.at(1);

        check_commit_exists(paths, git_commit_id_for_current_snapshot);
        check_commit_exists(paths, git_commit_id_for_previous_snapshot);

        const std::map<std::string, Version> current_names_and_versions =
            read_ports_from_commit(paths, git_commit_id_for_current_snapshot);
        const std::map<std::string, Version> previous_names_and_versions =
            read_ports_from_commit(paths, git_commit_id_for_previous_snapshot);

        // Already sorted, so set_difference can work on std::vector too
        const std::vector<std::string> current_ports = Util::extract_keys(current_names_and_versions);
        const std::vector<std::string> previous_ports = Util::extract_keys(previous_names_and_versions);

        const SetElementPresence<std::string> setp =
            SetElementPresence<std::string>::create(current_ports, previous_ports);

        const std::vector<std::string>& added_ports = setp.only_left;
        if (!added_ports.empty())
        {
            msg::println(msgPortsAdded, msg::count = added_ports.size());
            do_print_name_and_version(added_ports, current_names_and_versions);
        }

        const std::vector<std::string>& removed_ports = setp.only_right;
        if (!removed_ports.empty())
        {
            msg::println(msgPortsRemoved, msg::count = removed_ports.size());
            do_print_name_and_version(removed_ports, previous_names_and_versions);
        }

        const std::vector<std::string>& common_ports = setp.both;
        const std::vector<UpdatedPort> updated_ports =
            find_updated_ports(common_ports, previous_names_and_versions, current_names_and_versions);

        if (!updated_ports.empty())
        {
            msg::println(msgPortsUpdated, msg::count = updated_ports.size());
            for (const UpdatedPort& p : updated_ports)
            {
                msg::write_unlocalized_text_to_stdout(
                    Color::none, fmt::format("\t- {:<15}{:<}\n", p.port, p.version_diff.to_string()));
            }
        }

        if (added_ports.empty() && removed_ports.empty() && updated_ports.empty())
        {
            msg::println(msgPortsNoDiff);
        }

        Checks::exit_success(VCPKG_LINE_INFO);
    }
} // namespace vcpkg
