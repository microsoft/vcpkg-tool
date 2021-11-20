#include <vcpkg/base/system.print.h>

#include <vcpkg/commands.find.h>
#include <vcpkg/configure-environment.h>
#include <vcpkg/dependencies.h>
#include <vcpkg/globalstate.h>
#include <vcpkg/help.h>
#include <vcpkg/paragraphs.h>
#include <vcpkg/portfileprovider.h>
#include <vcpkg/sourceparagraph.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkglib.h>
#include <vcpkg/versiont.h>

using namespace vcpkg;
using vcpkg::PortFileProvider::PathsPortFileProvider;

namespace
{
    void do_print_json(std::vector<const vcpkg::SourceControlFile*> source_control_files)
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
    constexpr const int s_name_and_ver_columns = 41;
    void do_print(const SourceParagraph& source_paragraph, bool full_desc)
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

    void do_print(const std::string& name, const FeatureParagraph& feature_paragraph, bool full_desc)
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

    constexpr StringLiteral OPTION_FULLDESC = "x-full-desc"; // TODO: This should find a better home, eventually
    constexpr StringLiteral OPTION_JSON = "x-json";

    constexpr std::array<CommandSwitch, 2> FindSwitches = {{
        {OPTION_FULLDESC, "Do not truncate long text"},
        {OPTION_JSON, "(experimental) List libraries in JSON format"},
    }};

    const CommandStructure FindCommandStructure = {
        Strings::format("Searches for the indicated artifact or port. With no parameter after 'artifact' or 'port', "
                        "displays everything.\n%s\n%s",
                        create_example_string("find port png"),
                        create_example_string("find artifact cmake")),
        1,
        2,
        {FindSwitches, {}},
        nullptr,
    };
}

namespace vcpkg::Commands
{
    void perform_find_port_and_exit(const VcpkgPaths& paths,
                                    bool full_description,
                                    bool enable_json,
                                    Optional<StringView> filter,
                                    View<std::string> overlay_ports)
    {
        PathsPortFileProvider provider(paths, overlay_ports);
        auto source_paragraphs =
            Util::fmap(provider.load_all_control_files(),
                       [](auto&& port) -> const SourceControlFile* { return port->source_control_file.get(); });

        if (auto* filter_str = filter.get())
        {
            const auto contained_in = [filter_str](StringView haystack) {
                return Strings::case_insensitive_ascii_contains(haystack, *filter_str);
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
        else
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

        if (!enable_json)
        {
            print2("The result may be outdated. Run `git pull` to get the latest results.\n"
                   "\nIf your port is not listed, please open an issue at and/or consider making a pull request:\n"
                   "    https://github.com/Microsoft/vcpkg/issues\n");
        }

        Checks::exit_success(VCPKG_LINE_INFO);
    }

    void perform_find_artifact_and_exit(const VcpkgPaths& paths, Optional<StringView> filter)
    {
        std::vector<std::string> ce_args;
        ce_args.push_back("find");
        if (auto* filter_str = filter.get())
        {
            ce_args.emplace_back(filter_str->data(), filter_str->size());
        }

        Checks::exit_with_code(VCPKG_LINE_INFO, run_configure_environment_command(paths, ce_args));
    }

    void FindCommand::perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths) const
    {
        const ParsedArguments options = args.parse_arguments(FindCommandStructure);
        const bool full_description = Util::Sets::contains(options.switches, OPTION_FULLDESC);
        const bool enable_json = Util::Sets::contains(options.switches, OPTION_JSON);
        auto&& selector = args.command_arguments[0];
        Optional<StringView> filter;
        if (args.command_arguments.size() == 2)
        {
            filter = StringView{args.command_arguments[1]};
        }

        if (selector == "artifact")
        {
            if (full_description)
            {
                print2(Color::warning, "--%s has no effect on find artifact", OPTION_FULLDESC);
            }

            if (enable_json)
            {
                print2(Color::warning, "--%s has no effect on find artifact", OPTION_JSON);
            }

            perform_find_artifact_and_exit(paths, filter);
        }

        if (selector == "port")
        {
            perform_find_port_and_exit(paths, full_description, enable_json, filter, args.overlay_ports);
        }

        Checks::exit_with_message(VCPKG_LINE_INFO, "The first parmaeter to add must be 'artifact' or 'port'.\n");
    }
}
