#include <vcpkg/base/files.h>
#include <vcpkg/base/git.h>
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

    Optional<std::vector<VersionSpec>> read_ports_from_commit(DiagnosticContext& context,
                                                              const VcpkgPaths& paths,
                                                              const Path& git_exe,
                                                              StringView temp_name,
                                                              StringView git_commit_id)
    {
        auto& fs = paths.get_filesystem();
        const auto& builtin_ports_directory = paths.builtin_ports_directory();
        auto maybe_builtin_ports_prefix = git_prefix(context, git_exe, builtin_ports_directory);
        const auto builtin_ports_prefix = maybe_builtin_ports_prefix.get();
        if (!builtin_ports_prefix)
        {
            return nullopt;
        }

        // remove the trailing slash
        if (!builtin_ports_prefix->empty())
        {
            builtin_ports_prefix->pop_back();
        }

        const auto temp_checkout_path = paths.buildtrees() / temp_name;
        if (!git_extract_tree(context,
                              fs,
                              git_exe,
                              GitRepoLocator{GitRepoLocatorKind::CurrentDirectory, builtin_ports_directory},
                              temp_checkout_path,
                              fmt::format("{}:{}", git_commit_id, *builtin_ports_prefix)))
        {
            return nullopt;
        }

        OverlayPortIndexEntry ports_at_commit_index(OverlayPortKind::Directory, temp_checkout_path);
        std::map<std::string, const SourceControlFileAndLocation*> ports_at_commit;
        auto maybe_loaded_all_ports = ports_at_commit_index.try_load_all_ports(fs, ports_at_commit);
        if (!maybe_loaded_all_ports)
        {
            context.report(DiagnosticLine{DiagKind::None, std::move(maybe_loaded_all_ports).error()});
            return nullopt;
        }

        if (!fs.remove_all(context, temp_checkout_path))
        {
            return nullopt;
        }

        return Util::fmap(ports_at_commit,
                          [](const std::pair<const std::string, const SourceControlFileAndLocation*>& cache_entry) {
                              return cache_entry.second->source_control_file->to_version_spec();
                          });
    }

    bool check_commit_exists(DiagnosticContext& context,
                             const Path& git_exe,
                             const Path& builtin_ports_dir,
                             StringView git_commit_id)
    {
        if (!git_check_is_commit(context,
                                 git_exe,
                                 GitRepoLocator{GitRepoLocatorKind::CurrentDirectory, builtin_ports_dir},
                                 git_commit_id)
                 .value_or(false))
        {
            context.report_error(msgInvalidCommitId, msg::commit_sha = git_commit_id);
            return false;
        }

        return true;
    }
} // unnamed namespace

namespace vcpkg
{
    Optional<PortsDiff> find_portsdiff(DiagnosticContext& context,
                                       const VcpkgPaths& paths,
                                       StringView git_commit_id_for_previous_snapshot,
                                       StringView git_commit_id_for_current_snapshot)
    {
        const auto& git_exe = paths.get_tool_exe(Tools::GIT, out_sink);
        if (!check_commit_exists(context, git_exe, paths.root, git_commit_id_for_previous_snapshot) ||
            !check_commit_exists(context, git_exe, paths.root, git_commit_id_for_current_snapshot))
        {
            return nullopt;
        }

        const auto maybe_previous =
            read_ports_from_commit(context, paths, git_exe, "previous", git_commit_id_for_previous_snapshot);
        const auto previous = maybe_previous.get();
        if (!previous)
        {
            return nullopt;
        }

        const auto maybe_current =
            read_ports_from_commit(context, paths, git_exe, "current", git_commit_id_for_current_snapshot);
        const auto current = maybe_current.get();
        if (!current)
        {
            return nullopt;
        }

        auto firstPrevious = previous->begin();
        const auto lastPrevious = previous->end();

        auto firstCurrent = current->begin();
        const auto lastCurrent = current->end();

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

        const auto maybe_portsdiff = find_portsdiff(
            console_diagnostic_context, paths, git_commit_id_for_previous_snapshot, git_commit_id_for_current_snapshot);
        auto portsdiff = maybe_portsdiff.get();
        if (!portsdiff)
        {
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        const auto& added_ports = portsdiff->added_ports;

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

        const auto& removed_ports = portsdiff->removed_ports;
        if (!removed_ports.empty())
        {
            print_msg.append(msgPortsRemoved, msg::count = removed_ports.size()).append_raw('\n');
            for (auto&& removed_port : removed_ports)
            {
                print_msg.append_raw(format_name_only(removed_port));
            }

            print_msg.append_raw('\n');
        }

        const auto& updated_ports = portsdiff->updated_ports;
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
