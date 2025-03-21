#include <vcpkg/base/files.h>
#include <vcpkg/base/system.process.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.portsdiff.h>
#include <vcpkg/paragraphs.h>
#include <vcpkg/tools.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>
#include <vcpkg/versions.h>

using namespace vcpkg;

namespace
{
    std::string format_name_only(StringView name) { return fmt::format("\t- {:<15}\n", name); }

    std::string format_name_and_version(StringView name, const Version& version)
    {
        return fmt::format("\t- {:<15} {:<}\n", name, version);
    }

    std::string format_name_and_version_diff(StringView name, const VersionDiff& version_diff)
    {
        return fmt::format("\t- {:<15} {:<}\n", name, version_diff);
    }

    std::vector<VersionSpec> read_ports_from_commit(DiagnosticContext& context,
                                                    const VcpkgPaths& paths,
                                                    StringView git_commit_id)
    {
        auto& fs = paths.get_filesystem();
        const auto dot_git_dir = fs.try_find_file_recursively_up(paths.builtin_ports_directory().parent_path(), ".git")
                                     .map([](Path&& dot_git_parent) { return std::move(dot_git_parent) / ".git"; })
                                     .value_or_exit(VCPKG_LINE_INFO);
        const auto ports_dir_name = paths.builtin_ports_directory().filename();
        fs.create_directories(paths.buildtrees(), IgnoreErrors{});
        const auto temp_checkout_path = paths.buildtrees() / fmt::format("{}-{}", ports_dir_name, git_commit_id);
        fs.create_directory(temp_checkout_path, IgnoreErrors{});
        const auto checkout_this_dir =
            fmt::format("./{}", ports_dir_name); // Must be relative to the root of the repository

        RedirectedProcessLaunchSettings settings;
        settings.environment = get_clean_environment();
        auto maybe_checkout_output =
            cmd_execute_and_capture_output(context,
                                           paths.git_cmd_builder(dot_git_dir, temp_checkout_path)
                                               .string_arg("checkout")
                                               .string_arg(git_commit_id)
                                               .string_arg("-f")
                                               .string_arg("-q")
                                               .string_arg("--")
                                               .string_arg(checkout_this_dir)
                                               .string_arg(".vcpkg-root"),
                                           settings);
        if (check_zero_exit_code(context, maybe_checkout_output, Tools::GIT) == nullptr)
        {
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        auto maybe_reset_output = cmd_execute_and_capture_output(
            context, paths.git_cmd_builder(dot_git_dir, temp_checkout_path).string_arg("reset"), settings);
        if (check_zero_exit_code(context, maybe_reset_output, Tools::GIT) == nullptr)
        {
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        OverlayPortIndexEntry ports_at_commit_index(OverlayPortKind::Directory, temp_checkout_path / ports_dir_name);
        std::map<std::string, const SourceControlFileAndLocation*> ports_at_commit;
        auto maybe_loaded_all_ports = ports_at_commit_index.try_load_all_ports(fs, ports_at_commit);
        if (!maybe_loaded_all_ports)
        {
            context.report(DiagnosticLine{DiagKind::None, std::move(maybe_loaded_all_ports).error()});
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        fs.remove_all(temp_checkout_path, VCPKG_LINE_INFO);
        return Util::fmap(ports_at_commit,
                          [](const std::pair<const std::string, const SourceControlFileAndLocation*>& cache_entry) {
                              return cache_entry.second->source_control_file->to_version_spec();
                          });
    }

    void check_commit_exists(DiagnosticContext& context, const VcpkgPaths& paths, StringView git_commit_id)
    {
        static constexpr StringLiteral VALID_COMMIT_OUTPUT = "commit\n";
        auto maybe_cat_file_output =
            cmd_execute_and_capture_output(context,
                                           paths.git_cmd_builder(paths.root / ".git", paths.root)
                                               .string_arg("cat-file")
                                               .string_arg("-t")
                                               .string_arg(git_commit_id));
        if (auto output = check_zero_exit_code(context, maybe_cat_file_output, Tools::GIT))
        {
            if (*output != VALID_COMMIT_OUTPUT)
            {
                context.report_error(msgInvalidCommitId, msg::commit_sha = git_commit_id);
                Checks::exit_fail(VCPKG_LINE_INFO);
            }

            return;
        }

        Checks::exit_fail(VCPKG_LINE_INFO);
    }
} // unnamed namespace

namespace vcpkg
{
    PortsDiff find_portsdiff(DiagnosticContext& context,
                             const VcpkgPaths& paths,
                             StringView git_commit_id_for_previous_snapshot,
                             StringView git_commit_id_for_current_snapshot)
    {
        check_commit_exists(context, paths, git_commit_id_for_previous_snapshot);
        check_commit_exists(context, paths, git_commit_id_for_current_snapshot);

        const auto previous = read_ports_from_commit(context, paths, git_commit_id_for_previous_snapshot);
        const auto current = read_ports_from_commit(context, paths, git_commit_id_for_current_snapshot);

        auto firstPrevious = previous.begin();
        const auto lastPrevious = previous.end();

        auto firstCurrent = current.begin();
        const auto lastCurrent = current.end();

        PortsDiff result;
        for (;;)
        {
            if (firstCurrent == lastCurrent)
            {
                // all remaining previous were removed
                for (; firstPrevious != lastPrevious; ++firstPrevious)
                {
                    result.removed_ports.emplace_back(firstPrevious->port_name);
                }

                return result;
            }

            if (firstPrevious == lastPrevious)
            {
                // all remaining ports were added
                result.added_ports.insert(result.added_ports.end(), firstCurrent, lastCurrent);
                return result;
            }

            if (firstCurrent->port_name < firstPrevious->port_name)
            {
                // *firstCurrent is an added port
                result.added_ports.emplace_back(*firstCurrent);
                ++firstCurrent;
                continue;
            }

            if (firstPrevious->port_name < firstCurrent->port_name)
            {
                // *firstPrevious is a removed port
                result.removed_ports.emplace_back(firstPrevious->port_name);
                ++firstPrevious;
                continue;
            }

            if (firstCurrent->version != firstPrevious->version)
            {
                // update
                result.updated_ports.emplace_back(
                    UpdatedPort{firstPrevious->port_name, VersionDiff{firstPrevious->version, firstCurrent->version}});
            }

            // no change
            ++firstCurrent;
            ++firstPrevious;
        }
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
            parsed.command_arguments.size() < 2 ? StringLiteral{"HEAD"} : StringView{parsed.command_arguments[1]};

        if (git_commit_id_for_previous_snapshot == git_commit_id_for_current_snapshot)
        {
            msg::println(msgPortsNoDiff);
            Checks::exit_success(VCPKG_LINE_INFO);
        }

        const auto portsdiff = find_portsdiff(
            console_diagnostic_context, paths, git_commit_id_for_previous_snapshot, git_commit_id_for_current_snapshot);
        const auto& added_ports = portsdiff.added_ports;

        LocalizedString print_msg;

        if (!added_ports.empty())
        {
            print_msg.append(msgPortsAdded, msg::count = added_ports.size()).append_raw('\n');
            for (auto&& added_port : added_ports)
            {
                print_msg.append_raw(format_name_and_version(added_port.port_name, added_port.version));
            }

            print_msg.append_raw('\n');
        }

        const auto& removed_ports = portsdiff.removed_ports;
        if (!removed_ports.empty())
        {
            print_msg.append(msgPortsRemoved, msg::count = removed_ports.size()).append_raw('\n');
            for (auto&& removed_port : removed_ports)
            {
                print_msg.append_raw(format_name_only(removed_port));
            }

            print_msg.append_raw('\n');
        }

        const auto& updated_ports = portsdiff.updated_ports;
        if (!updated_ports.empty())
        {
            print_msg.append(msgPortsUpdated, msg::count = updated_ports.size()).append_raw('\n');
            for (auto&& updated_port : updated_ports)
            {
                print_msg.append_raw(format_name_and_version_diff(updated_port.port_name, updated_port.version_diff));
            }

            print_msg.append_raw('\n');
        }

        if (added_ports.empty() && removed_ports.empty() && updated_ports.empty())
        {
            print_msg.append(msgPortsNoDiff).append_raw('\n');
        }

        msg::print(print_msg);
        Checks::exit_success(VCPKG_LINE_INFO);
    }
} // namespace vcpkg
