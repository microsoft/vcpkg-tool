#include <vcpkg/base/json.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/setup-messages.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/util.h>

#include <iterator>
#include <vector>

#include <cmrc/cmrc.hpp>

CMRC_DECLARE(cmakerc);

using namespace vcpkg;

namespace vcpkg::msg
{
    REGISTER_MESSAGE(SeeURL);
    REGISTER_MESSAGE(NoteMessage);
    REGISTER_MESSAGE(WarningMessage);
    REGISTER_MESSAGE(ErrorMessage);
    REGISTER_MESSAGE(InternalErrorMessage);
    REGISTER_MESSAGE(InternalErrorMessageContact);
    REGISTER_MESSAGE(BothYesAndNoOptionSpecifiedError);

    // basic implementation - the write_unlocalized_text_to_stdout
#if defined(_WIN32)
    static bool is_console(HANDLE h)
    {
        DWORD mode = 0;
        // GetConsoleMode succeeds iff `h` is a console
        // we do not actually care about the mode of the console
        return GetConsoleMode(h, &mode);
    }

    static void check_write(BOOL success)
    {
        if (!success)
        {
            ::fwprintf(stderr, L"[DEBUG] Failed to write to stdout: %lu\n", GetLastError());
            std::abort();
        }
    }
    static DWORD size_to_write(::size_t size) { return size > MAXDWORD ? MAXDWORD : static_cast<DWORD>(size); }

    static void write_unlocalized_text_impl(Color c, StringView sv, HANDLE the_handle, bool is_console)
    {
        if (sv.empty()) return;

        if (is_console)
        {
            WORD original_color = 0;
            if (c != Color::none)
            {
                CONSOLE_SCREEN_BUFFER_INFO console_screen_buffer_info{};
                ::GetConsoleScreenBufferInfo(the_handle, &console_screen_buffer_info);
                original_color = console_screen_buffer_info.wAttributes;
                ::SetConsoleTextAttribute(the_handle, static_cast<WORD>(c) | (original_color & 0xF0));
            }

            auto as_wstr = Strings::to_utf16(sv);

            const wchar_t* pointer = as_wstr.data();
            ::size_t size = as_wstr.size();

            while (size != 0)
            {
                DWORD written = 0;
                check_write(::WriteConsoleW(the_handle, pointer, size_to_write(size), &written, nullptr));
                pointer += written;
                size -= written;
            }

            if (c != Color::none)
            {
                ::SetConsoleTextAttribute(the_handle, original_color);
            }
        }
        else
        {
            const char* pointer = sv.data();
            ::size_t size = sv.size();

            while (size != 0)
            {
                DWORD written = 0;
                check_write(::WriteFile(the_handle, pointer, size_to_write(size), &written, nullptr));
                pointer += written;
                size -= written;
            }
        }
    }

    void write_unlocalized_text_to_stdout(Color c, StringView sv)
    {
        static const HANDLE stdout_handle = ::GetStdHandle(STD_OUTPUT_HANDLE);
        static const bool stdout_is_console = is_console(stdout_handle);
        return write_unlocalized_text_impl(c, sv, stdout_handle, stdout_is_console);
    }

    void write_unlocalized_text_to_stderr(Color c, StringView sv)
    {
        static const HANDLE stderr_handle = ::GetStdHandle(STD_ERROR_HANDLE);
        static const bool stderr_is_console = is_console(stderr_handle);
        return write_unlocalized_text_impl(c, sv, stderr_handle, stderr_is_console);
    }
#else
    static void write_all(const char* ptr, size_t to_write, int fd)
    {
        while (to_write != 0)
        {
            auto written = ::write(fd, ptr, to_write);
            if (written == -1)
            {
                ::fprintf(stderr, "[DEBUG] Failed to print to stdout: %d\n", errno);
                std::abort();
            }
            ptr += written;
            to_write -= written;
        }
    }

    static void write_unlocalized_text_impl(Color c, StringView sv, int fd, bool is_a_tty)
    {
        static constexpr char reset_color_sequence[] = {'\033', '[', '0', 'm'};

        if (sv.empty()) return;

        bool reset_color = false;
        if (is_a_tty && c != Color::none)
        {
            reset_color = true;

            const char set_color_sequence[] = {'\033', '[', '9', static_cast<char>(c), 'm'};
            write_all(set_color_sequence, sizeof(set_color_sequence), fd);
        }

        write_all(sv.data(), sv.size(), fd);

        if (reset_color)
        {
            write_all(reset_color_sequence, sizeof(reset_color_sequence), fd);
        }
    }

    void write_unlocalized_text_to_stdout(Color c, StringView sv)
    {
        static bool is_a_tty = ::isatty(STDOUT_FILENO);
        return write_unlocalized_text_impl(c, sv, STDOUT_FILENO, is_a_tty);
    }

    void write_unlocalized_text_to_stderr(Color c, StringView sv)
    {
        static bool is_a_tty = ::isatty(STDERR_FILENO);
        return write_unlocalized_text_impl(c, sv, STDERR_FILENO, is_a_tty);
    }
#endif

    namespace
    {
        struct Messages
        {
            // this is basically a SoA - each index is:
            // {
            //   name
            //   default_string
            //   localization_comment
            //   localized_string
            // }
            // requires: names.size() == default_strings.size() == localized_strings.size()
            std::vector<StringLiteral> names;
            std::vector<StringLiteral> default_strings;     // const after startup
            std::vector<ZStringView> localization_comments; // const after startup
            Optional<cmrc::file> json_file;
            std::vector<std::string> localized_strings;
        };

        // to avoid static initialization order issues,
        // everything that needs the messages needs to get it from this function
        Messages& messages()
        {
            static Messages m;
            return m;
        }
    }

    void threadunsafe_initialize_context()
    {
        Messages& m = messages();
        auto names_sorted = m.names;
        std::sort(names_sorted.begin(), names_sorted.end());
        std::vector<StringLiteral> duplicate_names;
        Util::set_duplicates(names_sorted.begin(), names_sorted.end(), std::back_inserter(duplicate_names));
        for (auto&& duplicate : duplicate_names)
        {
            write_unlocalized_text_to_stdout(
                Color::error,
                fmt::format("INTERNAL ERROR: localization message '{}' has been declared multiple times\n", duplicate));
        }

        if (!duplicate_names.empty())
        {
            ::abort();
        }
    }
    const Optional<cmrc::file> get_file()
    {
        Messages& m = messages();
        return m.json_file;
    }
    void load_from_message_map(const MessageMapAndFile& map_and_file)
    {
        auto&& message_map = map_and_file.map;
        Messages& m = messages();
        m.localized_strings.resize(m.names.size());
        m.json_file = map_and_file.map_file;

        std::vector<std::string> names_without_localization;

        for (::size_t index = 0; index < m.names.size(); ++index)
        {
            const StringView name = msg::detail::get_message_name(index);
            if (auto p = message_map.get(msg::detail::get_message_name(index)))
            {
                m.localized_strings[index] = p->string(VCPKG_LINE_INFO).to_string();
            }
            else if (Debug::g_debugging)
            {
                // we only want to print these in debug
                names_without_localization.emplace_back(name);
            }
        }

        if (!names_without_localization.empty())
        {
            println(Color::warning, msgNoLocalizationForMessages);
            for (const auto& name : names_without_localization)
            {
                write_unlocalized_text_to_stdout(Color::warning, fmt::format("    - {}\n", name));
            }
        }
    }

    ExpectedS<MessageMapAndFile> get_message_map_from_lcid(int LCID)
    {
        threadunsafe_initialize_context();
        auto embedded_filesystem = cmrc::cmakerc::get_filesystem();

        const auto maybe_locale_path = get_locale_path(LCID);
        if (const auto locale_path = maybe_locale_path.get())
        {
            auto file = embedded_filesystem.open(*locale_path);
            return Json::parse_object(StringView{file.begin(), file.end()}, *locale_path)
                .map([&](Json::Object&& parsed_file) {
                    return MessageMapAndFile{std::move(parsed_file), file};
                });
        }

        return std::string{"Unrecognized LCID"};
    }

    Optional<std::string> get_locale_path(int LCID)
    {
        return get_language_tag(LCID).map(
            [](StringLiteral tag) { return fmt::format("locales/messages.{}.json", tag); });
    }

