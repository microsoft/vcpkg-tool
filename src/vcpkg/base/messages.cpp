#include <vcpkg/base/json.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/system.debug.h>

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
            std::vector<std::string> localization_comments; // const after startup

            bool initialized = false;
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
        if (m.initialized)
        {
            write_unlocalized_text_to_stdout(
                Color::error, "double-initialized message context; this is a very serious bug in vcpkg\n");
            Checks::exit_fail(VCPKG_LINE_INFO);
        }
        m.localized_strings.resize(m.names.size());
        m.initialized = true;

        std::set<StringLiteral, std::less<>> names_set(m.names.begin(), m.names.end());
        if (names_set.size() < m.names.size())
        {
            // This will not trigger on any correct code path, so it's fine to use a naive O(n^2)
            for (size_t i = 0; i < m.names.size() - 1; ++i)
            {
                for (size_t j = i + 1; j < m.names.size(); ++j)
                {
                    if (m.names[i] == m.names[j])
                    {
                        write_unlocalized_text_to_stdout(
                            Color::error,
                            fmt::format("INTERNAL ERROR: localization message '{}' has been declared multiple times\n",
                                        m.names[i]));
                        write_unlocalized_text_to_stdout(Color::error, fmt::format("INTERNAL ERROR: first message:\n"));
                        write_unlocalized_text_to_stdout(Color::none, m.default_strings[i]);
                        write_unlocalized_text_to_stdout(Color::error,
                                                         fmt::format("\nINTERNAL ERROR: second message:\n"));
                        write_unlocalized_text_to_stdout(Color::none, m.default_strings[j]);
                        write_unlocalized_text_to_stdout(Color::none, "\n");
                        ::abort();
                    }
                }
            }
            Checks::unreachable(VCPKG_LINE_INFO);
        }
    }
    static void load_from_message_map(const Json::Object& message_map)
    {
        Messages& m = messages();
        std::vector<std::string> names_without_localization;

        for (::size_t index = 0; index < m.names.size(); ++index)
        {
            const auto& name = m.names[index];
            if (auto p = message_map.get(m.names[index]))
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

    static std::string locale_file_name(StringView language)
    {
        std::string filename = "messages.";
        filename.append(language.begin(), language.end()).append(".json");
        return filename;
    }

    void threadunsafe_initialize_context(const Filesystem& fs, StringView language, const Path& locale_base)
    {
        threadunsafe_initialize_context();

        auto path_to_locale = locale_base / locale_file_name(language);

        auto message_map = Json::parse_file(VCPKG_LINE_INFO, fs, path_to_locale);
        if (!message_map.first.is_object())
        {
            write_unlocalized_text_to_stdout(
                Color::error,
                fmt::format("Invalid locale file '{}' - locale file must be an object.\n", path_to_locale));
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        load_from_message_map(message_map.first.object(VCPKG_LINE_INFO));
    }

    ::size_t detail::number_of_messages() { return messages().names.size(); }

    std::string detail::format_examples_for_args(StringView extra_comment,
                                                 const detail::FormatArgAbi* args,
                                                 std::size_t arg_count)
    {
        std::vector<std::string> blocks;
        if (!extra_comment.empty())
        {
            blocks.emplace_back(extra_comment.data(), extra_comment.size());
        }

        for (std::size_t idx = 0; idx < arg_count; ++idx)
        {
            auto& arg = args[idx];
            if (arg.example[0] != '\0')
            {
                blocks.emplace_back(fmt::format("An example of {{{}}} is {}.", arg.name, arg.example));
            }
        }

        return Strings::join(" ", blocks);
    }

    ::size_t detail::startup_register_message(StringLiteral name, StringLiteral format_string, std::string&& comment)
    {
        Messages& m = messages();
        const auto res = m.names.size();
        m.names.push_back(name);
        m.default_strings.push_back(format_string);
        m.localization_comments.push_back(std::move(comment));
        return res;
    }

    StringView detail::get_format_string(::size_t index)
    {
        Messages& m = messages();
        Checks::check_exit(VCPKG_LINE_INFO, m.localized_strings.size() == m.default_strings.size());
        Checks::check_exit(VCPKG_LINE_INFO, index < m.default_strings.size());
        const auto& localized = m.localized_strings[index];
        if (localized.empty())
        {
            return m.default_strings[index];
        }
        else
        {
            return localized;
        }
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
        auto fmt_string = get_format_string(index);
        try
        {
            return LocalizedString::from_raw(fmt::vformat({fmt_string.data(), fmt_string.size()}, args));
        }
        catch (...)
        {
            auto default_format_string = get_default_format_string(index);
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
    MessageSink& stdout_sink = stdout_sink_instance;
    MessageSink& stderr_sink = stderr_sink_instance;

    REGISTER_MESSAGE(AddArtifactOnlyOne);
    REGISTER_MESSAGE(AddFirstArgument);
    REGISTER_MESSAGE(AddingCompletionEntry);
    REGISTER_MESSAGE(AddPortRequiresManifest);
    REGISTER_MESSAGE(AddPortSucceeded);
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
    REGISTER_MESSAGE(AlreadyInstalled);
    REGISTER_MESSAGE(AlreadyInstalledNotHead);
    REGISTER_MESSAGE(AnotherInstallationInProgress);
    REGISTER_MESSAGE(AppliedUserIntegration);
    REGISTER_MESSAGE(AssetSourcesArg);
    REGISTER_MESSAGE(AttemptingToFetchPackagesFromVendor);
    REGISTER_MESSAGE(AuthenticationMayRequireManualAction);
    REGISTER_MESSAGE(AutomaticLinkingForMSBuildProjects);
    REGISTER_MESSAGE(AutoSettingEnvVar);
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
    REGISTER_MESSAGE(ChecksFailedCheck);
    REGISTER_MESSAGE(ChecksUnreachableCode);
    REGISTER_MESSAGE(ChecksUpdateVcpkg);
    REGISTER_MESSAGE(CiBaselineAllowUnexpectedPassingRequiresBaseline);
    REGISTER_MESSAGE(CiBaselineDisallowedCascade);
    REGISTER_MESSAGE(CiBaselineRegression);
    REGISTER_MESSAGE(CiBaselineRegressionHeader);
    REGISTER_MESSAGE(CiBaselineUnexpectedPass);
    REGISTER_MESSAGE(CmakeTargetsExcluded);
    REGISTER_MESSAGE(CMakeTargetsUsage);
    REGISTER_MESSAGE(CMakeTargetsUsageHeuristicMessage);
    REGISTER_MESSAGE(CMakeToolChainFile);
    REGISTER_MESSAGE(CommandFailed);
    REGISTER_MESSAGE(CompressFolderFailed);
    REGISTER_MESSAGE(ConflictingValuesForOption);
    REGISTER_MESSAGE(CopyrightIsDir);
    REGISTER_MESSAGE(CouldNotDeduceNugetIdAndVersion);
    REGISTER_MESSAGE(CreatedNuGetPackage);
    REGISTER_MESSAGE(CurlReportedUnexpectedResults);
    REGISTER_MESSAGE(DefaultBrowserLaunched);
    REGISTER_MESSAGE(DefaultFlag);
    REGISTER_MESSAGE(DefaultPathToBinaries);
    REGISTER_MESSAGE(DetectCompilerHash);
    REGISTER_MESSAGE(DownloadAvailable);
    REGISTER_MESSAGE(DownloadedSources);
    REGISTER_MESSAGE(DownloadingVcpkgCeBundle);
    REGISTER_MESSAGE(DownloadingVcpkgCeBundleLatest);
    REGISTER_MESSAGE(DownloadRootsDir);
    REGISTER_MESSAGE(DuplicateCommandOption);
    REGISTER_MESSAGE(DuplicateOptions);
    REGISTER_MESSAGE(EmailVcpkgTeam);
    REGISTER_MESSAGE(EmptyArg);
    REGISTER_MESSAGE(EmptyLicenseExpression);
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
    REGISTER_MESSAGE(ErrorUnableToDetectCompilerInfo);
    REGISTER_MESSAGE(ErrorVcvarsUnsupported);
    REGISTER_MESSAGE(ErrorVsCodeNotFound);
    REGISTER_MESSAGE(ErrorVsCodeNotFoundPathExamined);
    REGISTER_MESSAGE(ErrorWhileParsing);
    REGISTER_MESSAGE(ErrorWhileWriting);
    REGISTER_MESSAGE(ExcludedPackage);
    REGISTER_MESSAGE(ExpectedCharacterHere);
    REGISTER_MESSAGE(ExpectedFailOrSkip);
    REGISTER_MESSAGE(ExpectedPortName);
    REGISTER_MESSAGE(ExpectedTripletName);
    REGISTER_MESSAGE(ExpectedValueForOption);
    REGISTER_MESSAGE(ExtendedDocumentationAtUrl);
    REGISTER_MESSAGE(FailedToExtract);
    REGISTER_MESSAGE(FailedToParseSerializedBinParagraph);
    REGISTER_MESSAGE(FailedToParseCMakeConsoleOut);
    REGISTER_MESSAGE(FailedToProvisionCe);
    REGISTER_MESSAGE(FailedToRunToolToDetermineVersion);
    REGISTER_MESSAGE(FailedToStoreBackToMirror);
    REGISTER_MESSAGE(FailedToStoreBinaryCache);
    REGISTER_MESSAGE(FailedVendorAuthentication);
    REGISTER_MESSAGE(FeedbackAppreciated);
    REGISTER_MESSAGE(FishCompletion);
    REGISTER_MESSAGE(ForceSystemBinariesOnWeirdPlatforms);
    REGISTER_MESSAGE(FormattedParseMessageExpression);
    REGISTER_MESSAGE(GenerateMsgErrorParsingFormatArgs);
    REGISTER_MESSAGE(GenerateMsgIncorrectComment);
    REGISTER_MESSAGE(GenerateMsgNoArgumentValue);
    REGISTER_MESSAGE(GenerateMsgNoCommentValue);
    REGISTER_MESSAGE(GitCommandFailed);
    REGISTER_MESSAGE(GitStatusOutputExpectedFileName);
    REGISTER_MESSAGE(GitStatusOutputExpectedNewLine);
    REGISTER_MESSAGE(GitStatusOutputExpectedRenameOrNewline);
    REGISTER_MESSAGE(GitStatusUnknownFileStatus);
    REGISTER_MESSAGE(GitUnexpectedCommandOutput);
    REGISTER_MESSAGE(HashFileFailureToRead);
    REGISTER_MESSAGE(HeaderOnlyUsage);
    REGISTER_MESSAGE(HelpContactCommand);
    REGISTER_MESSAGE(HelpCreateCommand);
    REGISTER_MESSAGE(HelpDependInfoCommand);
    REGISTER_MESSAGE(HelpEditCommand);
    REGISTER_MESSAGE(HelpEnvCommand);
    REGISTER_MESSAGE(HelpExampleCommand);
    REGISTER_MESSAGE(HelpExportCommand);
    REGISTER_MESSAGE(HelpFormatManifestCommand);
    REGISTER_MESSAGE(HelpHashCommand);
    REGISTER_MESSAGE(HelpHistoryCommand);
    REGISTER_MESSAGE(HelpInitializeRegistryCommand);
    REGISTER_MESSAGE(HelpInstallCommand);
    REGISTER_MESSAGE(HelpListCommand);
    REGISTER_MESSAGE(HelpOwnsCommand);
    REGISTER_MESSAGE(HelpRemoveCommand);
    REGISTER_MESSAGE(HelpRemoveOutdatedCommand);
    REGISTER_MESSAGE(HelpResponseFileCommand);
    REGISTER_MESSAGE(HelpSearchCommand);
    REGISTER_MESSAGE(HelpTopicCommand);
    REGISTER_MESSAGE(HelpTopicsCommand);
    REGISTER_MESSAGE(HelpUpdateCommand);
    REGISTER_MESSAGE(HelpUpgradeCommand);
    REGISTER_MESSAGE(HelpVersionCommand);
    REGISTER_MESSAGE(IllegalFeatures);
    REGISTER_MESSAGE(IllegalPlatformSpec);
    REGISTER_MESSAGE(IncorrectNumberOfArgs);
    REGISTER_MESSAGE(InfoSetEnvVar);
    REGISTER_MESSAGE(InstallingFromLocation);
    REGISTER_MESSAGE(InstallingPackage);
    REGISTER_MESSAGE(InstallPackageInstruction);
    REGISTER_MESSAGE(InstallRootDir);
    REGISTER_MESSAGE(InstallWithSystemManager);
    REGISTER_MESSAGE(InstallWithSystemManagerMono);
    REGISTER_MESSAGE(InstallWithSystemManagerPkg);
    REGISTER_MESSAGE(IntegrationFailed);
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
    REGISTER_MESSAGE(InvalidFormatString);
    REGISTER_MESSAGE(InvalidLinkage);
    REGISTER_MESSAGE(JsonErrorFailedToParse);
    REGISTER_MESSAGE(JsonErrorFailedToRead);
    REGISTER_MESSAGE(JsonErrorMustBeAnObject);
    REGISTER_MESSAGE(JsonSwitch);
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
    REGISTER_MESSAGE(Missing7zHeader);
    REGISTER_MESSAGE(MissingExtension);
    REGISTER_MESSAGE(MissmatchedBinParagraphs);
    REGISTER_MESSAGE(MonoInstructions);
    REGISTER_MESSAGE(MsiexecFailedToExtract);
    REGISTER_MESSAGE(MultiArch);
    REGISTER_MESSAGE(NavigateToNPS);
    REGISTER_MESSAGE(NewConfigurationAlreadyExists);
    REGISTER_MESSAGE(NewManifestAlreadyExists);
    REGISTER_MESSAGE(NewNameCannotBeEmpty);
    REGISTER_MESSAGE(NewOnlyOneVersionKind);
    REGISTER_MESSAGE(NewSpecifyNameVersionOrApplication);
    REGISTER_MESSAGE(NewVersionCannotBeEmpty);
    REGISTER_MESSAGE(NoArgumentsForOption);
    REGISTER_MESSAGE(NoCachedPackages);
    REGISTER_MESSAGE(NoLocalizationForMessages);
    REGISTER_MESSAGE(NoRegistryForPort);
    REGISTER_MESSAGE(NugetPackageFileSucceededButCreationFailed);
    REGISTER_MESSAGE(OriginalBinParagraphHeader);
    REGISTER_MESSAGE(PackageFailedtWhileExtracting);
    REGISTER_MESSAGE(PackageRootDir);
    REGISTER_MESSAGE(PackingVendorFailed);
    REGISTER_MESSAGE(ParseControlErrorInfoInvalidFields);
    REGISTER_MESSAGE(ParseControlErrorInfoMissingFields);
    REGISTER_MESSAGE(ParseControlErrorInfoTypesEntry);
    REGISTER_MESSAGE(ParseControlErrorInfoWhileLoading);
    REGISTER_MESSAGE(ParseControlErrorInfoWrongTypeFields);
    REGISTER_MESSAGE(PortDependencyConflict);
    REGISTER_MESSAGE(PortNotInBaseline);
    REGISTER_MESSAGE(PortSupportsField);
    REGISTER_MESSAGE(PreviousIntegrationFileRemains);
    REGISTER_MESSAGE(ProcessorArchitectureMalformed);
    REGISTER_MESSAGE(ProcessorArchitectureMissing);
    REGISTER_MESSAGE(ProcessorArchitectureW6432Malformed);
    REGISTER_MESSAGE(ProgramReturnedNonzeroExitCode);
    REGISTER_MESSAGE(PushingVendorFailed);
    REGISTER_MESSAGE(ReplaceSecretsError);
    REGISTER_MESSAGE(RestoredPackage);
    REGISTER_MESSAGE(RestoredPackagesFromVendor);
    REGISTER_MESSAGE(ResultsHeader);
    REGISTER_MESSAGE(SerializedBinParagraphHeader);
    REGISTER_MESSAGE(SettingEnvVar);
    REGISTER_MESSAGE(SourceFieldPortNameMismatch);
    REGISTER_MESSAGE(SpecifiedFeatureTurnedOff);
    REGISTER_MESSAGE(SpecifyDirectoriesContaining);
    REGISTER_MESSAGE(SpecifyDirectoriesWhenSearching);
    REGISTER_MESSAGE(SpecifyHostArch);
    REGISTER_MESSAGE(SpecifyTargetArch);
    REGISTER_MESSAGE(StoredBinaryCache);
    REGISTER_MESSAGE(SuggestStartingBashShell);
    REGISTER_MESSAGE(SuggestUpdateVcpkg);
    REGISTER_MESSAGE(SupportedPort);
    REGISTER_MESSAGE(SystemApiErrorMessage);
    REGISTER_MESSAGE(ToolFetchFailed);
    REGISTER_MESSAGE(ToolInWin10);
    REGISTER_MESSAGE(TwoFeatureFlagsSpecified);
    REGISTER_MESSAGE(UndeterminedToolChainForTriplet);
    REGISTER_MESSAGE(UnexpectedErrorDuringBulkDownload);
    REGISTER_MESSAGE(UnexpectedExtension);
    REGISTER_MESSAGE(UnexpectedFormat);
    REGISTER_MESSAGE(UnexpectedToolOutput);
    REGISTER_MESSAGE(UnknownBaselineFileContent);
    REGISTER_MESSAGE(UnknownBinaryProviderType);
    REGISTER_MESSAGE(UnknownOptions);
    REGISTER_MESSAGE(UnknownParameterForIntegrate);
    REGISTER_MESSAGE(UnknownPolicySetting);
    REGISTER_MESSAGE(UnknownSettingForBuildType);
    REGISTER_MESSAGE(UnknownTool);
    REGISTER_MESSAGE(UnknownVariablesInTemplate);
    REGISTER_MESSAGE(UnsupportedPort);
    REGISTER_MESSAGE(UnsupportedPortDependency);
    REGISTER_MESSAGE(UnsupportedShortOptions);
    REGISTER_MESSAGE(UnsupportedSystemName);
    REGISTER_MESSAGE(UnsupportedToolchain);
    REGISTER_MESSAGE(UpdateBaselineAddBaselineNoManifest);
    REGISTER_MESSAGE(UpdateBaselineLocalGitError);
    REGISTER_MESSAGE(UpdateBaselineNoConfiguration);
    REGISTER_MESSAGE(UpdateBaselineNoExistingBuiltinBaseline);
    REGISTER_MESSAGE(UpdateBaselineNoUpdate);
    REGISTER_MESSAGE(UpdateBaselineRemoteGitError);
    REGISTER_MESSAGE(UpdateBaselineUpdatedBaseline);
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
    REGISTER_MESSAGE(VcpkgCompletion);
    REGISTER_MESSAGE(VcpkgDisallowedClassicMode);
    REGISTER_MESSAGE(VcpkgHasCrashed);
    REGISTER_MESSAGE(VcpkgInvalidCommand);
    REGISTER_MESSAGE(VcpkgRootsDir);
    REGISTER_MESSAGE(VcpkgSendMetricsButDisabled);
    REGISTER_MESSAGE(VersionCommandHeader);
    REGISTER_MESSAGE(VersionConstraintViolated);
    REGISTER_MESSAGE(VersionInvalidDate);
    REGISTER_MESSAGE(VersionInvalidRelaxed);
    REGISTER_MESSAGE(VersionInvalidSemver);
    REGISTER_MESSAGE(VersionSpecMismatch);
    REGISTER_MESSAGE(VSExaminedInstances);
    REGISTER_MESSAGE(VSExaminedPaths);
    REGISTER_MESSAGE(VSNoInstances);
    REGISTER_MESSAGE(WaitingForChildrenToExit);
    REGISTER_MESSAGE(WaitingToTakeFilesystemLock);
    REGISTER_MESSAGE(WarningMessageMustUsePrintWarning);
    REGISTER_MESSAGE(WarningsTreatedAsErrors);
}
