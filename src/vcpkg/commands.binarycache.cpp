#include <vcpkg/base/checks.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/system.process.h>
#include <vcpkg/base/util.h>

#include <vcpkg/archives.h>
#include <vcpkg/binarycaching.h>
#include <vcpkg/commands.binarycache.h>
#include <vcpkg/paragraphs.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

#include <set>

using namespace vcpkg;

namespace
{
    const CommandStructure BinaryCacheCommandStructure = {
        []() {
            return LocalizedString::from_raw(fmt::format(
                "Adds the indicated port or artifact to the manifest associated with the current directory.\n%s\n%s",
                create_example_string("binarycache list"),
                create_example_string("binarycache remove --outdated-versions")));
        },
        1,
        INT_MAX,
        {{}, {}},
        nullptr,
    };

    using AbiEntries = std::map<std::string, std::string>;

    AbiEntries parse_abi_info(const std::string& content)
    {
        AbiEntries entries;
        for (auto&& line : Strings::split(content, '\n'))
        {
            auto index = line.find(" ");
            Checks::check_exit(VCPKG_LINE_INFO, index != std::string::npos);
            entries.emplace(line.substr(0, index), line.substr(index + 1));
        }
        return entries;
    }

    struct BinaryPackageInfo
    {
        PackageSpec spec;
        std::string version;
        int port_version = 0;
        std::vector<std::string> features;
        std::string abi;
        AbiEntries abi_entries;

        std::string full_name() const
        {
            std::string f = features.empty() ? "" : '[' + Strings::join(",", features) + ']';
            return Strings::concat(spec.name(), f, spec.triplet(), " -> ", version, '#', port_version);
        }
    };

    BinaryPackageInfo from(std::vector<BinaryParagraph> pghs)
    {
        Checks::check_exit(VCPKG_LINE_INFO, pghs.size() >= 1);
        const auto& p = pghs.front();
        BinaryPackageInfo info{p.spec, p.version, p.port_version, {}, p.abi};
        for (auto iter = pghs.begin() + 1; iter != pghs.end(); ++iter)
        {
            info.features.push_back(iter->feature);
        }
        return info;
    }

    Path get_filename(const std::string& abi, const std::string& ending)
    {
        return Path(abi.substr(0, 2)) / Path(abi + ending);
    }

    void delete_package(Filesystem& fs, const Path& root_dir, const std::string& abi)
    {
        fs.remove(root_dir / get_filename(abi, ".zip"), VCPKG_LINE_INFO);
        fs.remove_all(root_dir / get_filename(abi, "_files"), IgnoreErrors{});
    }

    std::map<std::string, std::vector<const BinaryPackageInfo*>> create_reverse_dependency_graph(
        const std::vector<BinaryPackageInfo>& infos)
    {
        std::set<std::string> port_names;
        for (const auto& info : infos)
        {
            port_names.insert(info.spec.name());
        }
        std::map<std::string, std::vector<const BinaryPackageInfo*>> map;
        for (const auto& info : infos)
        {
            for (auto& e : info.abi_entries)
            {
                if (port_names.find(e.first) != port_names.end())
                {
                    map[e.second].push_back(&info);
                }
            }
        }
        return map;
    }

    template<typename F>
    void remove_recusive(std::map<std::string, std::vector<const BinaryPackageInfo*>>& map,
                         const std::string& abi,
                         F func)
    {
        auto iter = map.find(abi);
        if (iter == map.end())
        {
            func(abi);
            return;
        }
        auto info = iter->second;
        map.erase(iter);
        func(abi);
        for (const auto& dep : info)
        {
            remove_recusive(map, dep->abi, func);
        }
    }

