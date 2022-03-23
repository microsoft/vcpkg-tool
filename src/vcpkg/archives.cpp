#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.print.h>
#include <vcpkg/base/system.process.h>

#include <vcpkg/archives.h>
#include <vcpkg/commands.h>
#include <vcpkg/tools.h>
#include <vcpkg/vcpkgpaths.h>

namespace
{
    using namespace vcpkg;

    DECLARE_AND_REGISTER_MESSAGE(
        MsiexecFailedToExtract,
        (msg::path, msg::exit_code),
        "",
        "msiexec failed while extracting '{path}' with launch or exit code {exit_code} and message:");
    DECLARE_AND_REGISTER_MESSAGE(CouldNotDeduceNugetIdAndVersion,
                                 (msg::path),
                                 "",
                                 "Could not deduce nuget id and version from filename: {path}");

#if defined(_WIN32)
    void win32_extract_nupkg(const VcpkgPaths& paths, const Path& archive, const Path& to_path)
    {
        const auto nuget_exe = paths.get_tool_exe(Tools::NUGET);

        const auto stem = archive.stem();

        // assuming format of [name].[version in the form d.d.d]
        // This assumption may not always hold
        auto dot_after_name = Util::find_nth_from_last(stem, '.', 2);

        auto is_digit_or_dot = [](char ch) { return ch == '.' || ParserBase::is_ascii_digit(ch); };
        if (dot_after_name == stem.end() || !std::all_of(dot_after_name, stem.end(), is_digit_or_dot))
        {
            Checks::msg_exit_with_message(VCPKG_LINE_INFO, msgCouldNotDeduceNugetIdAndVersion, msg::path = archive);
        }

        auto nugetid = StringView{stem.begin(), dot_after_name};
        auto version = StringView{dot_after_name + 1, stem.end()};

        Command nuget_command{nuget_exe};
        nuget_command.string_arg("install")
            .string_arg(nugetid)
            .string_arg("-Version")
            .string_arg(version)
            .string_arg("-OutputDirectory")
            .string_arg(to_path)
            .string_arg("-Source")
            .string_arg(paths.downloads)
            .string_arg("-nocache")
            .string_arg("-DirectDownload")
            .string_arg("-NonInteractive")
            .string_arg("-ForceEnglishOutput")
            .string_arg("-PackageSaveMode")
            .string_arg("nuspec");
        const auto code_and_output = cmd_execute_and_capture_output(nuget_command);

        Checks::check_exit(VCPKG_LINE_INFO,
                           code_and_output.exit_code == 0,
                           "Failed to extract '%s' with message:\n%s",
                           archive,
                           code_and_output.output);
    }

    void win32_extract_msi(const Path& archive, const Path& to_path)
    {
        // MSI installation sometimes requires a global lock and fails if another installation is concurrent. Loop
        // to enable retries.
        for (int i = 0;; ++i)
        {
            // msiexec is a WIN32/GUI application, not a console application and so needs special attention to wait
            // until it finishes (wrap in cmd /c).
            const auto code_and_output = cmd_execute_and_capture_output(
                Command{"cmd"}
                    .string_arg("/c")
                    .string_arg("msiexec")
                    // "/a" is administrative mode, which unpacks without modifying the system
                    .string_arg("/a")
                    .string_arg(archive)
                    .string_arg("/qn")
                    // msiexec requires quotes to be after "TARGETDIR=":
                    //      TARGETDIR="C:\full\path\to\dest"
                    .raw_arg(Strings::concat("TARGETDIR=", Command{to_path}.extract())),
                default_working_directory,
                default_environment,
                Encoding::Utf16);

            if (code_and_output.exit_code == 0)
            {
                // Success
                break;
            }

            // Retry up to 20 times
            if (i < 19)
            {
                if (code_and_output.exit_code == 1618)
                {
                    // ERROR_INSTALL_ALREADY_RUNNING
                    print2("Another installation is in progress on the machine, sleeping 6s before retrying.\n");
                    std::this_thread::sleep_for(std::chrono::seconds(6));
                    continue;
                }
            }
            Checks::msg_exit_with_message(
                VCPKG_LINE_INFO,
                msg::format(msgMsiexecFailedToExtract, msg::path = archive, msg::exit_code = code_and_output.exit_code)
                    .appendnl()
                    .append_raw(code_and_output.output));
        }
    }

