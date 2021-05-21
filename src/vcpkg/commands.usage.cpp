#include <vcpkg/commands.usage.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>
#include <vcpkg/base/util.h>
#include <vcpkg/input.h>
#include <vcpkg/vcpkglib.h>

namespace vcpkg::Commands::Usage
{
    const CommandStructure COMMAND_STRUCTURE = {
        create_example_string(R"(x-usage <package>...)"),
        1,
        SIZE_MAX,
        {},
        nullptr,
    };

    void perform_and_exit(const VcpkgCmdArguments& args,
                          const VcpkgPaths& paths,
                          Triplet default_triplet,
                          Triplet)
    {
        const ParsedArguments options = args.parse_arguments(COMMAND_STRUCTURE);

        const std::vector<FullPackageSpec> specs = Util::fmap(args.command_arguments, [&](auto&& arg) {
            return Input::check_and_get_full_package_spec(
                std::string(arg), default_triplet, COMMAND_STRUCTURE.example_text);
        });

        for (const auto& spec : specs)
        {
            Input::check_triplet(spec.package_spec.triplet(), paths);
        }

        StatusParagraphs status_db = database_load_check(paths);

        if (args.json.value_or(false))
        {
            Json::Array json_to_print;
            for (const auto& spec : specs)
            {
                Json::Object& spec_object = json_to_print.push_back(Json::Object{});
                spec_object.insert("name", Json::Value::string(spec.package_spec.name()));
                spec_object.insert("triplet", Json::Value::string(spec.package_spec.triplet().to_string()));

                auto it = status_db.find_installed(spec.package_spec);
                if (it == status_db.end())
                {
                    spec_object.insert("installed", Json::Value::boolean(false));
                    continue;
                }
                spec_object.insert("installed", Json::Value::boolean(true));
                spec_object.insert("usage", to_json(get_cmake_usage((*it)->package, paths)));
            }
            System::print2(Json::stringify(json_to_print, {}));
        }
        else
        {
            for (const auto& spec : specs)
            {
                auto it = status_db.find_installed(spec.package_spec);
                if (it == status_db.end())
                {
                    System::printf(System::Color::warning, "warning: the package %s:%s is not installed; not printing usage.", spec.package_spec.name(), spec.package_spec.triplet());
                    continue;
                }
                auto usage = to_string(get_cmake_usage((*it)->package, paths));
                if (!usage.empty())
                {
                    System::print2(usage);
                }
                else
                {
                    System::printf("The package %s:%s does not have CMake usage instructions.\n", spec.package_spec.name(), spec.package_spec.triplet());
                }
            }
        }
    }

    Json::Value to_json(const CMakeUsageInfo& cmui)
    {
        auto result = Json::Object{};
        if (const std::string* usage_file = cmui.usage_file.get())
        {
            result.insert("usage-file", Json::Value::string(*usage_file));
        }
        else if (const std::string* header_path = cmui.header_to_find.get())
        {
            result.insert("header-to-find", Json::Value::string(*header_path));
        }
        else if (!cmui.cmake_targets_map.empty())
        {
            Json::Object& cmake_targets = result.insert("cmake-targets", Json::Object{});
            for (auto&& library_target_pair : cmui.cmake_targets_map)
            {
                Json::Array& targets_for_config = cmake_targets.insert(library_target_pair.first, Json::Array{});
                for (const auto& target : library_target_pair.second)
                {
                    targets_for_config.push_back(Json::Value::string(target));
                }
            }
        }
        else
        {
            return Json::Value::null(nullptr);
        }

        return Json::Value::object(std::move(result));
    }