    std::vector<BinaryPackageInfo> read_path(const VcpkgPaths& paths, const Path& root_dir)
    {
        const Filesystem& fs = paths.get_filesystem();
        std::vector<Path> control_files;
        std::vector<std::string> missing_abi_hashes;
        for (Path& path : fs.get_regular_files_recursive(root_dir, VCPKG_LINE_INFO))
        {
            if (path.extension() == ".zip")
            {
                StringView filename = path.filename();
                if (filename.size() == 68)
                {
                    auto abi_hash = filename.substr(0, 64).to_string();
                    control_files.push_back(Path(path.parent_path()) / Path(abi_hash + "_files") / "CONTROL");
                    if (!fs.exists(control_files.back(), VCPKG_LINE_INFO))
                    {
                        missing_abi_hashes.push_back(std::move(abi_hash));
                    }
                }
            }
        }
        if (!missing_abi_hashes.empty())
        {
            std::vector<Command> jobs;
            jobs.reserve(missing_abi_hashes.size());
            for (auto&& abi_hash : missing_abi_hashes)
            {
                jobs.push_back(extract_files_command(paths,
                                                     root_dir / get_filename(abi_hash, ".zip"),
                                                     {"CONTROL", "share/*/vcpkg_abi_info.txt"},
                                                     root_dir / get_filename(abi_hash, "_files")));
            }
            msg::write_unlocalized_text_to_stdout(
                Color::none, Strings::concat("Extracting ", missing_abi_hashes.size(), " archives..."));
            decompress_in_parallel(VCPKG_LINE_INFO, jobs);
            msg::write_unlocalized_text_to_stdout(Color::none, " Done.\n");
        }

        std::vector<BinaryPackageInfo> output;
        for (auto&& path : control_files)
        {
            const auto pghss = Paragraphs::get_paragraphs(fs, path);
            if (const auto p = pghss.get())
            {
                auto paras = Util::fmap(*p, [](auto& paragraph) { return BinaryParagraph(paragraph); });
                auto info = from(paras);
                info.abi_entries = parse_abi_info(fs.read_contents(
                    Path(path.parent_path()) / "share" / info.spec.name() / "vcpkg_abi_info.txt", VCPKG_LINE_INFO));
                output.push_back(std::move(info));
            }
        }
        return output;
    }

    std::vector<std::vector<std::pair<std::string, std::string>>> find_differences(std::vector<AbiEntries> abi_entries)
    {
        std::vector<std::vector<std::pair<std::string, std::string>>> differences;
        std::vector<std::string> current_values;
        differences.resize(abi_entries.size());
        current_values.resize(abi_entries.size());
        size_t outer_i = 0;
        for (auto& entries : abi_entries)
        {
            for (auto entry_iter = entries.begin(); entry_iter != entries.end(); ++entry_iter)
            {
                const auto key = entry_iter->first;
                const auto& value = current_values[outer_i] = entry_iter->second;
                bool same = true;
                size_t i = 0;
                for (auto& e : abi_entries)
                {
                    if (i != outer_i)
                    {
                        auto iter = e.find(key);
                        if (iter == e.end())
                        {
                            current_values[i] = "None";
                            same = false;
                        }
                        else
                        {
                            same = same && iter->second == value;
                            current_values[i] = std::move(iter->second);
                            e.erase(iter);
                        }
                    }
                    ++i;
                }
                if (!same)
                {
                    for (i = 0; i < current_values.size(); ++i)
                    {
                        differences[i].emplace_back(key, current_values[i]);
                    }
                }
            }
            entries.clear();
            ++outer_i;
        }
        return differences;
    }

    std::map<std::string, const BinaryPackageInfo&> create_abi_map(const std::vector<BinaryPackageInfo>& ports)
    {
        std::map<std::string, const BinaryPackageInfo&> map;
        for (auto& port : ports)
        {
            map.emplace(port.abi, port);
        }
        return map;
    }
}

namespace vcpkg
{
    namespace Commands::Binarycache
    {

