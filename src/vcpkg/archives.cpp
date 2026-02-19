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

    bool postprocess_extract_archive(DiagnosticContext& context,
                                     Optional<ExitCodeAndOutput>&& maybe_exit_and_output,
                                     const Path& tool,
                                     const Path& archive)
    {
        if (auto* exit_and_output = maybe_exit_and_output.get())
        {
            if (exit_and_output->exit_code == 0)
            {
                return true;
            }

            auto error_text =
                msg::format(msgArchiverFailedToExtractExitCode, msg::exit_code = exit_and_output->exit_code);
            error_text.append_raw('\n');
            error_text.append_raw(std::move(exit_and_output->output));
            context.report(DiagnosticLine{DiagKind::Error, tool, std::move(error_text)});
            context.report(DiagnosticLine{DiagKind::Note, archive, msg::format(msgArchiveHere)});
        }
        else
        {
            context.report(DiagnosticLine{DiagKind::Note, archive, msg::format(msgWhileExtractingThisArchive)});
        }

        return false;
    }

#if defined(_WIN32)
    bool win32_extract_nupkg(DiagnosticContext& context,
                             const Filesystem& fs,
                             const ToolCache& tools,
                             const Path& archive,
                             const Path& to_path)
    {
        const auto* nuget_exe = tools.get_tool_path(context, fs, Tools::NUGET);
        if (!nuget_exe)
        {
            return false;
        }

        const auto stem = archive.stem();

        // assuming format of [name].[version in the form d.d.d]
        // This assumption may not always hold
        auto dot_after_name = Util::find_nth_from_last(stem, '.', 2);

        auto is_digit_or_dot = [](char ch) { return ch == '.' || ParserBase::is_ascii_digit(ch); };
        if (dot_after_name == stem.end() || !std::all_of(dot_after_name, stem.end(), is_digit_or_dot))
        {
            context.report(DiagnosticLine{DiagKind::Error, archive, msg::format(msgCouldNotDeduceNuGetIdAndVersion2)});
            return false;
        }

        StringView nugetid{stem.begin(), dot_after_name};
        StringView version{dot_after_name + 1, stem.end()};

        auto cmd = Command{*nuget_exe}
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

        Optional<ExitCodeAndOutput> maybe_exit_and_output = cmd_execute_and_capture_output(context, cmd);
        return postprocess_extract_archive(context, std::move(maybe_exit_and_output), *nuget_exe, archive);
    }

    bool win32_extract_msi(DiagnosticContext& context, const Path& archive, const Path& to_path)
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

        // MSI installation sometimes requires a global lock and fails if another installation is concurrent. Loop
        // to enable retries.

        AttemptDiagnosticContext adc{context};
        for (unsigned int i = 0;; ++i)
        {
            auto maybe_code_and_output = cmd_execute_and_capture_output(adc, cmd, settings);
            if (auto code_and_output = maybe_code_and_output.get())
            {
                if (code_and_output->exit_code == 0)
                {
                    // Success
                    adc.handle();
                    return true;
                }

                if (i < 19 && code_and_output->exit_code == 1618)
                {
                    // ERROR_INSTALL_ALREADY_RUNNING
                    adc.statusln(msg::format(msgAnotherInstallationInProgress));
                    std::this_thread::sleep_for(std::chrono::seconds(6));
                    continue;
                }

                auto error_text =
                    msg::format(msgArchiverFailedToExtractExitCode, msg::exit_code = code_and_output->exit_code);
                error_text.append_raw('\n');
                error_text.append_raw(std::move(code_and_output->output));
                adc.report(DiagnosticLine{DiagKind::Error, "msiexec", std::move(error_text)});
                adc.report(DiagnosticLine{DiagKind::Note, archive, msg::format(msgArchiveHere)});
            }
            else
            {
                adc.report(DiagnosticLine{DiagKind::Note, archive, msg::format(msgWhileExtractingThisArchive)});
            }

            adc.commit();
            return false;
        }
    }

    bool win32_extract_with_seven_zip(DiagnosticContext& context,
                                      const Path& seven_zip,
                                      const Path& archive,
                                      const Path& to_path)
    {
        static bool recursion_limiter_sevenzip = false;
        Checks::check_exit(VCPKG_LINE_INFO, !recursion_limiter_sevenzip);
        recursion_limiter_sevenzip = true;
        auto maybe_exit_and_output = cmd_execute_and_capture_output(context,
                                                                    Command{seven_zip}
                                                                        .string_arg("x")
                                                                        .string_arg(archive)
                                                                        .string_arg(fmt::format("-o{}", to_path))
                                                                        .string_arg("-y"));
        recursion_limiter_sevenzip = false;
        return postprocess_extract_archive(context, std::move(maybe_exit_and_output), seven_zip, archive);
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

    bool extract_archive(DiagnosticContext& context,
                         const Filesystem& fs,
                         const ToolCache& tools,
                         const Path& archive,
                         const Path& to_path)
    {
        const auto ext_type = guess_extraction_type(archive);

#if defined(_WIN32)
        switch (ext_type)
        {
            case ExtractionType::Unknown: break; // try cmake for unkown extensions, i.e., vsix => zip, below
            case ExtractionType::Nupkg: return win32_extract_nupkg(context, fs, tools, archive, to_path);
            case ExtractionType::Msi: return win32_extract_msi(context, archive, to_path); break;
            case ExtractionType::SevenZip:
                if (const auto* tool = tools.get_tool_path(context, fs, Tools::SEVEN_ZIP_R))
                {
                    return win32_extract_with_seven_zip(context, *tool, archive, to_path);
                }

                return false;
            case ExtractionType::Zip:
                if (const auto* tool = tools.get_tool_path(context, fs, Tools::SEVEN_ZIP))
                {
                    return win32_extract_with_seven_zip(context, *tool, archive, to_path);
                }

                return false;
            case ExtractionType::Tar:
                if (const auto* tool = tools.get_tool_path(context, fs, Tools::TAR))
                {
                    return extract_tar(context, *tool, archive, to_path);
                }

                return false;
            case ExtractionType::Exe:
                if (const auto* tool = tools.get_tool_path(context, fs, Tools::SEVEN_ZIP))
                {
                    return win32_extract_with_seven_zip(context, *tool, archive, to_path);
                }

                return false;
            case ExtractionType::SelfExtracting7z:
            {
                const Path filename = archive.filename();
                const Path stem = filename.stem();
                const Path to_archive = Path(archive.parent_path()) / stem;
                if (!win32_extract_self_extracting_7z(context, fs, archive, to_archive))
                {
                    return false;
                }

                return extract_archive(context, fs, tools, to_archive, to_path);
            }

            default: Checks::unreachable(VCPKG_LINE_INFO);
        }
#else
        if (ext_type == ExtractionType::Tar)
        {
            if (const auto* tool = tools.get_tool_path(context, fs, Tools::TAR))
            {
                return extract_tar(context, *tool, archive, to_path);
            }

            return false;
        }

        if (ext_type == ExtractionType::Zip)
        {
            auto cmd = Command{"unzip"}.string_arg("-qqo").string_arg(archive);
            RedirectedProcessLaunchSettings settings;
            settings.working_directory = to_path;
            auto maybe_code_and_output = cmd_execute_and_capture_output(context, cmd, settings);
            if (!check_zero_exit_code(context, cmd, maybe_code_and_output))
            {
                context.report(DiagnosticLine{DiagKind::Note, archive, msg::format(msgWhileExtractingThisArchive)});
                return false;
            }

            return true;
        }
#endif

        // try cmake for unkown extensions, i.e., vsix => zip
        if (ext_type == ExtractionType::Unknown)
        {
            if (const auto* tool = tools.get_tool_path(context, fs, Tools::CMAKE))
            {
                return extract_tar_cmake(context, *tool, archive, to_path);
            }

            return false;
        }

        return false;
    }

    Optional<Path> extract_archive_to_temp_subdirectory(DiagnosticContext& context,
                                                        const Filesystem& fs,
                                                        const ToolCache& tools,
                                                        const Path& archive,
                                                        const Path& to_path)
    {
        Path to_path_partial = to_path + ".partial";
#if defined(_WIN32)
        to_path_partial += "." + std::to_string(GetCurrentProcessId());
#endif

        fs.remove_all(to_path_partial, VCPKG_LINE_INFO);
        fs.create_directories(to_path_partial, VCPKG_LINE_INFO);
        if (extract_archive(context, fs, tools, archive, to_path_partial))
        {
            return to_path_partial;
        }

        return nullopt;
    }
#ifdef _WIN32
    bool win32_extract_self_extracting_7z(DiagnosticContext& context,
                                          const Filesystem& fs,
                                          const Path& archive,
                                          const Path& to_path)
    {
        static constexpr StringLiteral header_7z = "7z\xBC\xAF\x27\x1C";
        const Path stem = archive.stem();
        const auto subext = stem.extension();
        if (!Strings::case_insensitive_ascii_equals(subext, ".7z"))
        {
            context.report(DiagnosticLine{DiagKind::Error, archive, msg::format(msgSevenZipIncorrectExtension)});
            return false;
        }

        auto contents = fs.read_contents(archive, VCPKG_LINE_INFO);
        // try to chop off the beginning of the self extractor before the embedded 7z archive
        // some 7z self extractors, such as PortableGit-2.43.0-32-bit.7z.exe have 1 header
        // some 7z self extractors, such as 7z2408-x64.exe, have 2 headers
        auto pos = contents.find(header_7z.data(), 0, header_7z.size());
        if (pos == std::string::npos)
        {
            context.report(DiagnosticLine{DiagKind::Error, archive, msg::format(msgSevenZipIncorrectHeader)});
            return false;
        }

        // no bounds check necessary because header_7z is nonempty:
        auto pos2 = contents.find(header_7z.data(), pos + 1, header_7z.size());
        if (pos2 != std::string::npos)
        {
            pos = pos2;
        }

        StringView contents_sv = contents;
        fs.write_contents(to_path, contents_sv.substr(pos), VCPKG_LINE_INFO);
        return true;
    }
#endif

    bool extract_tar(DiagnosticContext& context, const Path& tar_tool, const Path& archive, const Path& to_path)
    {
        RedirectedProcessLaunchSettings settings;
        settings.working_directory = to_path;
        auto maybe_exit_and_output =
            cmd_execute_and_capture_output(context, Command{tar_tool}.string_arg("xzf").string_arg(archive), settings);
        return postprocess_extract_archive(context, std::move(maybe_exit_and_output), tar_tool, archive);
    }

    bool extract_tar_cmake(DiagnosticContext& context, const Path& cmake_tool, const Path& archive, const Path& to_path)
    {
        // Note that CMake's built in tar can extract more archive types than many system tars; e.g. 7z
        RedirectedProcessLaunchSettings settings;
        settings.working_directory = to_path;
        auto maybe_exit_and_output = cmd_execute_and_capture_output(
            context,
            Command{cmake_tool}.string_arg("-E").string_arg("tar").string_arg("xzf").string_arg(archive),
            settings);
        return postprocess_extract_archive(context, std::move(maybe_exit_and_output), cmake_tool, archive);
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

    bool ZipTool::setup(DiagnosticContext& context, const Filesystem& fs, const ToolCache& cache)
    {
#if defined(_WIN32)
        if (const auto* tool = cache.get_tool_path(context, fs, Tools::SEVEN_ZIP))
        {
            seven_zip.emplace(*tool);
            return true;
        }

        return false;
#else
        // Unused on non-Windows
        (void)context;
        (void)fs;
        (void)cache;
        return true;
#endif
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
}
