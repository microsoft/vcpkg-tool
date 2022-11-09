#include <vcpkg/base/cofffilereader.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/hash.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.print.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.applocal.h>
#include <vcpkg/vcpkgcmdarguments.h>

#include <functional>
#include <map>
#include <string>

namespace
{
    using namespace vcpkg;

    WriteFilePointer maybe_create_log(const std::map<std::string, std::string, std::less<>>& settings,
                                      StringLiteral setting,
                                      Filesystem& fs)
    {
        const auto entry = settings.find(setting);
        if (entry == settings.end())
        {
            return WriteFilePointer();
        }

        return fs.open_for_write(entry->second, VCPKG_LINE_INFO);
    }

    struct AppLocalInvocation
    {
        AppLocalInvocation(Filesystem& fs,
                           const Path& deployment_dir,
                           const Path& installed_bin_dir,
                           WriteFilePointer&& tlog_file,
                           WriteFilePointer&& copied_files_log)
            : m_fs(fs)
            , m_deployment_dir(deployment_dir)
            , m_installed_bin_dir(fs.almost_canonical(installed_bin_dir, VCPKG_LINE_INFO))
            , m_installed_bin_parent(m_installed_bin_dir.parent_path())
            , m_tlog_file(std::move(tlog_file))
            , m_copied_files_log(std::move(copied_files_log))
        {
        }

        void resolve(const Path& binary)
        {
            vcpkg::printf("vcpkg applocal processing: %s\n", binary);
            const auto imported_names = vcpkg::read_dll_imported_dll_names(m_fs.open_for_read(binary, VCPKG_LINE_INFO));
            Debug::print("Imported DLLs of %s were %s", binary, Strings::join("\n", imported_names));
            for (auto&& imported_name : imported_names)
            {
                deploy_binary(imported_name);
            }
        }

    private:
        bool deploy_binary(const std::string& target_binary_name)
        {
            const auto source = m_installed_bin_dir / target_binary_name;
            const auto target = m_deployment_dir / target_binary_name;
            // FIXME This should use an NT Mutant (what comes out of CreateMutex) to ensure that different vcpkg.exes
            // running at the same time don't try to deploy the same file at the same time.
            std::error_code ec;
            // FIXME Should this check for last_write_time and choose latest?
            const bool did_deploy = m_fs.copy_file(source, target, CopyOptions::overwrite_existing, ec);
            if (did_deploy)
            {
                vcpkg::printf("%s -> %s done\n", source, target);
            }
            else if (!ec)
            {
                // note that we still print this as "copied" in tlog etc. because it's still a dependency
                vcpkg::printf("%s -> %s skipped, up to date\n", source, target);
            }
            else if (ec == std::errc::no_such_file_or_directory)
            {
                Debug::print("Attempted to deploy %s, but it didn't exist", source);
                return false;
            }
            else
            {
                Checks::exit_with_message(
                    VCPKG_LINE_INFO, "Failed to deploy %s -> %s; error: %s\n", source, target, ec.message());
            }

            if (m_tlog_file)
            {
                const auto as_utf16 = Strings::to_utf16(source);
                Checks::check_exit(VCPKG_LINE_INFO,
                                   m_tlog_file.write(as_utf16.data(), sizeof(wchar_t), as_utf16.size()) ==
                                       as_utf16.size());
                static constexpr wchar_t native_newline = L'\n';
                Checks::check_exit(VCPKG_LINE_INFO, m_tlog_file.write(&native_newline, sizeof(wchar_t), 1) == 1);
            }

            if (m_copied_files_log)
            {
                const auto& native = source.native();
                Checks::check_exit(VCPKG_LINE_INFO,
                                   m_copied_files_log.write(native.c_str(), 1, native.size()) == native.size());
                Checks::check_exit(VCPKG_LINE_INFO, m_copied_files_log.put('\n') == '\n');
            }

            return did_deploy;
        }

        Filesystem& m_fs;
        Path m_deployment_dir;
        Path m_installed_bin_dir;
        Path m_installed_bin_parent;
        WriteFilePointer m_tlog_file;
        WriteFilePointer m_copied_files_log;
    };
}

namespace vcpkg::Commands
{
    void AppLocalCommand::perform_and_exit(const VcpkgCmdArguments& args, Filesystem&) const
    {
        static constexpr StringLiteral OPTION_TARGET_BINARY = "target-binary";
        static constexpr StringLiteral OPTION_INSTALLED_DIR = "installed-bin-dir";
        static constexpr StringLiteral OPTION_TLOG_FILE = "tlog-file";
        static constexpr StringLiteral OPTION_COPIED_FILES_LOG = "copied-files-log";

        static constexpr CommandSetting SETTINGS[] = {
            {OPTION_TARGET_BINARY, "Path to the binary to analyze"},
            {OPTION_INSTALLED_DIR, "Path to the installed tree to use"},
            {OPTION_TLOG_FILE, "Path to the tlog file to create"},
            {OPTION_COPIED_FILES_LOG, "Path to the copied files log to create"},
        };

        const CommandStructure COMMAND_STRUCTURE = {
            "--target-binary=\"Path\\to\\binary\" --installed-bin-dir=\"Path\\to\\installed\\bin\" --tlog-file="
            "\"Path\\to\\tlog.tlog\" --copied-files-log=\"Path\\to\\copiedFilesLog.log\"",
            0,
            0,
            {{}, SETTINGS, {}},
            nullptr};

        auto& fs = get_real_filesystem();

        auto parsed = args.parse_arguments(COMMAND_STRUCTURE);
        const auto target_binary = parsed.settings.find(OPTION_TARGET_BINARY);
        Checks::check_exit(
            VCPKG_LINE_INFO, target_binary != parsed.settings.end(), "The --target-binary setting is required.");
        const auto target_installed_bin_dir = parsed.settings.find(OPTION_INSTALLED_DIR);
        Checks::check_exit(VCPKG_LINE_INFO,
                           target_installed_bin_dir != parsed.settings.end(),
                           "The --installed-bin-dir setting is required.");

        const auto target_binary_path = fs.almost_canonical(target_binary->second, VCPKG_LINE_INFO);
        AppLocalInvocation invocation(fs,
                                      target_binary_path.parent_path(),
                                      target_installed_bin_dir->second,
                                      maybe_create_log(parsed.settings, OPTION_TLOG_FILE, fs),
                                      maybe_create_log(parsed.settings, OPTION_COPIED_FILES_LOG, fs));
        invocation.resolve(target_binary_path);
        Checks::exit_success(VCPKG_LINE_INFO);
    }
}