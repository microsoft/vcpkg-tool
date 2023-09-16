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
    template<class T>
    struct SetElementPresence
    {
        static SetElementPresence create(const std::vector<T>& left, const std::vector<T>& right)
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

    void print_name_only(StringView name)
    {
        msg::write_unlocalized_text_to_stdout(Color::none, fmt::format("\t- {:<15}\n", name));
    }

    void print_name_and_version(StringView name, const Version& version)
    {
        msg::write_unlocalized_text_to_stdout(Color::none, fmt::format("\t- {:<15}{:<}\n", name, version));
    }

    void print_name_and_version_diff(StringView name, const VersionDiff& version_diff)
    {
        msg::write_unlocalized_text_to_stdout(Color::none, fmt::format("\t- {:<15}{:<}\n", name, version_diff));
    }

    std::map<std::string, Version> read_ports_from_commit(const VcpkgPaths& paths, StringView git_commit_id)
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

    void check_commit_exists(const VcpkgPaths& paths, StringView git_commit_id)
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
    PortsDiff find_portsdiff(const VcpkgPaths& paths,
                             StringView git_commit_id_for_previous_snapshot,
                             StringView git_commit_id_for_current_snapshot)
    {
        check_commit_exists(paths, git_commit_id_for_current_snapshot);
        check_commit_exists(paths, git_commit_id_for_previous_snapshot);

        const auto current_names_and_versions = read_ports_from_commit(paths, git_commit_id_for_current_snapshot);
        const auto previous_names_and_versions = read_ports_from_commit(paths, git_commit_id_for_previous_snapshot);

        // Already sorted, so set_difference can work on std::vector too
        const auto current_ports = Util::extract_keys(current_names_and_versions);
        const auto previous_ports = Util::extract_keys(previous_names_and_versions);

        const SetElementPresence<std::string> setp =
            SetElementPresence<std::string>::create(current_ports, previous_ports);

        return PortsDiff{
            Util::fmap(setp.only_left,
                       [&](const std::string& added_port) {
                           return VersionSpec{added_port, current_names_and_versions.find(added_port)->second};
                       }),
            find_updated_ports(setp.both, previous_names_and_versions, current_names_and_versions),
            setp.only_right};
    }

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

        const StringView git_commit_id_for_previous_snapshot = parsed.command_arguments[0];
        const StringView git_commit_id_for_current_snapshot =
            parsed.command_arguments.size() < 2 ? "HEAD" : parsed.command_arguments[1];

        const auto portsdiff =
            find_portsdiff(paths, git_commit_id_for_previous_snapshot, git_commit_id_for_current_snapshot);
        const auto& added_ports = portsdiff.added_ports;
        if (!added_ports.empty())
        {
            msg::println(msgPortsAdded, msg::count = added_ports.size());
            for (auto&& added_port : added_ports)
            {
                print_name_and_version(added_port.port_name, added_port.version);
            }
        }

        const auto& removed_ports = portsdiff.removed_ports;
        if (!removed_ports.empty())
        {
            msg::println(msgPortsRemoved, msg::count = removed_ports.size());
            for (auto&& removed_port : removed_ports)
            {
                print_name_only(removed_port);
            }
        }

        const auto& updated_ports = portsdiff.updated_ports;
        if (!updated_ports.empty())
        {
            msg::println(msgPortsUpdated, msg::count = updated_ports.size());
            for (auto&& updated_port : updated_ports)
            {
                print_name_and_version_diff(updated_port.port_name, updated_port.version_diff);
            }
        }

        if (added_ports.empty() && removed_ports.empty() && updated_ports.empty())
        {
            msg::println(msgPortsNoDiff);
        }

        Checks::exit_success(VCPKG_LINE_INFO);
    }
} // namespace vcpkg
