#include <vcpkg/base/system.print.h>
#include <vcpkg/base/system.process.h>

#include <vcpkg/archives.h>
#include <vcpkg/commands.h>
#include <vcpkg/tools.h>
#include <vcpkg/vcpkgpaths.h>

namespace
{
    using namespace vcpkg;

#if defined(_WIN32)
    void win32_extract_nupkg(const VcpkgPaths& paths, const Path& archive, const Path& to_path)
    {
        const auto nuget_exe = paths.get_tool_exe(Tools::NUGET);

        const auto stem = archive.stem().to_string();
        // assuming format of [name].[version in the form d.d.d]
        // This assumption may not always hold
        std::smatch match;
        const bool has_match = std::regex_match(stem, match, std::regex{R"###(^(.+)\.(\d+\.\d+\.\d+)$)###"});
        Checks::check_exit(
            VCPKG_LINE_INFO, has_match, "Could not deduce nuget id and version from filename: %s", archive);

        const std::string nugetid = match[1];
        const std::string version = match[2];

        const auto code_and_output = cmd_execute_and_capture_output(Command{nuget_exe}
                                                                        .string_arg("install")
                                                                        .string_arg(nugetid)
                                                                        .string_arg("-Version")
                                                                        .string_arg(version)
                                                                        .string_arg("-OutputDirectory")
                                                                        .path_arg(to_path)
                                                                        .string_arg("-Source")
                                                                        .path_arg(paths.downloads)
                                                                        .string_arg("-nocache")
                                                                        .string_arg("-DirectDownload")
                                                                        .string_arg("-NonInteractive")
                                                                        .string_arg("-ForceEnglishOutput")
                                                                        .string_arg("-PackageSaveMode")
                                                                        .string_arg("nuspec"));

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
                    .path_arg(archive)
                    .string_arg("/qn")
                    // msiexec requires quotes to be after "TARGETDIR=":
                    //      TARGETDIR="C:\full\path\to\dest"
                    .raw_arg(Strings::concat("TARGETDIR=", Command{to_path}.extract())));

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
            Checks::exit_with_message(VCPKG_LINE_INFO,
                                      "msiexec failed while extracting '%s' with message:\n%s",
                                      archive,
                                      code_and_output.output);
        }
    }

    void win32_extract_with_seven_zip(const VcpkgPaths& paths, const Path& archive, const Path& to_path)
    {
        static bool recursion_limiter_sevenzip = false;
        Checks::check_exit(VCPKG_LINE_INFO, !recursion_limiter_sevenzip);
        recursion_limiter_sevenzip = true;
        const auto seven_zip = paths.get_tool_exe(Tools::SEVEN_ZIP);
        const auto code_and_output = cmd_execute_and_capture_output(Command{seven_zip}
                                                                        .string_arg("x")
                                                                        .path_arg(archive)
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
            win32_extract_with_seven_zip(paths, archive, to_path);
        }
#else
        if (ext == ".zip")
        {
            const auto code =
                cmd_execute(Command{"unzip"}.string_arg("-qqo").path_arg(archive), InWorkingDirectory{to_path});
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
}

namespace vcpkg
{
    void extract_tar(const Path& tar_tool, const Path& archive, const Path& to_path)
    {
        const auto code =
            cmd_execute(Command{tar_tool}.string_arg("xzf").path_arg(archive), InWorkingDirectory{to_path});
        Checks::check_exit(VCPKG_LINE_INFO, code == 0, "tar failed while extracting %s", archive);
    }

    Path extract_archive_to_temp_subdirectory(const VcpkgPaths& paths, const Path& archive, const Path& to_path)
    {
        Filesystem& fs = paths.get_filesystem();
        Path to_path_partial = to_path + ".partial"
#if defined(_WIN32)
                               + "." + std::to_string(GetCurrentProcessId())
#endif
            ;

        fs.remove_all(to_path_partial, VCPKG_LINE_INFO);
        fs.create_directories(to_path_partial, VCPKG_LINE_INFO);
        extract_archive_to_empty(paths, archive, to_path_partial);
        return to_path_partial;
    }

    void extract_archive(const VcpkgPaths& paths, const Path& archive, const Path& to_path)
    {
        Filesystem& fs = paths.get_filesystem();
        fs.remove_all(to_path, VCPKG_LINE_INFO);
        Path to_path_partial = extract_archive_to_temp_subdirectory(paths, archive, to_path);
        fs.rename_with_retry(to_path_partial, to_path, VCPKG_LINE_INFO);
    }
}