    std::string to_string(const CMakeUsageInfo& cmui)
    {
        if (const std::string* usage_file = cmui.usage_file.get())
        {
            return Strings::concat(*usage_file, "\n");
        }
        else if (const std::string* header_path = cmui.header_to_find.get())
        {
            auto name = cmui.name;
            name = Strings::ascii_to_uppercase(Strings::replace_all(std::move(name), "-", "_"));
            if (name.empty() || Parse::ParserBase::is_ascii_digit(name[0])) name.insert(name.begin(), '_');
            auto msg = Strings::concat(
                "The package ", cmui.name, ":", cmui.triplet, " is header only and can be used from CMake via:\n\n");
            Strings::append(msg, "    find_path(", name, "_INCLUDE_DIRS \"", *header_path, "\")\n");
            Strings::append(msg, "    target_include_directories(main PRIVATE ${", name, "_INCLUDE_DIRS})\n\n");

            return msg;
        }
        else if (!cmui.cmake_targets_map.empty())
        {
            auto msg = Strings::concat("The package ", cmui.name, ":", cmui.triplet, " provides CMake targets:\n\n");

            for (auto&& library_target_pair : cmui.cmake_targets_map)
            {
                Strings::append(msg, "    find_package(", library_target_pair.first, " CONFIG REQUIRED)\n");

                auto library_target_pair_copy = library_target_pair.second;
                std::sort(library_target_pair_copy.begin(),
                            library_target_pair_copy.end(),
                            [](const std::string& l, const std::string& r) {
                                if (l.size() < r.size()) return true;
                                if (l.size() > r.size()) return false;
                                return l < r;
                            });

                if (library_target_pair_copy.size() <= 4)
                {
                    Strings::append(msg,
                                    "    target_link_libraries(main PRIVATE ",
                                    Strings::join(" ", library_target_pair.second),
                                    ")\n\n");
                }
                else
                {
                    auto omitted = library_target_pair_copy.size() - 4;
                    msg += Strings::format("    # Note: %zd target(s) were omitted.\n"
                                            "    target_link_libraries(main PRIVATE %s)\n\n",
                                            omitted,
                                            Strings::join(" ", library_target_pair.second.begin(), library_target_pair.second.end()));
                }
            }
            return msg;
        }
        else
        {
            return std::string{};
        }
    }

    CMakeUsageInfo get_cmake_usage(const BinaryParagraph& bpgh, const VcpkgPaths& paths)
    {
        static const std::regex cmake_library_regex(R"(\badd_library\(([^\$\s\)]+)\s)",
                                                    std::regex_constants::ECMAScript);

        CMakeUsageInfo ret;

        ret.name = bpgh.spec.name();
        ret.triplet = bpgh.spec.triplet();

        auto& fs = paths.get_filesystem();

        auto usage_file = paths.installed / bpgh.spec.triplet().canonical_name() / "share" / bpgh.spec.name() / "usage";
        if (fs.exists(usage_file))
        {
            auto maybe_contents = fs.read_contents(usage_file);
            if (auto p_contents = maybe_contents.get())
            {
                ret.usage_file = std::move(*p_contents);
            }
            return ret;
        }

        auto files = fs.read_lines(paths.listfile_path(bpgh));
        if (auto p_lines = files.get())
        {
            std::map<std::string, std::string> config_files;
            std::map<std::string, std::vector<std::string>> library_targets;
            bool is_header_only = true;
            std::string header_path;

            for (auto&& suffix : *p_lines)
            {
                if (Strings::case_insensitive_ascii_contains(suffix, "/share/") && Strings::ends_with(suffix, ".cmake"))
                {
                    auto filename = fs::u8string(fs::u8path(suffix).filename());
                    // CMake file is inside the share folder
                    auto path = paths.installed / suffix;
                    auto port_name = fs::u8string(path.parent_path().filename());
                    std::string find_package_name;

                    if (Strings::ends_with(filename, "Config.cmake"))
                    {
                        auto root = filename.substr(0, filename.size() - 12);
                        if (Strings::case_insensitive_ascii_equals(root, port_name))
                            find_package_name = root;
                    }
                    else if (Strings::ends_with(filename, "-config.cmake"))
                    {
                        auto root = filename.substr(0, filename.size() - 13);
                        if (Strings::case_insensitive_ascii_equals(root, port_name))
                            find_package_name = root;
                    }
                    else
                    {
                        find_package_name = port_name;
                    }

                    auto maybe_contents = fs.read_contents(path);
                    if (auto p_contents = maybe_contents.get())
                    {
                        std::sregex_iterator next(p_contents->begin(), p_contents->end(), cmake_library_regex);
                        std::sregex_iterator last;

                        if (next != last)
                        {
                            auto& targets = library_targets[find_package_name];
                            while (next != last)
                            {
                                auto match = *next;
                                if (std::find(targets.cbegin(), targets.cend(), match[1]) == targets.cend())
                                    targets.push_back(match[1]);
                                ++next;
                            }
                        }
                    }
                }
                if (Strings::case_insensitive_ascii_contains(suffix, "/lib/") ||
                    Strings::case_insensitive_ascii_contains(suffix, "/bin/"))
                {
                    if (!Strings::ends_with(suffix, ".pc") && !Strings::ends_with(suffix, "/")) is_header_only = false;
                }

                if (is_header_only && header_path.empty())
                {
                    auto it = suffix.find("/include/");
                    if (it != std::string::npos && !Strings::ends_with(suffix, "/"))
                    {
                        header_path = suffix.substr(it + 9);
                    }
                }
            }

            if (!library_targets.empty())
            {
                ret.cmake_targets_map = std::move(library_targets);
            }
            else if (is_header_only && !header_path.empty())
            {
                ret.header_to_find = header_path;
            }
        }
        return ret;
    }
}