    // LCIDs supported by VS:
    // https://learn.microsoft.com/en-us/visualstudio/ide/reference/lcid-devenv-exe?view=vs-2022
    Optional<StringLiteral> get_language_tag(int LCID)
    {
        static constexpr std::pair<int, StringLiteral> languages[] = {
            std::pair<int, StringLiteral>(1029, "cs"), // Czech
            std::pair<int, StringLiteral>(1031, "de"), // German
            // Always use default handling for 1033 (English)
            // std::pair<int, StringLiteral>(1033, "en"),    // English
            std::pair<int, StringLiteral>(3082, "es"),       // Spanish (Spain)
            std::pair<int, StringLiteral>(1036, "fr"),       // French
            std::pair<int, StringLiteral>(1040, "it"),       // Italian
            std::pair<int, StringLiteral>(1041, "ja"),       // Japanese
            std::pair<int, StringLiteral>(1042, "ko"),       // Korean
            std::pair<int, StringLiteral>(1045, "pl"),       // Polish
            std::pair<int, StringLiteral>(1046, "pt-BR"),    // Portuguese (Brazil)
            std::pair<int, StringLiteral>(1049, "ru"),       // Russian
            std::pair<int, StringLiteral>(1055, "tr"),       // Turkish
            std::pair<int, StringLiteral>(2052, "zh-Hans"),  // Chinese (Simplified)
            std::pair<int, StringLiteral>(1028, "zh-Hant")}; // Chinese (Traditional)

        for (auto&& l : languages)
        {
            if (l.first == LCID)
            {
                return l.second;
            }
        }

        return nullopt;
    }

    ::size_t detail::number_of_messages() { return messages().names.size(); }

    ::size_t detail::startup_register_message(StringLiteral name, StringLiteral format_string, ZStringView comment)
    {
        Messages& m = messages();
        const auto res = m.names.size();
        m.names.push_back(name);
        m.default_strings.push_back(format_string);
        m.localization_comments.push_back(comment);
        return res;
    }

    StringView detail::get_format_string(::size_t index)
    {
        Messages& m = messages();
        if (m.localized_strings.empty())
        {
            return m.default_strings[index];
        }

        if (m.localized_strings.size() != m.default_strings.size() || index >= m.default_strings.size())
        {
            // abort is used rather than check_exit to avoid infinite recursion trying to get a format string to print
            std::abort();
        }

        return m.localized_strings[index];
    }
    StringView detail::get_message_name(::size_t index)
    {
        Messages& m = messages();
        Checks::check_exit(VCPKG_LINE_INFO, index < m.names.size());
        return m.names[index];
    }
    StringView detail::get_default_format_string(::size_t index)
    {
        Messages& m = messages();
        Checks::check_exit(VCPKG_LINE_INFO, index < m.default_strings.size());
        return m.default_strings[index];
    }
    StringView detail::get_localization_comment(::size_t index)
    {
        Messages& m = messages();
        Checks::check_exit(VCPKG_LINE_INFO, index < m.localization_comments.size());
        return m.localization_comments[index];
    }

    LocalizedString detail::internal_vformat(::size_t index, fmt::format_args args)
    {
        const auto fmt_string = get_format_string(index);
        try
        {
            return LocalizedString::from_raw(fmt::vformat({fmt_string.data(), fmt_string.size()}, args));
        }
        catch (...)
        {
            const auto default_format_string = get_default_format_string(index);
            try
            {
                return LocalizedString::from_raw(
                    fmt::vformat({default_format_string.data(), default_format_string.size()}, args));
            }
            catch (...)
            {
                ::fprintf(stderr,
                          "INTERNAL ERROR: failed to format default format string for index %zu\nformat string: %.*s\n",
                          index,
                          (int)default_format_string.size(),
                          default_format_string.data());
                Checks::exit_fail(VCPKG_LINE_INFO);
            }
        }
    }

    void println_warning(const LocalizedString& s)
    {
        print(Color::warning, format(msgWarningMessage).append(s).append_raw('\n'));
    }

    void println_error(const LocalizedString& s)
    {
        print(Color::error, format(msgErrorMessage).append(s).append_raw('\n'));
    }
}

namespace
{
    struct NullMessageSink : MessageSink
    {
        virtual void print(Color, StringView) override { }
    };

    NullMessageSink null_sink_instance;

    struct StdOutMessageSink : MessageSink
    {
        virtual void print(Color c, StringView sv) override { msg::write_unlocalized_text_to_stdout(c, sv); }
    };

    StdOutMessageSink stdout_sink_instance;

    struct StdErrMessageSink : MessageSink
    {
        virtual void print(Color c, StringView sv) override { msg::write_unlocalized_text_to_stderr(c, sv); }
    };

    StdErrMessageSink stderr_sink_instance;
}

namespace vcpkg
{

    MessageSink& null_sink = null_sink_instance;
    MessageSink& stderr_sink = stderr_sink_instance;
    MessageSink& stdout_sink = stdout_sink_instance;