        void BinaryCacheCommand::perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths) const
        {
            args.parse_arguments(BinaryCacheCommandStructure);
            auto&& selector = args.command_arguments[0];
            if (selector == "list")
            {
                auto ports = read_path(paths, default_cache_path().value_or_exit(VCPKG_LINE_INFO));
                std::sort(ports.begin(), ports.end(), [](const auto& l, const auto& r) {
                    return l.full_name() < r.full_name();
                });
                std::map<std::string, std::string> port_versions;
                for (const auto& port : ports)
                {
                    port_versions[port.abi] = port.full_name();
                }
                if (args.command_arguments.size() >= 2)
                {
                    Util::erase_remove_if(
                        ports, [&](const BinaryPackageInfo& i) { return i.spec.name() != args.command_arguments[1]; });
                }
                std::map<std::string, std::vector<const BinaryPackageInfo*>> map;
                Util::group_by(ports, &map, [](const auto& port) { return port.full_name(); });
                for (auto& e : map)
                {
                    auto abi_entries = Util::fmap(e.second, [](auto e) { return e->abi_entries; });
                    auto differences = find_differences(abi_entries);
                    msg::write_unlocalized_text_to_stdout(Color::none, Strings::concat(e.first, "\n"));
                    size_t i = 0;
                    for (auto& difference : differences)
                    {
                        auto diff = Util::fmap(difference, [&](auto& diff) {
                            auto version_iter = port_versions.find(diff.second);
                            if (version_iter == port_versions.end())
                                return Strings::concat("    ", diff.first, ": ", diff.second);
                            return Strings::concat("    ", diff.first, ": ", version_iter->second, " ", diff.second);
                        });
                        msg::write_unlocalized_text_to_stdout(
                            Color::none,
                            Strings::concat("  Version: ", e.second[i]->abi, "\n", Strings::join("\n", diff), "\n\n"));
                        ++i;
                    }
                }

                /*for (const auto& e : ports)
                {
                    auto port = e.first;
                    print2(port.spec.name(),
                           ":",
                           port.spec.triplet(),
                           " -> ",
                           port.version,
                           "#",
                           port.port_version,
                           //" ",
                           // Strings::replace_all(e.second, "\n", "\n    "),
                           "\n");
                }*/
                Checks::exit_with_code(VCPKG_LINE_INFO, 0);
            }
            else if (selector == "remove-recursive")
            {
                Checks::check_exit(
                    VCPKG_LINE_INFO, args.command_arguments.size() > 1, "You must provide a hash of a binary package");
                auto ports = read_path(paths, default_cache_path().value_or_exit(VCPKG_LINE_INFO));
                auto graph = create_reverse_dependency_graph(ports);
                const auto abi_names = create_abi_map(ports);
                for (size_t i = 1; i < args.command_arguments.size(); ++i)
                {
                    remove_recusive(graph, args.command_arguments[i], [&](const auto& abi) {
                        auto port = abi_names.find(abi);
                        auto name = (port != abi_names.end()) ? port->second.full_name() : "unknown";
                        msg::write_unlocalized_text_to_stdout(Color::none,
                                                              Strings::concat("Delete package ", name, " ", abi, "\n"));

                        delete_package(
                            paths.get_filesystem(), default_cache_path().value_or_exit(VCPKG_LINE_INFO), abi);
                    });
                }
                Checks::exit_with_code(VCPKG_LINE_INFO, 0);
            }
            else if (selector == "remove-with-key")
            {
                Checks::check_exit(VCPKG_LINE_INFO,
                                   args.command_arguments.size() == 3,
                                   "You must provide a key and a value, for example cmake 21.1.1");
                auto ports = read_path(paths, default_cache_path().value_or_exit(VCPKG_LINE_INFO));
                for (auto& port : ports)
                {
                    auto iter = port.abi_entries.find(args.command_arguments[1]);
                    if (iter != port.abi_entries.end() && iter->second == args.command_arguments[2])
                    {
                        msg::write_unlocalized_text_to_stdout(
                            Color::none, Strings::concat("Delete package ", port.full_name(), "\n"));
                        delete_package(
                            paths.get_filesystem(), default_cache_path().value_or_exit(VCPKG_LINE_INFO), port.abi);
                    }
                }
                Checks::exit_with_code(VCPKG_LINE_INFO, 0);
            }

            Checks::exit_with_message(VCPKG_LINE_INFO, "The first parmaeter to add must be 'artifact' or 'port'.\n");
        }
    }
}