    void win32_extract_with_seven_zip(const Path& seven_zip, const Path& archive, const Path& to_path)
    {
        static bool recursion_limiter_sevenzip = false;
        Checks::check_exit(VCPKG_LINE_INFO, !recursion_limiter_sevenzip);
        recursion_limiter_sevenzip = true;
        const auto code_and_output = cmd_execute_and_capture_output(Command{seven_zip}
                                                                        .string_arg("x")
                                                                        .string_arg(archive)
                                                                        .string_arg(Strings::format("-o%s", to_path))
                                                                        .string_arg("-y"));
        Checks::check_exit(VCPKG_LINE_INFO,
                           code_and_output.exit_code == 0,
                           "7zip failed while extracting '%s' with message:\n%s",
                           archive,
                           code_and_output.output);
        recursion_limiter_sevenzip = false;
    }
#endif // ^^^ _WIN32

    void extract_archive_to_empty(const VcpkgPaths& paths, const Path& archive, const Path& to_path)
    {
        const auto ext = archive.extension();
#if defined(_WIN32)
        if (Strings::case_insensitive_ascii_equals(ext, ".nupkg"))
        {
            win32_extract_nupkg(paths, archive, to_path);
        }
        else if (Strings::case_insensitive_ascii_equals(ext, ".msi"))
        {
            win32_extract_msi(archive, to_path);
        }
        else if (Strings::case_insensitive_ascii_equals(ext, ".zip") ||
                 Strings::case_insensitive_ascii_equals(ext, ".7z"))
        {
            extract_tar_cmake(paths.get_tool_exe(Tools::CMAKE), archive, to_path);
        }
        else if (Strings::case_insensitive_ascii_equals(ext, ".exe"))
        {
            win32_extract_with_seven_zip(paths.get_tool_exe(Tools::SEVEN_ZIP), archive, to_path);
        }
#else
        if (ext == ".zip")
        {
            const auto code =
                cmd_execute(Command{"unzip"}.string_arg("-qqo").string_arg(archive), WorkingDirectory{to_path});
            Checks::check_exit(VCPKG_LINE_INFO, code == 0, "unzip failed while extracting %s", archive);
        }
#endif
        else if (ext == ".gz" || ext == ".bz2" || ext == ".tgz")
        {
            vcpkg::extract_tar(paths.get_tool_exe(Tools::TAR), archive, to_path);
        }
        else
        {
            Checks::exit_maybe_upgrade(VCPKG_LINE_INFO, "Unexpected archive extension: %s", ext);
        }
    }

    Path extract_archive_to_temp_subdirectory(const VcpkgPaths& paths, const Path& archive, const Path& to_path)
    {
        Filesystem& fs = paths.get_filesystem();
        Path to_path_partial = to_path + ".partial";
#if defined(_WIN32)
        to_path_partial += "." + std::to_string(GetCurrentProcessId());
#endif

        fs.remove_all(to_path_partial, VCPKG_LINE_INFO);
        fs.create_directories(to_path_partial, VCPKG_LINE_INFO);
        extract_archive_to_empty(paths, archive, to_path_partial);
        return to_path_partial;
    }
}

namespace vcpkg
{
#ifdef _WIN32
    void win32_extract_bootstrap_zip(const VcpkgPaths& paths, const Path& archive, const Path& to_path)
    {
        Filesystem& fs = paths.get_filesystem();
        fs.remove_all(to_path, VCPKG_LINE_INFO);
        Path to_path_partial = to_path + ".partial." + std::to_string(GetCurrentProcessId());

        fs.remove_all(to_path_partial, VCPKG_LINE_INFO);
        fs.create_directories(to_path_partial, VCPKG_LINE_INFO);
        const auto tar_path = get_system32().value_or_exit(VCPKG_LINE_INFO) / "tar.exe";
        if (fs.exists(tar_path, IgnoreErrors{}))
        {
            // On Windows 10, tar.exe is in the box.

            // Example:
            // tar unpacks cmake unpacks 7zip unpacks git
            extract_tar(tar_path, archive, to_path_partial);
        }
        else
        {
            // On Windows <10, we attempt to use msiexec to unpack 7zip.

            // Example:
            // msiexec unpacks 7zip_msi unpacks cmake unpacks 7zip unpacks git
            win32_extract_with_seven_zip(paths.get_tool_exe(Tools::SEVEN_ZIP_MSI), archive, to_path_partial);
        }
        fs.rename_with_retry(to_path_partial, to_path, VCPKG_LINE_INFO);
    }
#endif