    REGISTER_MESSAGE(AddArtifactOnlyOne);
    REGISTER_MESSAGE(AddCommandFirstArg);
    REGISTER_MESSAGE(AddFirstArgument);
    REGISTER_MESSAGE(AddingCompletionEntry);
    REGISTER_MESSAGE(AdditionalPackagesToExport);
    REGISTER_MESSAGE(AdditionalPackagesToRemove);
    REGISTER_MESSAGE(AddPortRequiresManifest);
    REGISTER_MESSAGE(AddPortSucceeded);
    REGISTER_MESSAGE(AddRecurseOption);
    REGISTER_MESSAGE(AddTripletExpressionNotAllowed);
    REGISTER_MESSAGE(AddVersionAddedVersionToFile);
    REGISTER_MESSAGE(AddVersionCommitChangesReminder);
    REGISTER_MESSAGE(AddVersionCommitResultReminder);
    REGISTER_MESSAGE(AddVersionDetectLocalChangesError);
    REGISTER_MESSAGE(AddVersionFileNotFound);
    REGISTER_MESSAGE(AddVersionFormatPortSuggestion);
    REGISTER_MESSAGE(AddVersionIgnoringOptionAll);
    REGISTER_MESSAGE(AddVersionLoadPortFailed);
    REGISTER_MESSAGE(AddVersionNewFile);
    REGISTER_MESSAGE(AddVersionNewShaIs);
    REGISTER_MESSAGE(AddVersionNoFilesUpdated);
    REGISTER_MESSAGE(AddVersionNoFilesUpdatedForPort);
    REGISTER_MESSAGE(AddVersionNoGitSha);
    REGISTER_MESSAGE(AddVersionOldShaIs);
    REGISTER_MESSAGE(AddVersionOverwriteOptionSuggestion);
    REGISTER_MESSAGE(AddVersionPortDoesNotExist);
    REGISTER_MESSAGE(AddVersionPortFilesShaChanged);
    REGISTER_MESSAGE(AddVersionPortFilesShaUnchanged);
    REGISTER_MESSAGE(AddVersionPortHasImproperFormat);
    REGISTER_MESSAGE(AddVersionSuggestNewVersionScheme);
    REGISTER_MESSAGE(AddVersionUnableToParseVersionsFile);
    REGISTER_MESSAGE(AddVersionUncommittedChanges);
    REGISTER_MESSAGE(AddVersionUpdateVersionReminder);
    REGISTER_MESSAGE(AddVersionUseOptionAll);
    REGISTER_MESSAGE(AddVersionVersionAlreadyInFile);
    REGISTER_MESSAGE(AddVersionVersionIs);
    REGISTER_MESSAGE(AllFormatArgsRawArgument);
    REGISTER_MESSAGE(AllFormatArgsUnbalancedBraces);
    REGISTER_MESSAGE(AllPackagesAreUpdated);
    REGISTER_MESSAGE(AlreadyInstalled);
    REGISTER_MESSAGE(AlreadyInstalledNotHead);
    REGISTER_MESSAGE(AnotherInstallationInProgress);
    REGISTER_MESSAGE(AppliedUserIntegration);
    REGISTER_MESSAGE(ArtifactsOptionIncompatibility);
    REGISTER_MESSAGE(AssetSourcesArg);
    REGISTER_MESSAGE(AttemptingToFetchPackagesFromVendor);
    REGISTER_MESSAGE(AttemptingToSetBuiltInBaseline);
    REGISTER_MESSAGE(AuthenticationMayRequireManualAction);
    REGISTER_MESSAGE(AutomaticLinkingForMSBuildProjects);
    REGISTER_MESSAGE(AutoSettingEnvVar);
    REGISTER_MESSAGE(BaselineConflict);
    REGISTER_MESSAGE(BaselineFileNoDefaultField);
    REGISTER_MESSAGE(BaselineMissingDefault);
    REGISTER_MESSAGE(AvailableArchitectureTriplets);
    REGISTER_MESSAGE(AvailableHelpTopics);
    REGISTER_MESSAGE(BinarySourcesArg);
    REGISTER_MESSAGE(BuildAlreadyInstalled);
    REGISTER_MESSAGE(BuildDependenciesMissing);
    REGISTER_MESSAGE(BuildingFromHead);
    REGISTER_MESSAGE(BuildingPackage);
    REGISTER_MESSAGE(BuildingPackageFailed);
    REGISTER_MESSAGE(BuildingPackageFailedDueToMissingDeps);
    REGISTER_MESSAGE(BuildResultBuildFailed);
    REGISTER_MESSAGE(BuildResultCacheMissing);
    REGISTER_MESSAGE(BuildResultCascadeDueToMissingDependencies);
    REGISTER_MESSAGE(BuildResultDownloaded);
    REGISTER_MESSAGE(BuildResultExcluded);
    REGISTER_MESSAGE(BuildResultFileConflicts);
    REGISTER_MESSAGE(BuildResultPostBuildChecksFailed);
    REGISTER_MESSAGE(BuildResultRemoved);
    REGISTER_MESSAGE(BuildResultSucceeded);
    REGISTER_MESSAGE(BuildResultSummaryHeader);
    REGISTER_MESSAGE(BuildResultSummaryLine);
    REGISTER_MESSAGE(BuildTreesRootDir);
    REGISTER_MESSAGE(BuildTroubleshootingMessage1);
    REGISTER_MESSAGE(BuildTroubleshootingMessage2);
    REGISTER_MESSAGE(BuildTroubleshootingMessage3);
    REGISTER_MESSAGE(BuildTroubleshootingMessage4);
    REGISTER_MESSAGE(BuiltInTriplets);
    REGISTER_MESSAGE(ChecksFailedCheck);
    REGISTER_MESSAGE(ChecksUnreachableCode);
    REGISTER_MESSAGE(ChecksUpdateVcpkg);
    REGISTER_MESSAGE(CiBaselineAllowUnexpectedPassingRequiresBaseline);
    REGISTER_MESSAGE(CiBaselineDisallowedCascade);
    REGISTER_MESSAGE(CiBaselineRegression);
    REGISTER_MESSAGE(CiBaselineRegressionHeader);
    REGISTER_MESSAGE(CiBaselineUnexpectedPass);
    REGISTER_MESSAGE(ClearingContents);
    REGISTER_MESSAGE(CmakeTargetsExcluded);
    REGISTER_MESSAGE(CMakeTargetsUsage);
    REGISTER_MESSAGE(CMakeTargetsUsageHeuristicMessage);
    REGISTER_MESSAGE(CMakeToolChainFile);
    REGISTER_MESSAGE(CommandFailed);
    REGISTER_MESSAGE(CompressFolderFailed);
    REGISTER_MESSAGE(ComputingInstallPlan);
    REGISTER_MESSAGE(ConfigurationErrorRegistriesWithoutBaseline);
    REGISTER_MESSAGE(ConflictingFiles);
    REGISTER_MESSAGE(CMakeUsingExportedLibs);
    REGISTER_MESSAGE(CommunityTriplets);
    REGISTER_MESSAGE(ComparingUtf8Decoders);
    REGISTER_MESSAGE(ConflictingValuesForOption);
    REGISTER_MESSAGE(ConstraintViolation);
    REGISTER_MESSAGE(ContinueCodeUnitInStart);
    REGISTER_MESSAGE(ControlAndManifestFilesPresent);
    REGISTER_MESSAGE(ControlCharacterInString);
    REGISTER_MESSAGE(CopyrightIsDir);
    REGISTER_MESSAGE(CorruptedDatabase);
    REGISTER_MESSAGE(CouldNotDeduceNugetIdAndVersion);
    REGISTER_MESSAGE(CouldNotFindToolVersion);
    REGISTER_MESSAGE(CorruptedInstallTree);
    REGISTER_MESSAGE(CouldNotFindBaseline);
    REGISTER_MESSAGE(CouldNotFindBaselineForRepo);
    REGISTER_MESSAGE(CouldNotFindBaselineInCommit);
    REGISTER_MESSAGE(CouldNotFindGitTreeAtCommit);
    REGISTER_MESSAGE(CreatedNuGetPackage);
    REGISTER_MESSAGE(CreateFailureLogsDir);
    REGISTER_MESSAGE(Creating7ZipArchive);
    REGISTER_MESSAGE(CreatingNugetPackage);
    REGISTER_MESSAGE(CreatingZipArchive);
    REGISTER_MESSAGE(CreationFailed);
    REGISTER_MESSAGE(CurlFailedToExecute);
    REGISTER_MESSAGE(CurlFailedToPut);
    REGISTER_MESSAGE(CurlFailedToPutHttp);
    REGISTER_MESSAGE(CurlReportedUnexpectedResults);
    REGISTER_MESSAGE(CurlReturnedUnexpectedResponseCodes);
    REGISTER_MESSAGE(CurrentCommitBaseline);
    REGISTER_MESSAGE(DateTableHeader);
    REGISTER_MESSAGE(DefaultBrowserLaunched);
    REGISTER_MESSAGE(DefaultFlag);
    REGISTER_MESSAGE(DefaultPathToBinaries);
    REGISTER_MESSAGE(DeleteVcpkgConfigFromManifest);
    REGISTER_MESSAGE(DeprecatedPrefabDebugOption);
    REGISTER_MESSAGE(DetectCompilerHash);
    REGISTER_MESSAGE(DocumentedFieldsSuggestUpdate);
    REGISTER_MESSAGE(DownloadAvailable);
    REGISTER_MESSAGE(DownloadedSources);
    REGISTER_MESSAGE(DownloadFailedCurl);
    REGISTER_MESSAGE(DownloadFailedHashMismatch);
    REGISTER_MESSAGE(DownloadFailedRetrying);
    REGISTER_MESSAGE(DownloadFailedStatusCode);
    REGISTER_MESSAGE(DownloadingPortableToolVersionX);
    REGISTER_MESSAGE(DownloadingTool);
    REGISTER_MESSAGE(DownloadingUrl);
    REGISTER_MESSAGE(DownloadWinHttpError);
    REGISTER_MESSAGE(DownloadingVcpkgCeBundle);
    REGISTER_MESSAGE(DownloadingVcpkgCeBundleLatest);
    REGISTER_MESSAGE(DownloadingVcpkgStandaloneBundle);
    REGISTER_MESSAGE(DownloadingVcpkgStandaloneBundleLatest);
    REGISTER_MESSAGE(DownloadRootsDir);
    REGISTER_MESSAGE(DuplicateCommandOption);
    REGISTER_MESSAGE(DuplicatedKeyInObj);
    REGISTER_MESSAGE(DuplicateOptions);
    REGISTER_MESSAGE(DuplicatePackagePattern);
    REGISTER_MESSAGE(DuplicatePackagePatternFirstOcurrence);
    REGISTER_MESSAGE(DuplicatePackagePatternIgnoredLocations);
    REGISTER_MESSAGE(DuplicatePackagePatternLocation);
    REGISTER_MESSAGE(DuplicatePackagePatternRegistry);
    REGISTER_MESSAGE(ElapsedInstallTime);
    REGISTER_MESSAGE(ElapsedTimeForChecks);
    REGISTER_MESSAGE(EmailVcpkgTeam);
    REGISTER_MESSAGE(EmbeddingVcpkgConfigInManifest);
    REGISTER_MESSAGE(EmptyArg);
    REGISTER_MESSAGE(EmptyLicenseExpression);
    REGISTER_MESSAGE(EndOfStringInCodeUnit);
    REGISTER_MESSAGE(EnvInvalidMaxConcurrency);
    REGISTER_MESSAGE(EnvStrFailedToExtract);
    REGISTER_MESSAGE(ErrorDetectingCompilerInfo);
    REGISTER_MESSAGE(ErrorIndividualPackagesUnsupported);
    REGISTER_MESSAGE(ErrorInvalidClassicModeOption);
    REGISTER_MESSAGE(ErrorInvalidManifestModeOption);
    REGISTER_MESSAGE(ErrorMessageMustUsePrintError);
    REGISTER_MESSAGE(ErrorMissingVcpkgRoot);
    REGISTER_MESSAGE(ErrorNoVSInstance);
    REGISTER_MESSAGE(ErrorNoVSInstanceAt);
    REGISTER_MESSAGE(ErrorNoVSInstanceFullVersion);
    REGISTER_MESSAGE(ErrorNoVSInstanceVersion);
    REGISTER_MESSAGE(ErrorParsingBinaryParagraph);
    REGISTER_MESSAGE(ErrorRequireBaseline);
    REGISTER_MESSAGE(ErrorRequirePackagesList);
    REGISTER_MESSAGE(ErrorsFound);
    REGISTER_MESSAGE(ErrorUnableToDetectCompilerInfo);
    REGISTER_MESSAGE(ErrorVcvarsUnsupported);
    REGISTER_MESSAGE(ErrorVsCodeNotFound);
    REGISTER_MESSAGE(ErrorVsCodeNotFoundPathExamined);
    REGISTER_MESSAGE(ErrorWhileFetchingBaseline);
    REGISTER_MESSAGE(ErrorWhileParsing);
    REGISTER_MESSAGE(ErrorWhileWriting);
    REGISTER_MESSAGE(ExceededRecursionDepth);
    REGISTER_MESSAGE(ExcludedPackage);
    REGISTER_MESSAGE(ExcludedPackages);
    REGISTER_MESSAGE(ExpectedAtMostOneSetOfTags);
    REGISTER_MESSAGE(ExpectedCascadeFailure);
    REGISTER_MESSAGE(ExpectedCharacterHere);
    REGISTER_MESSAGE(ExpectedFailOrSkip);
    REGISTER_MESSAGE(ExpectedPathToExist);
    REGISTER_MESSAGE(ExpectedDigitsAfterDecimal);
    REGISTER_MESSAGE(ExpectedOneSetOfTags);
    REGISTER_MESSAGE(ExpectedPortName);
    REGISTER_MESSAGE(ExpectedStatusField);
    REGISTER_MESSAGE(ExpectedTripletName);
    REGISTER_MESSAGE(ExpectedValueForOption);
    REGISTER_MESSAGE(ExtendedDocumentationAtUrl);
    REGISTER_MESSAGE(ExtractingTool);
    REGISTER_MESSAGE(FailedToDetermineCurrentCommit);
    REGISTER_MESSAGE(FailedToExtract);
    REGISTER_MESSAGE(ExportArchitectureReq);
    REGISTER_MESSAGE(Exported7zipArchive);
    REGISTER_MESSAGE(ExportedZipArchive);
    REGISTER_MESSAGE(ExportingAlreadyBuiltPackages);
    REGISTER_MESSAGE(ExportingMaintenanceTool);
    REGISTER_MESSAGE(ExportingPackage);
    REGISTER_MESSAGE(ExportPrefabRequiresAndroidTriplet);
    REGISTER_MESSAGE(ExportUnsupportedInManifest);
    REGISTER_MESSAGE(FailedToCheckoutRepo);
    REGISTER_MESSAGE(FailedToDownloadFromMirrorSet);
    REGISTER_MESSAGE(FailedToFindBaseline);
    REGISTER_MESSAGE(FailedToFindPortFeature);
    REGISTER_MESSAGE(FailedToFormatMissingFile);
    REGISTER_MESSAGE(FailedToLoadInstalledManifest);
    REGISTER_MESSAGE(FailedToLoadManifest);
    REGISTER_MESSAGE(FailedToLoadPort);
    REGISTER_MESSAGE(FailedToLoadPortFrom);
    REGISTER_MESSAGE(FailedToLocateSpec);
    REGISTER_MESSAGE(FailedToObtainDependencyVersion);
    REGISTER_MESSAGE(FailedToObtainLocalPortGitSha);
    REGISTER_MESSAGE(FailedToObtainPackageVersion);
    REGISTER_MESSAGE(FailedToParseCMakeConsoleOut);
    REGISTER_MESSAGE(FailedToParseConfig);
    REGISTER_MESSAGE(FailedToParseControl);
    REGISTER_MESSAGE(FailedToParseJson);
    REGISTER_MESSAGE(FailedToParseManifest);
    REGISTER_MESSAGE(FailedToParseSerializedBinParagraph);
    REGISTER_MESSAGE(FailedToParseVersionXML);
    REGISTER_MESSAGE(FailedToProvisionCe);
    REGISTER_MESSAGE(FailedToRead);
    REGISTER_MESSAGE(FailedToReadParagraph);
    REGISTER_MESSAGE(FailedToRemoveControl);
    REGISTER_MESSAGE(FailedToRunToolToDetermineVersion);
    REGISTER_MESSAGE(FailedToStoreBackToMirror);
    REGISTER_MESSAGE(FailedToStoreBinaryCache);
    REGISTER_MESSAGE(FailedToTakeFileSystemLock);
    REGISTER_MESSAGE(FailedToWriteManifest);
    REGISTER_MESSAGE(FailedVendorAuthentication);
    REGISTER_MESSAGE(FeedbackAppreciated);
    REGISTER_MESSAGE(FetchingBaselineInfo);
    REGISTER_MESSAGE(FetchingRegistryInfo);
    REGISTER_MESSAGE(FloatingPointConstTooBig);
    REGISTER_MESSAGE(FileNotFound);
    REGISTER_MESSAGE(FileReadFailed);
    REGISTER_MESSAGE(FileSeekFailed);
    REGISTER_MESSAGE(FilesExported);
    REGISTER_MESSAGE(FileSystemOperationFailed);
    REGISTER_MESSAGE(FishCompletion);
    REGISTER_MESSAGE(FilesContainAbsolutePath1);
    REGISTER_MESSAGE(FilesContainAbsolutePath2);
    REGISTER_MESSAGE(FollowingPackagesMissingControl);
    REGISTER_MESSAGE(FollowingPackagesNotInstalled);
    REGISTER_MESSAGE(FollowingPackagesUpgraded);
    REGISTER_MESSAGE(ForceSystemBinariesOnWeirdPlatforms);
    REGISTER_MESSAGE(FormattedParseMessageExpression);
    REGISTER_MESSAGE(GeneratedConfiguration);
    REGISTER_MESSAGE(GeneratedInstaller);
    REGISTER_MESSAGE(GenerateMsgErrorParsingFormatArgs);
    REGISTER_MESSAGE(GenerateMsgIncorrectComment);
    REGISTER_MESSAGE(GenerateMsgNoArgumentValue);
    REGISTER_MESSAGE(GenerateMsgNoCommentValue);
    REGISTER_MESSAGE(GeneratingConfiguration);
    REGISTER_MESSAGE(GeneratingInstaller);
    REGISTER_MESSAGE(GeneratingRepo);
    REGISTER_MESSAGE(GetParseFailureInfo);
    REGISTER_MESSAGE(GitCommandFailed);
    REGISTER_MESSAGE(GitRegistryMustHaveBaseline);
    REGISTER_MESSAGE(GitStatusOutputExpectedFileName);
    REGISTER_MESSAGE(GitStatusOutputExpectedNewLine);
    REGISTER_MESSAGE(GitStatusOutputExpectedRenameOrNewline);
    REGISTER_MESSAGE(GitStatusUnknownFileStatus);
    REGISTER_MESSAGE(GitUnexpectedCommandOutput);
    REGISTER_MESSAGE(HashFileFailureToRead);
    REGISTER_MESSAGE(HeaderOnlyUsage);
    REGISTER_MESSAGE(HelpBuiltinBase);
    REGISTER_MESSAGE(HelpContactCommand);
    REGISTER_MESSAGE(HelpCreateCommand);
    REGISTER_MESSAGE(HelpDependInfoCommand);
    REGISTER_MESSAGE(HelpEditCommand);
    REGISTER_MESSAGE(HelpEnvCommand);
    REGISTER_MESSAGE(HelpExampleCommand);
    REGISTER_MESSAGE(HelpExampleManifest);
    REGISTER_MESSAGE(HelpExportCommand);
    REGISTER_MESSAGE(HelpFormatManifestCommand);
    REGISTER_MESSAGE(HelpHashCommand);
    REGISTER_MESSAGE(HelpInitializeRegistryCommand);
    REGISTER_MESSAGE(HelpInstallCommand);
    REGISTER_MESSAGE(HelpListCommand);
    REGISTER_MESSAGE(HelpManifestConstraints);
    REGISTER_MESSAGE(HelpMinVersion);
    REGISTER_MESSAGE(HelpOverrides);
    REGISTER_MESSAGE(HelpOwnsCommand);
    REGISTER_MESSAGE(HelpPackagePublisher);
    REGISTER_MESSAGE(HelpPortVersionScheme);
    REGISTER_MESSAGE(HelpRemoveCommand);
    REGISTER_MESSAGE(HelpRemoveOutdatedCommand);
    REGISTER_MESSAGE(HelpResponseFileCommand);
    REGISTER_MESSAGE(HelpSearchCommand);
    REGISTER_MESSAGE(HelpTopicCommand);
    REGISTER_MESSAGE(HelpTopicsCommand);
    REGISTER_MESSAGE(HelpUpdateBaseline);
    REGISTER_MESSAGE(HelpUpdateCommand);
    REGISTER_MESSAGE(HelpUpgradeCommand);
    REGISTER_MESSAGE(HelpVersionCommand);
    REGISTER_MESSAGE(HelpVersionDateScheme);
    REGISTER_MESSAGE(HelpVersionGreater);
    REGISTER_MESSAGE(HelpVersioning);
    REGISTER_MESSAGE(HelpVersionScheme);
    REGISTER_MESSAGE(HelpVersionSchemes);
    REGISTER_MESSAGE(HelpVersionSemverScheme);
    REGISTER_MESSAGE(HelpVersionStringScheme);
    REGISTER_MESSAGE(IgnoringVcpkgRootEnvironment);
    REGISTER_MESSAGE(IllegalFeatures);
    REGISTER_MESSAGE(IllegalPlatformSpec);
    REGISTER_MESSAGE(ImproperShaLength);
    REGISTER_MESSAGE(IncorrectArchiveFileSignature);
    REGISTER_MESSAGE(IncorrectLibHeaderEnd);
    REGISTER_MESSAGE(IncorrectPESignature);
    REGISTER_MESSAGE(IncorrectNumberOfArgs);
    REGISTER_MESSAGE(IncrementedUtf8Decoder);
    REGISTER_MESSAGE(InfoSetEnvVar);
    REGISTER_MESSAGE(InitRegistryFailedNoRepo);
    REGISTER_MESSAGE(InstalledBy);
    REGISTER_MESSAGE(InstalledPackages);
    REGISTER_MESSAGE(InstalledRequestedPackages);
    REGISTER_MESSAGE(InstallFailed);
    REGISTER_MESSAGE(InstallingFromLocation);
    REGISTER_MESSAGE(InstallingMavenFile);
    REGISTER_MESSAGE(InstallingPackage);
    REGISTER_MESSAGE(InstallPackageInstruction);
    REGISTER_MESSAGE(InstallRootDir);
    REGISTER_MESSAGE(InstallWithSystemManager);
    REGISTER_MESSAGE(InstallWithSystemManagerMono);
    REGISTER_MESSAGE(InstallWithSystemManagerPkg);
    REGISTER_MESSAGE(IntegrationFailed);
    REGISTER_MESSAGE(InternalCICommand);
    REGISTER_MESSAGE(InvalidArgMustBeAnInt);
    REGISTER_MESSAGE(InvalidArgMustBePositive);
    REGISTER_MESSAGE(InvalidArgument);
    REGISTER_MESSAGE(InvalidArgumentRequiresAbsolutePath);
    REGISTER_MESSAGE(InvalidArgumentRequiresBaseUrl);
    REGISTER_MESSAGE(InvalidArgumentRequiresBaseUrlAndToken);
    REGISTER_MESSAGE(InvalidArgumentRequiresNoneArguments);
    REGISTER_MESSAGE(InvalidArgumentRequiresOneOrTwoArguments);
    REGISTER_MESSAGE(InvalidArgumentRequiresPathArgument);
    REGISTER_MESSAGE(InvalidArgumentRequiresPrefix);
    REGISTER_MESSAGE(InvalidArgumentRequiresSingleArgument);
    REGISTER_MESSAGE(InvalidArgumentRequiresSingleStringArgument);
    REGISTER_MESSAGE(InvalidArgumentRequiresSourceArgument);
    REGISTER_MESSAGE(InvalidArgumentRequiresTwoOrThreeArguments);
    REGISTER_MESSAGE(InvalidArgumentRequiresValidToken);
    REGISTER_MESSAGE(InvalidBuildInfo);
    REGISTER_MESSAGE(InvalidBuiltInBaseline);
    REGISTER_MESSAGE(InvalidBundleDefinition);
    REGISTER_MESSAGE(InvalidCodePoint);
    REGISTER_MESSAGE(InvalidCodeUnit);
    REGISTER_MESSAGE(InvalidCommandArgSort);
    REGISTER_MESSAGE(InvalidCommitId);
    REGISTER_MESSAGE(InvalidFilename);
    REGISTER_MESSAGE(InvalidFloatingPointConst);
    REGISTER_MESSAGE(InvalidHexDigit);
    REGISTER_MESSAGE(InvalidIntegerConst);
    REGISTER_MESSAGE(InvalidPortVersonName);
    REGISTER_MESSAGE(InvalidString);
    REGISTER_MESSAGE(InvalidFileType);
    REGISTER_MESSAGE(InvalidFormatString);
    REGISTER_MESSAGE(InvalidLinkage);
    REGISTER_MESSAGE(InvalidOptionForRemove);
    REGISTER_MESSAGE(InvalidTriplet);
    REGISTER_MESSAGE(IrregularFile);
    REGISTER_MESSAGE(JsonErrorMustBeAnObject);
    REGISTER_MESSAGE(JsonFileMissingExtension);
    REGISTER_MESSAGE(JsonSwitch);
    REGISTER_MESSAGE(JsonValueNotArray);
    REGISTER_MESSAGE(JsonValueNotObject);
    REGISTER_MESSAGE(JsonValueNotString);
    REGISTER_MESSAGE(LaunchingProgramFailed);
    REGISTER_MESSAGE(LicenseExpressionContainsExtraPlus);
    REGISTER_MESSAGE(LicenseExpressionContainsInvalidCharacter);
    REGISTER_MESSAGE(LicenseExpressionContainsUnicode);
    REGISTER_MESSAGE(LicenseExpressionDocumentRefUnsupported);
    REGISTER_MESSAGE(LicenseExpressionExpectCompoundFoundParen);
    REGISTER_MESSAGE(LicenseExpressionExpectCompoundFoundWith);
    REGISTER_MESSAGE(LicenseExpressionExpectCompoundFoundWord);
    REGISTER_MESSAGE(LicenseExpressionExpectCompoundOrWithFoundWord);
    REGISTER_MESSAGE(LicenseExpressionExpectExceptionFoundCompound);
    REGISTER_MESSAGE(LicenseExpressionExpectExceptionFoundEof);
    REGISTER_MESSAGE(LicenseExpressionExpectExceptionFoundParen);
    REGISTER_MESSAGE(LicenseExpressionExpectLicenseFoundCompound);
    REGISTER_MESSAGE(LicenseExpressionExpectLicenseFoundEof);
    REGISTER_MESSAGE(LicenseExpressionExpectLicenseFoundParen);
    REGISTER_MESSAGE(LicenseExpressionImbalancedParens);
    REGISTER_MESSAGE(LicenseExpressionUnknownException);
    REGISTER_MESSAGE(LicenseExpressionUnknownLicense);
    REGISTER_MESSAGE(ListOfValidFieldsForControlFiles);
    REGISTER_MESSAGE(LoadingCommunityTriplet);
    REGISTER_MESSAGE(LoadingDependencyInformation);
    REGISTER_MESSAGE(LoadingOverlayTriplet);
    REGISTER_MESSAGE(LocalizedMessageMustNotContainIndents);
    REGISTER_MESSAGE(LocalizedMessageMustNotEndWithNewline);
    REGISTER_MESSAGE(LocalPortfileVersion);
    REGISTER_MESSAGE(ManifestConflict);
    REGISTER_MESSAGE(ManifestFormatCompleted);
    REGISTER_MESSAGE(MismatchedFiles);
    REGISTER_MESSAGE(MismatchedNames);
    REGISTER_MESSAGE(Missing7zHeader);
    REGISTER_MESSAGE(MissingAndroidEnv);
    REGISTER_MESSAGE(MissingAndroidHomeDir);
    REGISTER_MESSAGE(MissingArgFormatManifest);
    REGISTER_MESSAGE(MissingDependency);
    REGISTER_MESSAGE(MissingExtension);
    REGISTER_MESSAGE(MissingOption);
    REGISTER_MESSAGE(MissingPortSuggestPullRequest);
    REGISTER_MESSAGE(MissmatchedBinParagraphs);
    REGISTER_MESSAGE(MonoInstructions);
    REGISTER_MESSAGE(MsiexecFailedToExtract);
    REGISTER_MESSAGE(MultiArch);
    REGISTER_MESSAGE(MutuallyExclusiveOption);
    REGISTER_MESSAGE(NavigateToNPS);
    REGISTER_MESSAGE(NewConfigurationAlreadyExists);
    REGISTER_MESSAGE(NewManifestAlreadyExists);
    REGISTER_MESSAGE(NewNameCannotBeEmpty);
    REGISTER_MESSAGE(NewOnlyOneVersionKind);
    REGISTER_MESSAGE(NewSpecifyNameVersionOrApplication);
    REGISTER_MESSAGE(NewVersionCannotBeEmpty);
    REGISTER_MESSAGE(NoArgumentsForOption);
    REGISTER_MESSAGE(NoCachedPackages);
    REGISTER_MESSAGE(NoError);
    REGISTER_MESSAGE(NoInstalledPackages);
    REGISTER_MESSAGE(NoLocalizationForMessages);
    REGISTER_MESSAGE(NoOutdatedPackages);
    REGISTER_MESSAGE(NoRegistryForPort);
    REGISTER_MESSAGE(NoUrlsAndHashSpecified);
    REGISTER_MESSAGE(NoUrlsAndNoHashSpecified);
    REGISTER_MESSAGE(NugetPackageFileSucceededButCreationFailed);
    REGISTER_MESSAGE(OptionMustBeInteger);
    REGISTER_MESSAGE(OptionRequired);
    REGISTER_MESSAGE(OptionRequiresOption);
    REGISTER_MESSAGE(OriginalBinParagraphHeader);
    REGISTER_MESSAGE(OverlayPatchDir);
    REGISTER_MESSAGE(OverlayTriplets);
    REGISTER_MESSAGE(OverwritingFile);
    REGISTER_MESSAGE(PackageAlreadyRemoved);
    REGISTER_MESSAGE(PackageFailedtWhileExtracting);
    REGISTER_MESSAGE(PackageRootDir);
    REGISTER_MESSAGE(PackagesToInstall);
    REGISTER_MESSAGE(PackagesToInstallDirectly);
    REGISTER_MESSAGE(PackagesToModify);
    REGISTER_MESSAGE(PackagesToRebuild);
    REGISTER_MESSAGE(PackagesToRebuildSuggestRecurse);
    REGISTER_MESSAGE(PackagesToRemove);
    REGISTER_MESSAGE(PackagesUpToDate);
    REGISTER_MESSAGE(PackingVendorFailed);
    REGISTER_MESSAGE(PairedSurrogatesAreInvalid);
    REGISTER_MESSAGE(ParseControlErrorInfoInvalidFields);
    REGISTER_MESSAGE(ParseControlErrorInfoMissingFields);
    REGISTER_MESSAGE(ParseControlErrorInfoTypesEntry);
    REGISTER_MESSAGE(ParseControlErrorInfoWhileLoading);
    REGISTER_MESSAGE(ParseControlErrorInfoWrongTypeFields);
    REGISTER_MESSAGE(ParseIdentifierError);
    REGISTER_MESSAGE(ParsePackageNameError);
    REGISTER_MESSAGE(ParsePackagePatternError);
    REGISTER_MESSAGE(PathMustBeAbsolute);
    REGISTER_MESSAGE(PECoffHeaderTooShort);
    REGISTER_MESSAGE(PEConfigCrossesSectionBoundary);
    REGISTER_MESSAGE(PEImportCrossesSectionBoundary);
    REGISTER_MESSAGE(PEPlusTagInvalid);
    REGISTER_MESSAGE(PERvaNotFound);
    REGISTER_MESSAGE(PESignatureMismatch);
    REGISTER_MESSAGE(PortDependencyConflict);
    REGISTER_MESSAGE(PortNotInBaseline);
    REGISTER_MESSAGE(PortsAdded);
    REGISTER_MESSAGE(PortsNoDiff);
    REGISTER_MESSAGE(PortsRemoved);
    REGISTER_MESSAGE(PortsUpdated);
    REGISTER_MESSAGE(PortSupportsField);
    REGISTER_MESSAGE(PortTypeConflict);
    REGISTER_MESSAGE(PreviousIntegrationFileRemains);
    REGISTER_MESSAGE(ProgramReturnedNonzeroExitCode);
    REGISTER_MESSAGE(ProvideExportType);
    REGISTER_MESSAGE(PushingVendorFailed);
    REGISTER_MESSAGE(RegistryCreated);
    REGISTER_MESSAGE(RemoveDependencies);
    REGISTER_MESSAGE(RemovePackageConflict);
    REGISTER_MESSAGE(RestoredPackage);
    REGISTER_MESSAGE(RestoredPackagesFromVendor);
    REGISTER_MESSAGE(ResultsHeader);
    REGISTER_MESSAGE(SecretBanner);
    REGISTER_MESSAGE(SerializedBinParagraphHeader);
    REGISTER_MESSAGE(SettingEnvVar);
    REGISTER_MESSAGE(ShallowRepositoryDetected);
    REGISTER_MESSAGE(ShaPassedAsArgAndOption);
    REGISTER_MESSAGE(ShaPassedWithConflict);
    REGISTER_MESSAGE(SkipClearingInvalidDir);
    REGISTER_MESSAGE(SourceFieldPortNameMismatch);
    REGISTER_MESSAGE(SpecifiedFeatureTurnedOff);
    REGISTER_MESSAGE(SpecifyDirectoriesContaining);
    REGISTER_MESSAGE(SpecifyDirectoriesWhenSearching);
    REGISTER_MESSAGE(SpecifyHostArch);
    REGISTER_MESSAGE(SpecifyTargetArch);
    REGISTER_MESSAGE(StartCodeUnitInContinue);
    REGISTER_MESSAGE(StoredBinaryCache);
    REGISTER_MESSAGE(StoreOptionMissingSha);
    REGISTER_MESSAGE(SuccessfulyExported);
    REGISTER_MESSAGE(SuggestGitPull);
    REGISTER_MESSAGE(SuggestResolution);
    REGISTER_MESSAGE(SuggestStartingBashShell);
    REGISTER_MESSAGE(SuggestUpdateVcpkg);
    REGISTER_MESSAGE(SupportedPort);
    REGISTER_MESSAGE(SystemApiErrorMessage);
    REGISTER_MESSAGE(ToolFetchFailed);
    REGISTER_MESSAGE(ToolInWin10);
    REGISTER_MESSAGE(ToolOfVersionXNotFound);
    REGISTER_MESSAGE(ToRemovePackages);
    REGISTER_MESSAGE(TotalInstallTime);
    REGISTER_MESSAGE(TwoFeatureFlagsSpecified);
    REGISTER_MESSAGE(UndeterminedToolChainForTriplet);
    REGISTER_MESSAGE(UnexpectedCharExpectedCloseBrace);
    REGISTER_MESSAGE(UnexpectedCharExpectedColon);
    REGISTER_MESSAGE(UnexpectedCharExpectedComma);
    REGISTER_MESSAGE(UnexpectedCharExpectedName);
    REGISTER_MESSAGE(UnexpectedCharExpectedValue);
    REGISTER_MESSAGE(UnexpectedCharMidArray);
    REGISTER_MESSAGE(UnexpectedCharMidKeyword);
    REGISTER_MESSAGE(UnexpectedDigitsAfterLeadingZero);
    REGISTER_MESSAGE(UnexpectedEOFAfterEscape);
    REGISTER_MESSAGE(UnexpectedEOFAfterMinus);
    REGISTER_MESSAGE(UnexpectedEOFExpectedChar);
    REGISTER_MESSAGE(UnexpectedEOFExpectedCloseBrace);
    REGISTER_MESSAGE(UnexpectedEOFExpectedColon);
    REGISTER_MESSAGE(UnexpectedEOFExpectedName);
    REGISTER_MESSAGE(UnexpectedEOFExpectedProp);
    REGISTER_MESSAGE(UnexpectedEOFExpectedValue);
    REGISTER_MESSAGE(UnexpectedEOFMidArray);
    REGISTER_MESSAGE(UnexpectedEOFMidKeyword);
    REGISTER_MESSAGE(UnexpectedEOFMidString);
    REGISTER_MESSAGE(UnexpectedEOFMidUnicodeEscape);
    REGISTER_MESSAGE(UnexpectedErrorDuringBulkDownload);
    REGISTER_MESSAGE(UnexpectedEscapeSequence);
    REGISTER_MESSAGE(UnexpectedByteSize);
    REGISTER_MESSAGE(UnexpectedExtension);
    REGISTER_MESSAGE(UnexpectedFormat);
    REGISTER_MESSAGE(UnexpectedToolOutput);
    REGISTER_MESSAGE(UnknownBaselineFileContent);
    REGISTER_MESSAGE(UnknownBinaryProviderType);
    REGISTER_MESSAGE(UnknownMachineCode);
    REGISTER_MESSAGE(UnknownOptions);
    REGISTER_MESSAGE(UnknownParameterForIntegrate);
    REGISTER_MESSAGE(UnknownPolicySetting);
    REGISTER_MESSAGE(UnknownSettingForBuildType);
    REGISTER_MESSAGE(UnknownTool);
    REGISTER_MESSAGE(UnknownTopic);
    REGISTER_MESSAGE(UnknownVariablesInTemplate);
    REGISTER_MESSAGE(UnrecognizedConfigField);
    REGISTER_MESSAGE(UnrecognizedIdentifier);
    REGISTER_MESSAGE(UnsupportedFeature);
    REGISTER_MESSAGE(UnsupportedFeatureSupportsExpression);
    REGISTER_MESSAGE(UnsupportedFeatureSupportsExpressionWarning);
    REGISTER_MESSAGE(UnsupportedPort);
    REGISTER_MESSAGE(UnsupportedPortDependency);
    REGISTER_MESSAGE(UnsupportedShortOptions);
    REGISTER_MESSAGE(UnsupportedSupportsExpression);
    REGISTER_MESSAGE(UnsupportedSupportsExpressionWarning);
    REGISTER_MESSAGE(UnsupportedSyntaxInCDATA);
    REGISTER_MESSAGE(UnsupportedSystemName);
    REGISTER_MESSAGE(UnsupportedToolchain);
    REGISTER_MESSAGE(UnsupportedUpdateCMD);
    REGISTER_MESSAGE(UpdateBaselineAddBaselineNoManifest);
    REGISTER_MESSAGE(UpdateBaselineLocalGitError);
    REGISTER_MESSAGE(UpdateBaselineNoConfiguration);
    REGISTER_MESSAGE(UpdateBaselineNoExistingBuiltinBaseline);
    REGISTER_MESSAGE(UpdateBaselineNoUpdate);
    REGISTER_MESSAGE(UpdateBaselineRemoteGitError);
    REGISTER_MESSAGE(UpdateBaselineUpdatedBaseline);
    REGISTER_MESSAGE(UpgradeInManifest);
    REGISTER_MESSAGE(UpgradeRunWithNoDryRun);
    REGISTER_MESSAGE(UploadedBinaries);
    REGISTER_MESSAGE(UploadedPackagesToVendor);
    REGISTER_MESSAGE(UploadingBinariesToVendor);
    REGISTER_MESSAGE(UploadingBinariesUsingVendor);
    REGISTER_MESSAGE(UseEnvVar);
    REGISTER_MESSAGE(UserWideIntegrationDeleted);
    REGISTER_MESSAGE(UserWideIntegrationRemoved);
    REGISTER_MESSAGE(UsingCommunityTriplet);
    REGISTER_MESSAGE(UsingManifestAt);
    REGISTER_MESSAGE(VcpkgCeIsExperimental);
    REGISTER_MESSAGE(VcpkgCommitTableHeader);
    REGISTER_MESSAGE(VcpkgCompletion);
    REGISTER_MESSAGE(VcpkgDisallowedClassicMode);
    REGISTER_MESSAGE(VcpkgHasCrashed);
    REGISTER_MESSAGE(VcpkgInvalidCommand);
    REGISTER_MESSAGE(InvalidCommentStyle);
    REGISTER_MESSAGE(InvalidUri);
    REGISTER_MESSAGE(VcpkgInVsPrompt);
    REGISTER_MESSAGE(VcpkgRootRequired);
    REGISTER_MESSAGE(VcpkgRootsDir);
    REGISTER_MESSAGE(VcpkgSendMetricsButDisabled);
    REGISTER_MESSAGE(VersionCommandHeader);
    REGISTER_MESSAGE(VersionConflictXML);
    REGISTER_MESSAGE(VersionConstraintViolated);
    REGISTER_MESSAGE(VersionInvalidDate);
    REGISTER_MESSAGE(VersionInvalidRelaxed);
    REGISTER_MESSAGE(VersionInvalidSemver);
    REGISTER_MESSAGE(VersionSpecMismatch);
    REGISTER_MESSAGE(VersionTableHeader);
    REGISTER_MESSAGE(VSExaminedInstances);
    REGISTER_MESSAGE(VSExaminedPaths);
    REGISTER_MESSAGE(VSNoInstances);
    REGISTER_MESSAGE(WaitingForChildrenToExit);
    REGISTER_MESSAGE(WaitingToTakeFilesystemLock);
    REGISTER_MESSAGE(WarningMessageMustUsePrintWarning);
    REGISTER_MESSAGE(WarningsTreatedAsErrors);
    REGISTER_MESSAGE(WarnOnParseConfig);
    REGISTER_MESSAGE(WhileLookingForSpec);
    REGISTER_MESSAGE(WindowsOnlyCommand);
    REGISTER_MESSAGE(WroteNuGetPkgConfInfo);
    REGISTER_MESSAGE(FailedToFetchError);
    REGISTER_MESSAGE(UnexpectedPortName);
    REGISTER_MESSAGE(FailedToLoadUnnamedPortFromPath);
    REGISTER_MESSAGE(TrailingCommaInArray);
    REGISTER_MESSAGE(TrailingCommaInObj);
    REGISTER_MESSAGE(Utf8ConversionFailed);
    REGISTER_MESSAGE(PrebuiltPackages);
    REGISTER_MESSAGE(AndroidHomeDirMissingProps);
    REGISTER_MESSAGE(PortVersionConflict);
    REGISTER_MESSAGE(ToUpdatePackages);
    REGISTER_MESSAGE(AmbiguousConfigDeleteConfigFile);
    REGISTER_MESSAGE(TripletFileNotFound);
    REGISTER_MESSAGE(VcpkgRegistriesCacheIsNotDirectory);
    REGISTER_MESSAGE(FailedToParseNoTopLevelObj);
    REGISTER_MESSAGE(MismatchedManifestAfterReserialize);
    REGISTER_MESSAGE(PortBugIncludeDirInCMakeHelperPort);
    REGISTER_MESSAGE(PortBugMissingIncludeDir);
    REGISTER_MESSAGE(PortBugRestrictedHeaderPaths);
    REGISTER_MESSAGE(PortBugAllowRestrictedHeaders);
    REGISTER_MESSAGE(PortBugDuplicateIncludeFiles);
    REGISTER_MESSAGE(PortBugDebugShareDir);
    REGISTER_MESSAGE(PortBugMissingFile);
    REGISTER_MESSAGE(PortBugMergeLibCMakeDir);
    REGISTER_MESSAGE(PortBugMisplacedCMakeFiles);
    REGISTER_MESSAGE(PortBugDllInLibDir);
    REGISTER_MESSAGE(PortBugMissingLicense);
    REGISTER_MESSAGE(PortBugMissingProvidedUsage);
    REGISTER_MESSAGE(PortBugFoundCopyrightFiles);
    REGISTER_MESSAGE(PortBugFoundExeInBinDir);
    REGISTER_MESSAGE(PortBugSetDllsWithoutExports);
    REGISTER_MESSAGE(PortBugDllAppContainerBitNotSet);
    REGISTER_MESSAGE(BuiltWithIncorrectArchitecture);
    REGISTER_MESSAGE(BinaryWithInvalidArchitecture);
    REGISTER_MESSAGE(ExpectedExtension);
    REGISTER_MESSAGE(FailedToDetermineArchitecture);
    REGISTER_MESSAGE(PortBugFoundDllInStaticBuild);
    REGISTER_MESSAGE(PortBugMismatchedNumberOfBinaries);
    REGISTER_MESSAGE(PortBugFoundDebugBinaries);
    REGISTER_MESSAGE(PortBugFoundReleaseBinaries);
    REGISTER_MESSAGE(PortBugMissingDebugBinaries);
    REGISTER_MESSAGE(PortBugMissingReleaseBinaries);
    REGISTER_MESSAGE(PortBugMissingImportedLibs);
    REGISTER_MESSAGE(PortBugBinDirExists);
    REGISTER_MESSAGE(PortBugDebugBinDirExists);
    REGISTER_MESSAGE(PortBugRemoveBinDir);
    REGISTER_MESSAGE(PortBugFoundEmptyDirectories);
    REGISTER_MESSAGE(PortBugRemoveEmptyDirectories);
    REGISTER_MESSAGE(PortBugMisplacedPkgConfigFiles);
    REGISTER_MESSAGE(PortBugMovePkgConfigFiles);
    REGISTER_MESSAGE(PortBugRemoveEmptyDirs);
    REGISTER_MESSAGE(PortBugInvalidCrtLinkage);
    REGISTER_MESSAGE(PortBugInspectFiles);
    REGISTER_MESSAGE(PortBugOutdatedCRT);
    REGISTER_MESSAGE(PortBugMisplacedFiles);
    REGISTER_MESSAGE(PortBugMisplacedFilesCont);
    REGISTER_MESSAGE(PerformingPostBuildValidation);
    REGISTER_MESSAGE(FailedPostBuildChecks);
    REGISTER_MESSAGE(HelpTxtOptDryRun);
    REGISTER_MESSAGE(HelpTxtOptUseHeadVersion);
    REGISTER_MESSAGE(HelpTxtOptNoDownloads);
    REGISTER_MESSAGE(HelpTxtOptOnlyDownloads);
    REGISTER_MESSAGE(HelpTxtOptOnlyBinCache);
    REGISTER_MESSAGE(HelpTxtOptRecurse);
    REGISTER_MESSAGE(HelpTxtOptKeepGoing);
    REGISTER_MESSAGE(HelpTxtOptEditable);
    REGISTER_MESSAGE(HelpTxtOptUseAria2);
    REGISTER_MESSAGE(HelpTxtOptCleanAfterBuild);
    REGISTER_MESSAGE(HelpTxtOptCleanBuildTreesAfterBuild);
    REGISTER_MESSAGE(HelpTxtOptCleanPkgAfterBuild);
    REGISTER_MESSAGE(HelpTxtOptCleanDownloadsAfterBuild);
    REGISTER_MESSAGE(HelpTxtOptManifestNoDefault);
    REGISTER_MESSAGE(HelpTxtOptEnforcePortChecks);
    REGISTER_MESSAGE(HelpTxtOptAllowUnsupportedPort);
    REGISTER_MESSAGE(HelpTxtOptNoUsage);
    REGISTER_MESSAGE(HelpTxtOptWritePkgConfig);
    REGISTER_MESSAGE(HelpTxtOptManifestFeature);
    REGISTER_MESSAGE(CmdAddVersionOptAll);
    REGISTER_MESSAGE(CmdAddVersionOptOverwriteVersion);
    REGISTER_MESSAGE(CmdAddVersionOptSkipFormatChk);
    REGISTER_MESSAGE(CmdAddVersionOptSkipVersionFormatChk);
    REGISTER_MESSAGE(CmdAddVersionOptVerbose);
    REGISTER_MESSAGE(CISettingsOptExclude);
    REGISTER_MESSAGE(CISettingsOptHostExclude);
    REGISTER_MESSAGE(CISettingsOptXUnit);
    REGISTER_MESSAGE(CISettingsOptCIBase);
    REGISTER_MESSAGE(CISettingsOptFailureLogs);
    REGISTER_MESSAGE(CISettingsOptOutputHashes);
    REGISTER_MESSAGE(CISettingsOptParentHashes);
    REGISTER_MESSAGE(CISettingsOptSkippedCascadeCount);
    REGISTER_MESSAGE(CISwitchOptDryRun);
    REGISTER_MESSAGE(CISwitchOptRandomize);
    REGISTER_MESSAGE(CISwitchOptAllowUnexpectedPassing);
    REGISTER_MESSAGE(CISwitchOptSkipFailures);
    REGISTER_MESSAGE(CISwitchOptXUnitAll);
    REGISTER_MESSAGE(CmdContactOptSurvey);
    REGISTER_MESSAGE(CISettingsVerifyVersion);
    REGISTER_MESSAGE(CISettingsVerifyGitTree);
    REGISTER_MESSAGE(CISettingsExclude);
    REGISTER_MESSAGE(CmdDependInfoOptDot);
    REGISTER_MESSAGE(CmdDependInfoOptDGML);
    REGISTER_MESSAGE(CmdDependInfoOptDepth);
    REGISTER_MESSAGE(CmdDependInfoOptMaxRecurse);
    REGISTER_MESSAGE(CmdDependInfoOptSort);
    REGISTER_MESSAGE(CmdEditOptBuildTrees);
    REGISTER_MESSAGE(CmdEditOptAll);
    REGISTER_MESSAGE(CmdEnvOptions);
    REGISTER_MESSAGE(CmdFetchOptXStderrStatus);
    REGISTER_MESSAGE(CmdFormatManifestOptAll);
    REGISTER_MESSAGE(CmdFormatManifestOptConvertControl);
    REGISTER_MESSAGE(CmdGenerateMessageMapOptOutputComments);
    REGISTER_MESSAGE(CmdGenerateMessageMapOptNoOutputComments);
    REGISTER_MESSAGE(CmdInfoOptInstalled);
    REGISTER_MESSAGE(CmdInfoOptTransitive);
    REGISTER_MESSAGE(CmdNewOptApplication);
    REGISTER_MESSAGE(CmdNewOptSingleFile);
    REGISTER_MESSAGE(CmdNewOptVersionRelaxed);
    REGISTER_MESSAGE(CmdNewOptVersionDate);
    REGISTER_MESSAGE(CmdNewOptVersionString);
    REGISTER_MESSAGE(CmdNewSettingName);
    REGISTER_MESSAGE(CmdNewSettingVersion);
    REGISTER_MESSAGE(CmdRegenerateOptForce);
    REGISTER_MESSAGE(CmdRegenerateOptDryRun);
    REGISTER_MESSAGE(CmdRegenerateOptNormalize);
    REGISTER_MESSAGE(HelpTextOptFullDesc);
    REGISTER_MESSAGE(CmdSettingCopiedFilesLog);
    REGISTER_MESSAGE(CmdSettingInstalledDir);
    REGISTER_MESSAGE(CmdSettingTargetBin);
    REGISTER_MESSAGE(CmdSettingTLogFile);
    REGISTER_MESSAGE(CmdSetInstalledOptDryRun);
    REGISTER_MESSAGE(CmdSetInstalledOptNoUsage);
    REGISTER_MESSAGE(CmdSetInstalledOptWritePkgConfig);
    REGISTER_MESSAGE(CmdUpdateBaselineOptInitial);
    REGISTER_MESSAGE(CmdUpdateBaselineOptDryRun);
    REGISTER_MESSAGE(CmdUpgradeOptNoDryRun);
    REGISTER_MESSAGE(CmdUpgradeOptNoKeepGoing);
    REGISTER_MESSAGE(CmdUpgradeOptAllowUnsupported);
    REGISTER_MESSAGE(CmdXDownloadOptStore);
    REGISTER_MESSAGE(CmdXDownloadOptSkipSha);
    REGISTER_MESSAGE(CmdXDownloadOptSha);
    REGISTER_MESSAGE(CmdXDownloadOptUrl);
    REGISTER_MESSAGE(CmdXDownloadOptHeader);
    REGISTER_MESSAGE(CmdExportOptDryRun);
    REGISTER_MESSAGE(CmdExportOptRaw);
    REGISTER_MESSAGE(CmdExportOptNuget);
    REGISTER_MESSAGE(CmdExportOptIFW);
    REGISTER_MESSAGE(CmdExportOptZip);
    REGISTER_MESSAGE(CmdExportOpt7Zip);
    REGISTER_MESSAGE(CmdExportOptChocolatey);
    REGISTER_MESSAGE(CmdExportOptPrefab);
    REGISTER_MESSAGE(CmdExportOptMaven);
    REGISTER_MESSAGE(CmdExportOptDebug);
    REGISTER_MESSAGE(CmdExportOptInstalled);
    REGISTER_MESSAGE(CmdExportSettingOutput);
    REGISTER_MESSAGE(CmdExportSettingOutputDir);
    REGISTER_MESSAGE(CmdExportSettingNugetID);
    REGISTER_MESSAGE(CmdExportSettingNugetDesc);
    REGISTER_MESSAGE(CmdExportSettingNugetVersion);
    REGISTER_MESSAGE(CmdExportSettingRepoURL);
    REGISTER_MESSAGE(CmdExportSettingPkgDir);
    REGISTER_MESSAGE(CmdExportSettingRepoDir);
    REGISTER_MESSAGE(CmdExportSettingConfigFile);
    REGISTER_MESSAGE(CmdExportSettingInstallerPath);
    REGISTER_MESSAGE(CmdExportSettingChocolateyMaint);
    REGISTER_MESSAGE(CmdExportSettingChocolateyVersion);
    REGISTER_MESSAGE(CmdExportSettingPrefabGroupID);
    REGISTER_MESSAGE(CmdExportSettingPrefabArtifactID);
    REGISTER_MESSAGE(CmdExportSettingPrefabVersion);
    REGISTER_MESSAGE(CmdExportSettingSDKMinVersion);
    REGISTER_MESSAGE(CmdExportSettingSDKTargetVersion);
    REGISTER_MESSAGE(CmdRemoveOptRecurse);
    REGISTER_MESSAGE(CmdRemoveOptDryRun);
    REGISTER_MESSAGE(CmdRemoveOptOutdated);
}
