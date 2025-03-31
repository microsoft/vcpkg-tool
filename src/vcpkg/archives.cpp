#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/parse.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/system.h>
#include <vcpkg/base/system.process.h>
#include <vcpkg/base/util.h>

#include <vcpkg/archives.h>
#include <vcpkg/tools.h>

namespace
{
    using namespace vcpkg;

#if defined(_WIN32)
    void win32_extract_nupkg(const ToolCache& tools, MessageSink& status_sink, const Path& archive, const Path& to_path)
    {
        const auto& nuget_exe = tools.get_tool_path(Tools::NUGET, status_sink);

        const auto stem = archive.stem();

        // assuming format of [name].[version in the form d.d.d]
        // This assumption may not always hold
        auto dot_after_name = Util::find_nth_from_last(stem, '.', 2);

        auto is_digit_or_dot = [](char ch) { return ch == '.' || ParserBase::is_ascii_digit(ch); };
        if (dot_after_name == stem.end() || !std::all_of(dot_after_name, stem.end(), is_digit_or_dot))
        {
            Checks::msg_exit_with_message(VCPKG_LINE_INFO, msgCouldNotDeduceNugetIdAndVersion, msg::path = archive);
        }

        StringView nugetid{stem.begin(), dot_after_name};
        StringView version{dot_after_name + 1, stem.end()};

        auto cmd = Command{nuget_exe}
                       .string_arg("install")
                       .string_arg(nugetid)
                       .string_arg("-Version")
                       .string_arg(version)
                       .string_arg("-OutputDirectory")
                       .string_arg(to_path)
                       .string_arg("-Source")
                       .string_arg(archive.parent_path())
                       .string_arg("-nocache")
                       .string_arg("-DirectDownload")
                       .string_arg("-NonInteractive")
                       .string_arg("-ForceEnglishOutput")
                       .string_arg("-PackageSaveMode")
                       .string_arg("nuspec");

        const auto result = flatten(cmd_execute_and_capture_output(cmd), Tools::NUGET);
        if (!result)
        {
            Checks::msg_exit_with_message(
                VCPKG_LINE_INFO,
                msg::format(msgFailedToExtract, msg::path = archive).append_raw('\n').append(result.error()));
        }
    }

    void win32_extract_msi(const Path& archive, const Path& to_path)
    {
        // MSI installation sometimes requires a global lock and fails if another installation is concurrent. Loop
        // to enable retries.
        for (unsigned int i = 0;; ++i)
        {
            // msiexec is a WIN32/GUI application, not a console application and so needs special attention to wait
            // until it finishes (wrap in cmd /c).
            auto cmd = Command{"cmd"}
                           .string_arg("/c")
                           .string_arg("msiexec")
                           // "/a" is administrative mode, which unpacks without modifying the system
                           .string_arg("/a")
                           .string_arg(archive)
                           .string_arg("/qn")
                           // msiexec requires quotes to be after "TARGETDIR=":
                           //      TARGETDIR="C:\full\path\to\dest"
                           .raw_arg(Strings::concat("TARGETDIR=", Command{to_path}.extract()));

            RedirectedProcessLaunchSettings settings;
            settings.encoding = Encoding::Utf16;
            const auto maybe_code_and_output = cmd_execute_and_capture_output(cmd, settings);
            if (auto code_and_output = maybe_code_and_output.get())
            {
                if (code_and_output->exit_code == 0)
                {
                    // Success
                    break;
                }

                if (i < 19 && code_and_output->exit_code == 1618)
                {
                    // ERROR_INSTALL_ALREADY_RUNNING
                    msg::println(msgAnotherInstallationInProgress);
                    std::this_thread::sleep_for(std::chrono::seconds(6));
                    continue;
                }
            }

            Checks::msg_exit_with_message(VCPKG_LINE_INFO, flatten(maybe_code_and_output, "msiexec").error());
        }
    }

    void win32_extract_with_seven_zip(const Path& seven_zip, const Path& archive, const Path& to_path)
    {
        static bool recursion_limiter_sevenzip = false;
        Checks::check_exit(VCPKG_LINE_INFO, !recursion_limiter_sevenzip);
        recursion_limiter_sevenzip = true;

        const auto maybe_output = flatten(cmd_execute_and_capture_output(Command{seven_zip}
                                                                             .string_arg("x")
                                                                             .string_arg(archive)
                                                                             .string_arg(fmt::format("-o{}", to_path))
                                                                             .string_arg("-y")),
                                          Tools::SEVEN_ZIP);
        if (!maybe_output)
        {
            Checks::msg_exit_with_message(
                VCPKG_LINE_INFO,
                msg::format(msgPackageFailedtWhileExtracting, msg::value = "7zip", msg::path = archive)
                    .append_raw('\n')
                    .append(maybe_output.error()));
        }

        recursion_limiter_sevenzip = false;
    }
#endif // ^^^ _WIN32

}

namespace vcpkg
{
    ExtractionType guess_extraction_type(const Path& archive)