    void extract_tar(const Path& tar_tool, const Path& archive, const Path& to_path)
    {
        const auto code =
            cmd_execute(Command{tar_tool}.string_arg("xzf").string_arg(archive), WorkingDirectory{to_path});
        Checks::check_exit(VCPKG_LINE_INFO, code == 0, "tar failed while extracting %s", archive);
    }

    void extract_tar_cmake(const Path& cmake_tool, const Path& archive, const Path& to_path)
    {
        // Note that CMake's built in tar can extract more archive types than many system tars; e.g. 7z
        const auto code =
            cmd_execute(Command{cmake_tool}.string_arg("-E").string_arg("tar").string_arg("xzf").string_arg(archive),
                        WorkingDirectory{to_path});
        Checks::check_exit(VCPKG_LINE_INFO, code == 0, "tar failed while extracting %s", archive);
    }

    void extract_archive(const VcpkgPaths& paths, const Path& archive, const Path& to_path)
    {
        Filesystem& fs = paths.get_filesystem();
        fs.remove_all(to_path, VCPKG_LINE_INFO);
        Path to_path_partial = extract_archive_to_temp_subdirectory(paths, archive, to_path);
        fs.rename_with_retry(to_path_partial, to_path, VCPKG_LINE_INFO);
    }

    int compress_directory_to_zip(const VcpkgPaths& paths, const Path& source, const Path& destination)
    {
        auto& fs = paths.get_filesystem();
        fs.remove(destination, VCPKG_LINE_INFO);
#if defined(_WIN32)
        auto&& seven_zip_exe = paths.get_tool_exe(Tools::SEVEN_ZIP);

        return cmd_execute_and_capture_output(
                   Command{seven_zip_exe}.string_arg("a").string_arg(destination).string_arg(source / "*"),
                   default_working_directory,
                   get_clean_environment())
            .exit_code;

#else
        return cmd_execute_clean(Command{"zip"}
                                     .string_arg("--quiet")
                                     .string_arg("-y")
                                     .string_arg("-r")
                                     .string_arg(destination)
                                     .string_arg("*")
                                     .string_arg("--exclude")
                                     .string_arg(".DS_Store"),
                                 WorkingDirectory{source});
#endif
    }

    Command decompress_zip_archive_cmd(const VcpkgPaths& paths, const Path& dst, const Path& archive_path)
    {
        Command cmd;
#if defined(_WIN32)
        auto&& seven_zip_exe = paths.get_tool_exe(Tools::SEVEN_ZIP);
        cmd.string_arg(seven_zip_exe)
            .string_arg("x")
            .string_arg(archive_path)
            .string_arg("-o" + dst.native())
            .string_arg("-y");
#else
        (void)paths;
        cmd.string_arg("unzip").string_arg("-qq").string_arg(archive_path).string_arg("-d" + dst.native());
#endif
        return cmd;
    }

    std::vector<ExitCodeAndOutput> decompress_in_parallel(View<Command> jobs)
    {
        auto results =
            cmd_execute_and_capture_output_parallel(jobs, default_working_directory, get_clean_environment());
#ifdef __APPLE__
        int i = 0;
        for (auto& result : results)
        {
            if (result.exit_code == 127 && result.output.empty())
            {
                Debug::print(jobs[i].command_line(), ": pclose returned 127, try again \n");
                result = cmd_execute_and_capture_output(jobs[i], default_working_directory, get_clean_environment());
            }
            ++i;
        }
#endif
        return results;
    }
}
