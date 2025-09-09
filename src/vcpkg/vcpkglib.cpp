#include <vcpkg/base/files.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/util.h>

#include <vcpkg/installedpaths.h>
#include <vcpkg/metrics.h>
#include <vcpkg/paragraphs.h>
#include <vcpkg/statusparagraphs.h>
#include <vcpkg/vcpkglib.h>

namespace vcpkg
{
    static StatusParagraphs load_current_database(const ReadOnlyFilesystem& fs, const Path& vcpkg_dir_status_file)
    {
        auto pghs = Paragraphs::get_paragraphs(fs, vcpkg_dir_status_file).value_or_exit(VCPKG_LINE_INFO);

        std::vector<std::unique_ptr<StatusParagraph>> status_pghs;
        status_pghs.reserve(pghs.size());
        for (auto&& p : pghs)
        {
            status_pghs.push_back(std::make_unique<StatusParagraph>(vcpkg_dir_status_file, std::move(p)));
        }

        return StatusParagraphs(std::move(status_pghs));
    }

    static std::vector<Path> apply_database_updates(const ReadOnlyFilesystem& fs,
                                                    StatusParagraphs& current_status_db,
                                                    const Path& updates_dir)
    {
        auto update_files = fs.get_regular_files_non_recursive(updates_dir, VCPKG_LINE_INFO);
        Util::sort(update_files);
        if (!update_files.empty())
        {
            for (auto&& file : update_files)
            {
                if (file.filename() == FileIncomplete) continue;

                auto pghs = Paragraphs::get_paragraphs(fs, file).value_or_exit(VCPKG_LINE_INFO);
                for (auto&& p : pghs)
                {
                    current_status_db.insert(std::make_unique<StatusParagraph>(file, std::move(p)));
                }
            }
        }

        return update_files;
    }

    static void apply_database_updates_on_disk(const Filesystem& fs,
                                               const InstalledPaths& installed,
                                               StatusParagraphs& current_status_db)
    {
        auto update_files = apply_database_updates(fs, current_status_db, installed.vcpkg_dir_updates());
        if (!update_files.empty())
        {
            const auto status_file = installed.vcpkg_dir_status_file();
            const auto status_file_new = Path(status_file.parent_path()) / FileStatusNew;
            fs.write_contents(status_file_new, Strings::serialize(current_status_db), VCPKG_LINE_INFO);
            fs.rename(status_file_new, status_file, VCPKG_LINE_INFO);
            for (auto&& file : update_files)
            {
                fs.remove(file, VCPKG_LINE_INFO);
            }
        }
    }

    StatusParagraphs database_load(const ReadOnlyFilesystem& fs, const InstalledPaths& installed)
    {
        const auto maybe_status_file = installed.vcpkg_dir_status_file();
        if (!fs.exists(maybe_status_file, IgnoreErrors{}))
        {
            // no status file, use empty db
            StatusParagraphs current_status_db;
            (void)apply_database_updates(fs, current_status_db, installed.vcpkg_dir_updates());
            return current_status_db;
        }

        StatusParagraphs current_status_db = load_current_database(fs, maybe_status_file);
        (void)apply_database_updates(fs, current_status_db, installed.vcpkg_dir_updates());
        return current_status_db;
    }

    StatusParagraphs database_load_collapse(const Filesystem& fs, const InstalledPaths& installed)
    {
        const auto updates_dir = installed.vcpkg_dir_updates();

        fs.create_directories(installed.root(), VCPKG_LINE_INFO);
        fs.create_directory(installed.vcpkg_dir(), VCPKG_LINE_INFO);
        fs.create_directory(installed.vcpkg_dir_info(), VCPKG_LINE_INFO);
        fs.create_directory(updates_dir, VCPKG_LINE_INFO);

        const auto status_file = installed.vcpkg_dir_status_file();
        if (!fs.exists(status_file, IgnoreErrors{}))
        {
            // no status file, use empty db
            StatusParagraphs current_status_db;
            apply_database_updates_on_disk(fs, installed, current_status_db);
            return current_status_db;
        }

        StatusParagraphs current_status_db = load_current_database(fs, status_file);
        apply_database_updates_on_disk(fs, installed, current_status_db);
        return current_status_db;
    }

    void write_update(const Filesystem& fs, const InstalledPaths& installed, const StatusParagraph& p)
    {
        static std::atomic<int> update_id = 0;

        const auto my_update_id = update_id++;
        const auto update_path = installed.vcpkg_dir_updates() / fmt::format("{:010}", my_update_id);

        fs.write_rename_contents(update_path, FileIncomplete, Strings::serialize(p), VCPKG_LINE_INFO);
    }

