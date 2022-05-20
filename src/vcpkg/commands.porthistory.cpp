#include <vcpkg/base/git.h>
#include <vcpkg/base/json.h>
#include <vcpkg/base/system.print.h>
#include <vcpkg/base/system.process.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.porthistory.h>
#include <vcpkg/help.h>
#include <vcpkg/paragraphs.h>
#include <vcpkg/tools.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>
#include <vcpkg/versiondeserializers.h>
#include <vcpkg/versions.h>

namespace vcpkg::Commands::PortHistory
{
    namespace
    {
        struct HistoryVersion
        {
            std::string port_name;
            std::string git_tree;
            std::string commit_id;
            std::string commit_date;
            std::string version_string;
            std::string version;
            int port_version;
            VersionScheme scheme;
        };

        vcpkg::Optional<HistoryVersion> get_version_from_text(StringView text,
                                                              const std::string& git_tree,
                                                              const std::string& commit_id,
                                                              const std::string& commit_date,
                                                              const std::string& port_name,
                                                              bool is_manifest)
        {
            auto res = Paragraphs::try_load_port_text(text, Strings::concat(commit_id, ":", port_name), is_manifest);
            if (const auto& maybe_scf = res.get())
            {
                if (const auto& scf = maybe_scf->get())
                {
                    auto version = scf->core_paragraph->raw_version;
                    auto port_version = scf->core_paragraph->port_version;
                    auto scheme = scf->core_paragraph->version_scheme;
                    return HistoryVersion{
                        port_name,
                        git_tree,
                        commit_id,
                        commit_date,
                        Strings::concat(version, "#", port_version),
                        version,
                        port_version,
                        scheme,
                    };
                }
            }

            return nullopt;
        }

        vcpkg::Optional<HistoryVersion> get_version_from_commit(const VcpkgPaths& paths,
                                                                const std::string& commit_id,
                                                                const std::string& commit_date,
                                                                const std::string& port_name)
        {
            const auto& git_impl = paths.get_git_impl();
            const auto config = paths.git_builtin_config();
            auto rev_parse_output = git_impl.rev_parse(config, Strings::concat(commit_id, ":ports/", port_name));
            if (const auto git_tree = rev_parse_output.get())
            {
                // Do we have a manifest file?
                auto maybe_manifest = git_impl.show(config, Strings::concat(*git_tree, ":vcpkg.json"));
                if (auto content = maybe_manifest.get())
                {
                    return get_version_from_text(
                        {content->data(), content->size()}, *git_tree, commit_id, commit_date, port_name, true);
                }

                auto maybe_control = git_impl.show(config, Strings::concat(*git_tree, ":CONTROL"));
                if (auto content = maybe_control.get())
                {
                    return get_version_from_text(
                        {content->data(), content->size()}, *git_tree, commit_id, commit_date, port_name, false);
                }
            }

            return nullopt;
        }

        std::vector<HistoryVersion> read_versions_from_log(const VcpkgPaths& paths, const std::string& port_name)
        {
            auto results = paths.get_git_impl()
                               .log(paths.git_builtin_config(), Strings::format("ports/%s/.", port_name))
                               .value_or_exit(VCPKG_LINE_INFO);

            std::vector<HistoryVersion> ret;
            std::string last_version;
            for (auto&& r : results)
            {
                auto maybe_version = get_version_from_commit(paths, r.commit, r.date, port_name);
                if (maybe_version.has_value())
                {
                    const auto version = maybe_version.value_or_exit(VCPKG_LINE_INFO);

                    // Keep latest port with the current version string
                    if (last_version != version.version_string)
                    {
                        last_version = version.version_string;
                        ret.emplace_back(version);
                    }
                }
            }
            return ret;
        }
    }

    static constexpr StringLiteral OPTION_OUTPUT_FILE = "output";

    static const CommandSetting HISTORY_SETTINGS[] = {
        {OPTION_OUTPUT_FILE, "Write output to a file"},
    };

    const CommandStructure COMMAND_STRUCTURE = {
        create_example_string("history <port>"),
        1,
        1,
        {{}, {HISTORY_SETTINGS}, {}},
        nullptr,
    };

    static Optional<std::string> maybe_lookup(std::map<std::string, std::string, std::less<>> const& m, StringView key)
    {
        const auto it = m.find(key);
        if (it != m.end()) return it->second;
        return nullopt;
    }

    void perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        const ParsedArguments parsed_args = args.parse_arguments(COMMAND_STRUCTURE);
        auto maybe_output_file = maybe_lookup(parsed_args.settings, OPTION_OUTPUT_FILE);

        std::string port_name = args.command_arguments.at(0);
        std::vector<HistoryVersion> versions = read_versions_from_log(paths, port_name);

        if (args.output_json())
        {
            Json::Array versions_json;
            for (auto&& version : versions)
            {
                Json::Object object;
                object.insert("git-tree", Json::Value::string(version.git_tree));

                serialize_schemed_version(object, version.scheme, version.version, version.port_version, true);
                versions_json.push_back(std::move(object));
            }

            Json::Object root;
            root.insert("versions", versions_json);

            auto json_string = Json::stringify(root, vcpkg::Json::JsonStyle::with_spaces(2));

            if (maybe_output_file.has_value())
            {
                auto output_file_path = maybe_output_file.value_or_exit(VCPKG_LINE_INFO);
                auto& fs = paths.get_filesystem();
                fs.write_contents(output_file_path, json_string, VCPKG_LINE_INFO);
            }
            else
            {
                vcpkg::printf("%s\n", json_string);
            }
        }
        else
        {
            if (maybe_output_file.has_value())
            {
                vcpkg::printf(Color::warning, "Warning: Option `--$s` requires `--x-json` switch.", OPTION_OUTPUT_FILE);
            }

            print2("             version          date    vcpkg commit\n");
            for (auto&& version : versions)
            {
                vcpkg::printf("%20.20s    %s    %s\n", version.version_string, version.commit_date, version.commit_id);
            }
        }
        Checks::exit_success(VCPKG_LINE_INFO);
    }

    void PortHistoryCommand::perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths) const
    {
        PortHistory::perform_and_exit(args, paths);
    }
}
