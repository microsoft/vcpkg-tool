#include <vcpkg/base/files.h>
#include <vcpkg/base/hash.h>
#include <vcpkg/base/span.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.find.h>
#include <vcpkg/configure-environment.h>
#include <vcpkg/documentation.h>
#include <vcpkg/metrics.h>
#include <vcpkg/portfileprovider.h>
#include <vcpkg/registries.h>
#include <vcpkg/sourceparagraph.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkglib.h>
#include <vcpkg/vcpkgpaths.h>

using namespace vcpkg;

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
            library_obj.insert("version", Json::Value::string(source_paragraph->version.text));
            library_obj.insert("port_version", Json::Value::integer(source_paragraph->version.port_version));
            Json::Array& desc = library_obj.insert("description", Json::Array());
            for (const auto& line : source_paragraph->description)
            {
                desc.push_back(Json::Value::string(line));
            }
        }

        msg::write_unlocalized_text_to_stdout(Color::none, Json::stringify(obj));
    }
    constexpr const int s_name_and_ver_columns = 41;
    void do_print(const SourceParagraph& source_paragraph, bool full_desc)
    {
        auto full_version = source_paragraph.version.to_string();
        if (full_desc)
        {
            msg::write_unlocalized_text_to_stdout(Color::none,
                                                  fmt::format("{:20} {:16} {}\n",
                                                              source_paragraph.name,
                                                              full_version,
                                                              Strings::join("\n    ", source_paragraph.description)));
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

            msg::write_unlocalized_text_to_stdout(Color::none,
                                                  fmt::format("{1:{0}} {3:{2}} {4}\n",
                                                              name_columns,
                                                              source_paragraph.name,
                                                              ver_size,
                                                              full_version,
                                                              vcpkg::shorten_text(description, description_size)));
        }
    }

    void do_print(const std::string& name, const FeatureParagraph& feature_paragraph, bool full_desc)
    {
        auto full_feature_name = Strings::concat(name, "[", feature_paragraph.name, "]");
        if (full_desc)
        {
            msg::write_unlocalized_text_to_stdout(
                Color::none,
                fmt::format("{:37} {}\n", full_feature_name, Strings::join("\n   ", feature_paragraph.description)));
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
            msg::write_unlocalized_text_to_stdout(Color::none,
                                                  fmt::format("{1:{0}} {2}\n",
                                                              s_name_and_ver_columns,
                                                              full_feature_name,
                                                              vcpkg::shorten_text(description, desc_length)));
        }
    }

    constexpr StringLiteral OPTION_FULLDESC = "x-full-desc"; // TODO: This should find a better home, eventually
    constexpr StringLiteral OPTION_JSON = "x-json";

    constexpr CommandSwitch FindSwitches[] = {
        {OPTION_FULLDESC, msgHelpTextOptFullDesc},
        {OPTION_JSON, msgJsonSwitch},
    };

    void perform_find_artifact_and_exit(const VcpkgPaths& paths,
                                        Optional<StringView> filter,
                                        Optional<StringView> version)
    {
        std::vector<std::string> ce_args;
        ce_args.emplace_back("find");
        if (auto* filter_str = filter.get())
        {
            ce_args.emplace_back(filter_str->data(), filter_str->size());
        }

        if (auto v = version.get())
        {
            ce_args.emplace_back("--version");
            ce_args.emplace_back(*v);
        }

        Checks::exit_with_code(VCPKG_LINE_INFO, run_configure_environment_command(paths, ce_args));
    }
} // unnamed namespace

namespace vcpkg
{
    void perform_find_port_and_exit(const VcpkgPaths& paths,
                                    bool full_description,
                                    bool enable_json,
                                    Optional<StringView> filter,
                                    View<std::string> overlay_ports)
    {
        Checks::check_exit(VCPKG_LINE_INFO, msg::default_output_stream == OutputStream::StdErr);
        auto& fs = paths.get_filesystem();
        auto registry_set = paths.make_registry_set();
        PathsPortFileProvider provider(fs, *registry_set, make_overlay_provider(fs, paths.original_cwd, overlay_ports));
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
        else if (enable_json)
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
                    do_print(source_control_file->to_name(), *feature_paragraph, full_description);
                }
            }
        }

        msg::println(msg::format(msgSuggestGitPull)
                         .append_raw('\n')
                         .append(msgMissingPortSuggestPullRequest)
                         .append_indent()
                         .append_raw("-  https://github.com/Microsoft/vcpkg/issues"));

        Checks::exit_success(VCPKG_LINE_INFO);
    }

    constexpr CommandMetadata CommandFindMetadata{
        "find",
        msgCmdFindSynopsis,
        {
            msgCmdFindExample1,
            "vcpkg find port png",
            msgCmdFindExample2,
            "vcpkg find artifact cmake",
        },
        Undocumented,
        AutocompletePriority::Public,
        1,
        2,
        {FindSwitches, CommonSelectArtifactVersionSettings},
        nullptr,
    };

    void command_find_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        msg::default_output_stream = OutputStream::StdErr;
        const ParsedArguments options = args.parse_arguments(CommandFindMetadata);
        const bool full_description = Util::Sets::contains(options.switches, OPTION_FULLDESC);
        const bool enable_json = Util::Sets::contains(options.switches, OPTION_JSON);
        auto&& selector = options.command_arguments[0];
        Optional<StringView> filter;
        if (options.command_arguments.size() == 2)
        {
            filter = StringView{options.command_arguments[1]};
        }

        if (selector == "artifact")
        {
            if (full_description)
            {
                msg::write_unlocalized_text_to_stderr(
                    Color::warning,
                    msg::format_warning(msgArtifactsOptionIncompatibility, msg::option = OPTION_FULLDESC)
                        .append_raw('\n'));
            }

            if (enable_json)
            {
                msg::write_unlocalized_text_to_stderr(
                    Color::warning,
                    msg::format_warning(msgArtifactsOptionIncompatibility, msg::option = "x-json").append_raw('\n'));
            }

            Optional<std::string> filter_hash = filter.map(Hash::get_string_sha256);
            MetricsSubmission metrics;
            metrics.track_string(StringMetric::CommandContext, "artifact");
            if (auto p_filter_hash = filter_hash.get())
            {
                metrics.track_string(StringMetric::CommandArgs, *p_filter_hash);
            }

            get_global_metrics_collector().track_submission(std::move(metrics));
            perform_find_artifact_and_exit(paths, filter, Util::lookup_value(options.settings, OPTION_VERSION));
        }

        if (selector == "port")
        {
            if (Util::Maps::contains(options.settings, OPTION_VERSION))
            {
                Checks::msg_exit_with_error(VCPKG_LINE_INFO, msgFindVersionArtifactsOnly);
            }

            Optional<std::string> filter_hash = filter.map(Hash::get_string_sha256);
            MetricsSubmission metrics;
            metrics.track_string(StringMetric::CommandContext, "port");
            if (auto p_filter_hash = filter_hash.get())
            {
                metrics.track_string(StringMetric::CommandArgs, *p_filter_hash);
            }

            get_global_metrics_collector().track_submission(std::move(metrics));
            perform_find_port_and_exit(paths, full_description, enable_json, filter, paths.overlay_ports);
        }

        Checks::msg_exit_with_error(VCPKG_LINE_INFO, msg::format(msgAddCommandFirstArg).append_raw('\n').append(msgSeeURL, msg::url = docs::add_command_url));
    }
} // namespace vcpkg