    static bool upgrade_to_slash_terminated_sorted_format(std::vector<std::string>& lines)
    {
        static std::atomic<bool> was_tracked = false;

        if (lines.empty())
        {
            return false;
        }

        if (lines.front().back() == '/')
        {
            return false; // File already in the new format
        }

        if (!was_tracked.exchange(true))
        {
            get_global_metrics_collector().track_string(StringMetric::ListFile, "update to new format");
        }

        // The files are sorted such that directories are placed just before the files they contain
        // (They are not necessarily sorted alphabetically, e.g. libflac)
        // Therefore we can detect the entries that represent directories by comparing every element with the next one
        // and checking if the next has a slash immediately after the current one's length
        const size_t end = lines.size() - 1;
        for (size_t i = 0; i < end; i++)
        {
            std::string& current_string = lines[i];
            const std::string& next_string = lines[i + 1];
            // check if the next line is the same as this one with a slash after; that indicates that this one
            // represents a directory
            const size_t potential_slash_char_index = current_string.length();
            if (next_string.size() > potential_slash_char_index && next_string[potential_slash_char_index] == '/')
            {
                current_string += '/'; // Mark as a directory
            }
        }

        // After suffixing the directories with a slash, we can now sort.
        // We cannot sort before adding the suffixes because the following (actual example):
        /*
            x86-windows/include/FLAC <<<<<< This would be separated from its group due to sorting
            x86-windows/include/FLAC/all.h
            x86-windows/include/FLAC/assert.h
            x86-windows/include/FLAC/callback.h
            x86-windows/include/FLAC++
            x86-windows/include/FLAC++/all.h
            x86-windows/include/FLAC++/decoder.h
            x86-windows/include/FLAC++/encoder.h
         *
            x86-windows/include/FLAC/ <<<<<< This will now be kept with its group when sorting
            x86-windows/include/FLAC/all.h
            x86-windows/include/FLAC/assert.h
            x86-windows/include/FLAC/callback.h
            x86-windows/include/FLAC++/
            x86-windows/include/FLAC++/all.h
            x86-windows/include/FLAC++/decoder.h
            x86-windows/include/FLAC++/encoder.h
        */
        // Note that after sorting, the FLAC++/ group will be placed before the FLAC/ group
        // The new format is lexicographically sorted
        Util::sort(lines);
        return true;
    }

    std::vector<InstalledPackageView> get_installed_ports(const StatusParagraphs& status_db)
    {
        std::map<PackageSpec, InstalledPackageView> ipv_map;

        for (auto&& pgh : status_db)
        {
            if (!pgh->is_installed()) continue;
            auto& ipv = ipv_map[pgh->package.spec];
            if (!pgh->package.is_feature())
            {
                ipv.core = pgh.get();
            }
            else
            {
                ipv.features.emplace_back(pgh.get());
            }
        }

        for (auto&& ipv : ipv_map)
        {
            Checks::msg_check_maybe_upgrade(VCPKG_LINE_INFO, ipv.second.core != nullptr, msgCorruptedDatabase);
        }

        return Util::fmap(ipv_map, [](auto&& p) -> InstalledPackageView { return std::move(p.second); });
    }

    template<bool AndUpdate, class FilesystemLike>
    static std::vector<StatusParagraphAndAssociatedFiles> get_installed_files_impl(const FilesystemLike& fs,
                                                                                   const InstalledPaths& installed,
                                                                                   const StatusParagraphs& status_db)
    {
        std::vector<StatusParagraphAndAssociatedFiles> installed_files;

        for (const std::unique_ptr<StatusParagraph>& pgh : status_db)
        {
            if (!pgh->is_installed() || pgh->package.is_feature())
            {
                continue;
            }

            const auto listfile_path = installed.listfile_path(pgh->package);
            std::vector<std::string> installed_files_of_current_pgh =
                fs.read_lines(listfile_path).value_or_exit(VCPKG_LINE_INFO);
            Strings::inplace_trim_all_and_remove_whitespace_strings(installed_files_of_current_pgh);
            if (upgrade_to_slash_terminated_sorted_format(installed_files_of_current_pgh))
            {
                if constexpr (AndUpdate)
                {
                    // Replace the listfile on disk
                    const auto updated_listfile_path = listfile_path + "_updated";
                    fs.write_lines(updated_listfile_path, installed_files_of_current_pgh, VCPKG_LINE_INFO);
                    fs.rename(updated_listfile_path, listfile_path, VCPKG_LINE_INFO);
                }
            }

            // Remove the directories
            Util::erase_remove_if(installed_files_of_current_pgh,
                                  [](const std::string& file) { return file.back() == '/'; });

            StatusParagraphAndAssociatedFiles pgh_and_files{*pgh, std::move(installed_files_of_current_pgh)};
            installed_files.push_back(std::move(pgh_and_files));
        }

        return installed_files;
    }

    std::vector<StatusParagraphAndAssociatedFiles> get_installed_files(const ReadOnlyFilesystem& fs,
                                                                       const InstalledPaths& installed,
                                                                       const StatusParagraphs& status_db)
    {
        return get_installed_files_impl<false>(fs, installed, status_db);
    }

    std::vector<StatusParagraphAndAssociatedFiles> get_installed_files_and_upgrade(const Filesystem& fs,
                                                                                   const InstalledPaths& installed,
                                                                                   const StatusParagraphs& status_db)
    {
        return get_installed_files_impl<true>(fs, installed, status_db);
    }

    std::string shorten_text(StringView desc, const size_t length)
    {
        Checks::check_exit(VCPKG_LINE_INFO, length >= 3);
        std::string simple_desc;

        auto first = desc.begin();
        auto last = desc.end();
        for (;;)
        {
            auto next_ws = std::find_if(first, last, ParserBase::is_whitespace);
            simple_desc.append(first, next_ws);
            if (next_ws == last) break;

            simple_desc.push_back(' ');
            first = std::find_if_not(next_ws + 1, last, ParserBase::is_whitespace);
        }

        return simple_desc.size() <= length ? simple_desc : simple_desc.substr(0, length - 3) + "...";
    }
}