    {
        const auto ext = archive.extension();
        if (Strings::case_insensitive_ascii_equals(ext, ".nupkg"))
        {
            return ExtractionType::Nupkg;
        }
        else if (Strings::case_insensitive_ascii_equals(ext, ".msi"))
        {
            return ExtractionType::Msi;
        }
        else if (Strings::case_insensitive_ascii_equals(ext, ".7z"))
        {
            return ExtractionType::SevenZip;
        }
        else if (Strings::case_insensitive_ascii_equals(ext, ".zip"))
        {
            return ExtractionType::Zip;
        }
        else if (ext == ".gz" || ext == ".bz2" || ext == ".tgz" || ext == ".xz")
        {
            return ExtractionType::Tar;
        }
        else if (Strings::case_insensitive_ascii_equals(ext, ".exe"))
        {
            // Special case to differentiate between self-extracting 7z archives and other exe files
            const auto stem = archive.stem();
            if (Strings::case_insensitive_ascii_equals(Path(stem).extension(), ".7z"))
            {
                return ExtractionType::SelfExtracting7z;
            }
            else
            {
                return ExtractionType::Exe;
            }
        }
        else
        {
            return ExtractionType::Unknown;
        }
    }

    void extract_archive(const Filesystem& fs,
                         const ToolCache& tools,
                         MessageSink& status_sink,
                         const Path& archive,
                         const Path& to_path)
    {
        const auto ext_type = guess_extraction_type(archive);

#if defined(_WIN32)
        switch (ext_type)
        {
            case ExtractionType::Unknown: break;
            case ExtractionType::Nupkg: win32_extract_nupkg(tools, status_sink, archive, to_path); break;
            case ExtractionType::Msi: win32_extract_msi(archive, to_path); break;
            case ExtractionType::SevenZip:
                win32_extract_with_seven_zip(tools.get_tool_path(Tools::SEVEN_ZIP_R, status_sink), archive, to_path);
                break;
            case ExtractionType::Zip:
                win32_extract_with_seven_zip(tools.get_tool_path(Tools::SEVEN_ZIP, status_sink), archive, to_path);
                break;
            case ExtractionType::Tar:
                extract_tar(tools.get_tool_path(Tools::TAR, status_sink), archive, to_path);
                break;
            case ExtractionType::Exe:
                win32_extract_with_seven_zip(tools.get_tool_path(Tools::SEVEN_ZIP, status_sink), archive, to_path);
                break;
            case ExtractionType::SelfExtracting7z:
                const Path filename = archive.filename();
                const Path stem = filename.stem();
                const Path to_archive = Path(archive.parent_path()) / stem;
                win32_extract_self_extracting_7z(fs, archive, to_archive);
                extract_archive(fs, tools, status_sink, to_archive, to_path);
                break;
        }
#else
        (void)fs;
        if (ext_type == ExtractionType::Tar)
        {
            extract_tar(tools.get_tool_path(Tools::TAR, status_sink), archive, to_path);
        }

        if (ext_type == ExtractionType::Zip)
        {
            ProcessLaunchSettings settings;
            settings.working_directory = to_path;
            const auto code = cmd_execute(Command{"unzip"}.string_arg("-qqo").string_arg(archive), settings)
                                  .value_or_exit(VCPKG_LINE_INFO);
            Checks::msg_check_exit(VCPKG_LINE_INFO,
                                   code == 0,
                                   msgPackageFailedtWhileExtracting,
                                   msg::value = "unzip",
                                   msg::path = archive);
        }

#endif
        // Try cmake for unkown extensions, i.e., vsix => zip
        if (ext_type == ExtractionType::Unknown)
        {
            extract_tar_cmake(tools.get_tool_path(Tools::CMAKE, status_sink), archive, to_path);
        }
    }

    Path extract_archive_to_temp_subdirectory(const Filesystem& fs,
                                              const ToolCache& tools,
                                              MessageSink& status_sink,
                                              const Path& archive,
                                              const Path& to_path)
    {
        Path to_path_partial = to_path + ".partial";
#if defined(_WIN32)
        to_path_partial += "." + std::to_string(GetCurrentProcessId());
#endif

        fs.remove_all(to_path_partial, VCPKG_LINE_INFO);
        fs.create_directories(to_path_partial, VCPKG_LINE_INFO);
        extract_archive(fs, tools, status_sink, archive, to_path_partial);
        return to_path_partial;
    }
#ifdef _WIN32
    void win32_extract_self_extracting_7z(const Filesystem& fs, const Path& archive, const Path& to_path)
    {
        static constexpr StringLiteral header_7z = "7z\xBC\xAF\x27\x1C";
        const Path stem = archive.stem();
        const auto subext = stem.extension();
        Checks::msg_check_exit(VCPKG_LINE_INFO,
                               Strings::case_insensitive_ascii_equals(subext, ".7z"),
                               msg::format(msgPackageFailedtWhileExtracting, msg::value = "7zip", msg::path = archive)
                                   .append(msgMissingExtension, msg::extension = ".7z.exe"));

        auto contents = fs.read_contents(archive, VCPKG_LINE_INFO);

        // try to chop off the beginning of the self extractor before the embedded 7z archive
        // some 7z self extractors, such as PortableGit-2.43.0-32-bit.7z.exe have 1 header
        // some 7z self extractors, such as 7z2408-x64.exe, have 2 headers
        auto pos = contents.find(header_7z.data(), 0, header_7z.size());
        Checks::msg_check_exit(VCPKG_LINE_INFO,
                               pos != std::string::npos,
                               msg::format(msgPackageFailedtWhileExtracting, msg::value = "7zip", msg::path = archive)
                                   .append(msgMissing7zHeader));
        // no bounds check necessary because header_7z is nonempty:
        auto pos2 = contents.find(header_7z.data(), pos + 1, header_7z.size());
        if (pos2 != std::string::npos)
        {
            pos = pos2;
        }

        StringView contents_sv = contents;
        fs.write_contents(to_path, contents_sv.substr(pos), VCPKG_LINE_INFO);
    }
#endif

