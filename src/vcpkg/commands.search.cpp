#include <vcpkg/base/system.print.h>

#include <vcpkg/commands.search.h>
#include <vcpkg/dependencies.h>
#include <vcpkg/globalstate.h>
#include <vcpkg/help.h>
#include <vcpkg/paragraphs.h>
#include <vcpkg/portfileprovider.h>
#include <vcpkg/sourceparagraph.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkglib.h>
#include <vcpkg/versiont.h>

using vcpkg::PortFileProvider::PathsPortFileProvider;

namespace vcpkg::Commands::Search
{
    static constexpr StringLiteral OPTION_FULLDESC = "x-full-desc"; // TODO: This should find a better home, eventually
    static constexpr StringLiteral OPTION_JSON = "x-json";

    static void do_print_json(std::vector<const vcpkg::SourceControlFile*> source_control_files)
    {
        Json::Object obj;
        for (const SourceControlFile* scf : source_control_files)
        {
            auto& source_paragraph = scf->core_paragraph;
            Json::Object& library_obj = obj.insert(source_paragraph->name, Json::Object());
            library_obj.insert("package_name", Json::Value::string(source_paragraph->name));
            library_obj.insert("version", Json::Value::string(source_paragraph->version));
            library_obj.insert("port_version", Json::Value::integer(source_paragraph->port_version));
            Json::Array& desc = library_obj.insert("description", Json::Array());
            for (const auto& line : source_paragraph->description)
            {
                desc.push_back(Json::Value::string(line));
            }
        }

        print2(Json::stringify(obj, Json::JsonStyle{}));
    }
    static constexpr const int s_name_and_ver_columns = 41;
    static void do_print(const SourceParagraph& source_paragraph, bool full_desc)
    {
        auto full_version = VersionT(source_paragraph.version, source_paragraph.port_version).to_string();
        if (full_desc)
        {
            vcpkg::printf("%-20s %-16s %s\n",
                          source_paragraph.name,
                          full_version,
                          Strings::join("\n    ", source_paragraph.description));
        }
        else
        {
            std::string description;
            if (!source_paragraph.description.empty())
            {
                description = source_paragraph.description[0];
            }
            static constexpr const int name_columns = 24;
            size_t used_columns = std::max<size_t>(source_paragraph.name.size(), name_columns) + 1;
            int ver_size = std::max(0, s_name_and_ver_columns - static_cast<int>(used_columns));
            used_columns += std::max<size_t>(full_version.size(), ver_size) + 1;
            size_t description_size = used_columns < (119 - 40) ? 119 - used_columns : 40;

            vcpkg::printf("%-*s %-*s %s\n",
                          name_columns,
                          source_paragraph.name,
                          ver_size,
                          full_version,
                          vcpkg::shorten_text(description, description_size));
        }
    }

    static void do_print(const std::string& name, const FeatureParagraph& feature_paragraph, bool full_desc)
    {
        auto full_feature_name = Strings::concat(name, "[", feature_paragraph.name, "]");
        if (full_desc)
        {
            vcpkg::printf("%-37s %s\n", full_feature_name, Strings::join("\n   ", feature_paragraph.description));
        }
        else
        {
            std::string description;
            if (!feature_paragraph.description.empty())
            {
                description = feature_paragraph.description[0];
            }
            size_t desc_length =
                119 - std::min<size_t>(60, 1 + std::max<size_t>(s_name_and_ver_columns, full_feature_name.size()));
            vcpkg::printf(
                "%-*s %s\n", s_name_and_ver_columns, full_feature_name, vcpkg::shorten_text(description, desc_length));
        }
    }

    static constexpr std::array<CommandSwitch, 2> SEARCH_SWITCHES = {{
        {OPTION_FULLDESC, "Do not truncate long text"},
        {OPTION_JSON, "(experimental) List libraries in JSON format"},
    }};

    const CommandStructure COMMAND_STRUCTURE = {
        Strings::format(
            "The argument should be a substring to search for, or no argument to display all libraries.\n%s",
            create_example_string("search png")),
        0,
        1,
        {SEARCH_SWITCHES, {}},
        nullptr,
    };

    void perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        const ParsedArguments options = args.parse_arguments(COMMAND_STRUCTURE);
        const bool full_description = Util::Sets::contains(options.switches, OPTION_FULLDESC);
        const bool enable_json = Util::Sets::contains(options.switches, OPTION_JSON);

        PathsPortFileProvider provider(paths, args.overlay_ports);
        auto source_paragraphs =
            Util::fmap(provider.load_all_control_files(),
                       [](auto&& port) -> const SourceControlFile* { return port->source_control_file.get(); });

        if (args.command_arguments.empty())
        {
            if (enable_json)
            {
                do_print_json(source_paragraphs);
            }
            else
            {
                for (const auto& source_control_file : source_paragraphs)
                {
                    do_print(*source_control_file->core_paragraph, full_description);
                    for (auto&& feature_paragraph : source_control_file->feature_paragraphs)
                    {
                        do_print(source_control_file->core_paragraph->name, *feature_paragraph, full_description);
                    }
                }
            }
        }
        else
        {
            // At this point there is 1 argument
            auto&& args_zero = args.command_arguments[0];
            const auto contained_in = [&args_zero](const auto& s) {
                return Strings::case_insensitive_ascii_contains(s, args_zero);
            };
            for (const auto& source_control_file : source_paragraphs)
            {
                auto&& sp = *source_control_file->core_paragraph;

                bool found_match = contained_in(sp.name);
                if (!found_match)
                {
                    found_match = std::any_of(sp.description.begin(), sp.description.end(), contained_in);
                }

                if (found_match)
                {
                    do_print(sp, full_description);
                }

                for (auto&& feature_paragraph : source_control_file->feature_paragraphs)
                {
                    bool found_match_for_feature = found_match;
                    if (!found_match_for_feature)
                    {
                        found_match_for_feature = contained_in(feature_paragraph->name);
                    }
                    if (!found_match_for_feature)
                    {
                        found_match_for_feature = std::any_of(
                            feature_paragraph->description.begin(), feature_paragraph->description.end(), contained_in);
                    }

                    if (found_match_for_feature)
                    {
                        do_print(sp.name, *feature_paragraph, full_description);
                    }
                }
            }
        }

        if (!enable_json)
        {
            print2("The search result may be outdated. Run `git pull` to get the latest results.\n"
                   "\nIf your library is not listed, please open an issue at and/or consider making a pull request:\n"
                   "    https://github.com/Microsoft/vcpkg/issues\n");
        }

        Checks::exit_success(VCPKG_LINE_INFO);
    }

    void SearchCommand::perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths) const
    {
        Search::perform_and_exit(args, paths);
    }
}