    void extract_tar(const Path& tar_tool, const Path& archive, const Path& to_path)
    {
        ProcessLaunchSettings settings;
        settings.working_directory = to_path;
        const auto code = cmd_execute(Command{tar_tool}.string_arg("xzf").string_arg(archive), settings);
        Checks::msg_check_exit(VCPKG_LINE_INFO,
                               succeeded(code),
                               msgPackageFailedtWhileExtracting,
                               msg::value = "tar",
                               msg::path = archive);
    }

    void extract_tar_cmake(const Path& cmake_tool, const Path& archive, const Path& to_path)
    {
        // Note that CMake's built in tar can extract more archive types than many system tars; e.g. 7z
        ProcessLaunchSettings settings;
        settings.working_directory = to_path;
        const auto code = cmd_execute(
            Command{cmake_tool}.string_arg("-E").string_arg("tar").string_arg("xzf").string_arg(archive), settings);
        Checks::msg_check_exit(VCPKG_LINE_INFO,
                               succeeded(code),
                               msgPackageFailedtWhileExtracting,
                               msg::value = "CMake",
                               msg::path = archive);
    }

    bool ZipTool::compress_directory_to_zip(DiagnosticContext& context,
                                            const Filesystem& fs,
                                            const Path& source,
                                            const Path& destination) const
    {
        fs.remove(destination, VCPKG_LINE_INFO);
#if defined(_WIN32)
        RedirectedProcessLaunchSettings settings;
        settings.environment = get_clean_environment();
        auto& seven_zip_path = seven_zip.value_or_exit(VCPKG_LINE_INFO);
        Command seven_zip_command{seven_zip_path};
        seven_zip_command.string_arg("a").string_arg(destination).string_arg(source / "*");
        auto output = cmd_execute_and_capture_output(context, seven_zip_command, settings);
        return check_zero_exit_code(context, seven_zip_command, output) != nullptr;
#else
        RedirectedProcessLaunchSettings settings;
        settings.working_directory = source;
        Command zip_command{"zip"};
        zip_command.string_arg("--quiet")
            .string_arg("-y")
            .string_arg("-r")
            .string_arg(destination)
            .string_arg("*")
            .string_arg("--exclude")
            .string_arg(FileDotDsStore);
        auto output = cmd_execute_and_capture_output(context, zip_command, settings);
        return check_zero_exit_code(context, zip_command, output) != nullptr;
#endif
    }

    void ZipTool::setup(const ToolCache& cache, MessageSink& status_sink)
    {
#if defined(_WIN32)
        seven_zip.emplace(cache.get_tool_path(Tools::SEVEN_ZIP, status_sink));
#endif
        // Unused on non-Windows
        (void)cache;
        (void)status_sink;
    }

    Command ZipTool::decompress_zip_archive_cmd(const Path& dst, const Path& archive_path) const
    {
        Command cmd;
#if defined(_WIN32)
        cmd.string_arg(seven_zip.value_or_exit(VCPKG_LINE_INFO))
            .string_arg("x")
            .string_arg(archive_path)
            .string_arg("-o" + dst.native())
            .string_arg("-y");
#else
        cmd.string_arg("unzip")
            .string_arg("-DD")
            .string_arg("-qq")
            .string_arg(archive_path)
            .string_arg("-d" + dst.native());
#endif
        return cmd;
    }

    std::vector<ExpectedL<Unit>> decompress_in_parallel(View<Command> jobs)
    {
        RedirectedProcessLaunchSettings settings;
        settings.environment = get_clean_environment();
        auto results = cmd_execute_and_capture_output_parallel(jobs, settings);
        std::vector<ExpectedL<Unit>> filtered_results;
        filtered_results.reserve(jobs.size());
        for (std::size_t idx = 0; idx < jobs.size(); ++idx)
        {
            filtered_results.push_back(flatten(results[idx], jobs[idx].command_line()));
        }

        return filtered_results;
    }
}
