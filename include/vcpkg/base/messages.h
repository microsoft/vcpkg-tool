#pragma once

#include <vcpkg/base/fwd/files.h>
#include <vcpkg/base/fwd/json.h>
#include <vcpkg/base/fwd/messages.h>

#include <vcpkg/base/format.h>
#include <vcpkg/base/lineinfo.h>
#include <vcpkg/base/stringview.h>

#include <string>
#include <type_traits>
#include <utility>

namespace vcpkg
{
    namespace msg::detail
    {
        template<class Tag, class Type>
        struct MessageArgument;
    }
    namespace msg
    {
        template<class Message, class... Tags, class... Ts>
        LocalizedString format(Message, detail::MessageArgument<Tags, Ts>... args);
    }

    struct LocalizedString
    {
        LocalizedString() = default;
        operator StringView() const noexcept { return m_data; }
        const std::string& data() const noexcept { return m_data; }
        const std::string& to_string() const noexcept { return m_data; }
        std::string extract_data() { return std::exchange(m_data, ""); }

        static LocalizedString from_raw(std::string&& s) { return LocalizedString(std::move(s)); }

        template<class StringLike,
                 std::enable_if_t<std::is_constructible<StringView, const StringLike&>::value, int> = 0>
        static LocalizedString from_raw(const StringLike& s)
        {
            return LocalizedString(StringView(s));
        }
        LocalizedString& append_raw(char c)
        {
            m_data.push_back(c);
            return *this;
        }
        LocalizedString& append_raw(StringView s)
        {
            m_data.append(s.begin(), s.size());
            return *this;
        }
        template<class... Args>
        LocalizedString& append_fmt_raw(fmt::string_view s, const Args&... args)
        {
            m_data.append(fmt::format(s, args...));
            return *this;
        }
        LocalizedString& append(const LocalizedString& s)
        {
            m_data.append(s.m_data);
            return *this;
        }
        template<class Message, class... Args>
        LocalizedString& append(Message m, const Args&... args)
        {
            return append(msg::format(m, args...));
        }

        LocalizedString& append_indent(size_t indent = 1)
        {
            m_data.append(indent * 4, ' ');
            return *this;
        }

        friend const char* to_printf_arg(const LocalizedString& s) { return s.data().c_str(); }

        friend bool operator==(const LocalizedString& lhs, const LocalizedString& rhs)
        {
            return lhs.data() == rhs.data();
        }

        friend bool operator!=(const LocalizedString& lhs, const LocalizedString& rhs)
        {
            return lhs.data() != rhs.data();
        }

        friend bool operator<(const LocalizedString& lhs, const LocalizedString& rhs)
        {
            return lhs.data() < rhs.data();
        }

        friend bool operator<=(const LocalizedString& lhs, const LocalizedString& rhs)
        {
            return lhs.data() <= rhs.data();
        }

        friend bool operator>(const LocalizedString& lhs, const LocalizedString& rhs)
        {
            return lhs.data() > rhs.data();
        }

        friend bool operator>=(const LocalizedString& lhs, const LocalizedString& rhs)
        {
            return lhs.data() >= rhs.data();
        }

        bool empty() { return m_data.empty(); }
        void clear() { m_data.clear(); }

    private:
        std::string m_data;

        explicit LocalizedString(StringView data) : m_data(data.data(), data.size()) { }
        explicit LocalizedString(std::string&& data) : m_data(std::move(data)) { }
    };
}

VCPKG_FORMAT_WITH_TO_STRING(vcpkg::LocalizedString);

namespace vcpkg::msg
{
    namespace detail
    {
        template<class Tag, class Type>
        struct MessageArgument
        {
            const Type* parameter; // always valid
        };

        template<class... Tags>
        struct MessageCheckFormatArgs
        {
            static constexpr void check_format_args(const Tags&...) noexcept { }
        };

        LocalizedString internal_vformat(::size_t index, fmt::format_args args);

        template<class... Args>
        MessageCheckFormatArgs<Args...> make_message_check_format_args(const Args&... args); // not defined

        struct FormatArgAbi
        {
            const char* name;
            const char* example;
        };

        std::string format_examples_for_args(StringView extra_comment, const FormatArgAbi* args, std::size_t arg_count);

        inline std::string get_examples_for_args(StringView extra_comment, const MessageCheckFormatArgs<>&)
        {
            return extra_comment.to_string();
        }

        template<class Arg0, class... Args>
        std::string get_examples_for_args(StringView extra_comment, const MessageCheckFormatArgs<Arg0, Args...>&)
        {
            FormatArgAbi abi[] = {FormatArgAbi{Arg0::name, Arg0::example}, FormatArgAbi{Args::name, Args::example}...};
            return format_examples_for_args(extra_comment, abi, 1 + sizeof...(Args));
        }

        ::size_t startup_register_message(StringLiteral name, StringLiteral format_string, std::string&& comment);

        ::size_t number_of_messages();

        // REQUIRES: index < last_message_index()
        StringView get_format_string(::size_t index);
        // REQUIRES: index < last_message_index()
        StringView get_message_name(::size_t index);
        // REQUIRES: index < last_message_index()
        StringView get_default_format_string(::size_t index);
        // REQUIRES: index < last_message_index()
        StringView get_localization_comment(::size_t index);
    }

    // load from "locale_base/messages.${language}.json"
    void threadunsafe_initialize_context(const Filesystem& fs, StringView language, const Path& locale_base);
    // initialize without any localized messages (use default messages only)
    void threadunsafe_initialize_context();

    template<class Message, class... Tags, class... Ts>
    LocalizedString format(Message, detail::MessageArgument<Tags, Ts>... args)
    {
        // avoid generating code, but still typecheck
        // (and avoid unused typedef warnings)
        static_assert((Message::check_format_args((Tags{})...), true), "");
        return detail::internal_vformat(Message::index,
                                        fmt::make_format_args(fmt::arg(Tags::name, *args.parameter)...));
    }

    inline void println() { msg::write_unlocalized_text_to_stdout(Color::none, "\n"); }

    inline void print(Color c, const LocalizedString& s) { msg::write_unlocalized_text_to_stdout(c, s); }
    inline void print(const LocalizedString& s) { msg::write_unlocalized_text_to_stdout(Color::none, s); }
    inline void println(Color c, const LocalizedString& s)
    {
        msg::write_unlocalized_text_to_stdout(c, s);
        msg::write_unlocalized_text_to_stdout(Color::none, "\n");
    }
    inline void println(const LocalizedString& s)
    {
        msg::write_unlocalized_text_to_stdout(Color::none, s);
        msg::write_unlocalized_text_to_stdout(Color::none, "\n");
    }

    template<class Message, class... Ts>
    typename Message::is_message_type print(Message m, Ts... args)
    {
        print(format(m, args...));
    }
    template<class Message, class... Ts>
    typename Message::is_message_type println(Message m, Ts... args)
    {
        print(format(m, args...).append_raw('\n'));
    }

    template<class Message, class... Ts>
    typename Message::is_message_type print(Color c, Message m, Ts... args)
    {
        print(c, format(m, args...));
    }
    template<class Message, class... Ts>
    typename Message::is_message_type println(Color c, Message m, Ts... args)
    {
        print(c, format(m, args...).append_raw('\n'));
    }

// these use `constexpr static` instead of `inline` in order to work with GCC 6;
// they are trivial and empty, and their address does not matter, so this is not a problem
#define DECLARE_MSG_ARG(NAME, EXAMPLE)                                                                                 \
    constexpr static struct NAME##_t                                                                                   \
    {                                                                                                                  \
        constexpr static const char* name = #NAME;                                                                     \
        constexpr static const char* example = EXAMPLE;                                                                \
        template<class T>                                                                                              \
        detail::MessageArgument<NAME##_t, T> operator=(const T& t) const noexcept                                      \
        {                                                                                                              \
            return detail::MessageArgument<NAME##_t, T>{&t};                                                           \
        }                                                                                                              \
    } NAME = {}

    DECLARE_MSG_ARG(error, "");
    DECLARE_MSG_ARG(value, "");
    DECLARE_MSG_ARG(pretty_value, "");
    DECLARE_MSG_ARG(expected, "");
    DECLARE_MSG_ARG(actual, "");
    DECLARE_MSG_ARG(list, "");
    DECLARE_MSG_ARG(old_value, "");
    DECLARE_MSG_ARG(new_value, "");

    DECLARE_MSG_ARG(actual_version, "1.3.8");
    DECLARE_MSG_ARG(arch, "x64");
    DECLARE_MSG_ARG(base_url, "azblob://");
    DECLARE_MSG_ARG(binary_source, "azblob");
    DECLARE_MSG_ARG(build_result, "One of the BuildResultXxx messages (such as BuildResultSucceeded/SUCCEEDED)");
    DECLARE_MSG_ARG(column, "42");
    DECLARE_MSG_ARG(command_line, "vcpkg install zlib");
    DECLARE_MSG_ARG(command_name, "install");
    DECLARE_MSG_ARG(count, "42");
    DECLARE_MSG_ARG(elapsed, "3.532 min");
    DECLARE_MSG_ARG(error_msg, "File Not Found");
    DECLARE_MSG_ARG(exit_code, "127");
    DECLARE_MSG_ARG(expected_version, "1.3.8");
    DECLARE_MSG_ARG(new_scheme, "version");
    DECLARE_MSG_ARG(old_scheme, "version-string");
    DECLARE_MSG_ARG(option, "editable");
    DECLARE_MSG_ARG(package_name, "zlib");
    DECLARE_MSG_ARG(path, "/foo/bar");
    DECLARE_MSG_ARG(row, "42");
    DECLARE_MSG_ARG(spec, "zlib:x64-windows");
    DECLARE_MSG_ARG(system_api, "CreateProcessW");
    DECLARE_MSG_ARG(system_name, "Darwin");
    DECLARE_MSG_ARG(tool_name, "aria2");
    DECLARE_MSG_ARG(triplet, "x64-windows");
    DECLARE_MSG_ARG(url, "https://github.com/microsoft/vcpkg");
    DECLARE_MSG_ARG(vcpkg_line_info, "/a/b/foo.cpp(13)");
    DECLARE_MSG_ARG(vendor, "Azure");
    DECLARE_MSG_ARG(version, "1.3.8");
    DECLARE_MSG_ARG(action_index, "340");
    DECLARE_MSG_ARG(env_var, "VCPKG_DEFAULT_TRIPLET");
    DECLARE_MSG_ARG(extension, ".exe");

#undef DECLARE_MSG_ARG

#define DECLARE_MESSAGE(NAME, ARGS, COMMENT, ...)                                                                      \
    constexpr struct NAME##_msg_t : decltype(::vcpkg::msg::detail::make_message_check_format_args ARGS)                \
    {                                                                                                                  \
        using is_message_type = void;                                                                                  \
        static constexpr ::vcpkg::StringLiteral name = #NAME;                                                          \
        static constexpr ::vcpkg::StringLiteral extra_comment = COMMENT;                                               \
        static constexpr ::vcpkg::StringLiteral default_format_string = __VA_ARGS__;                                   \
        static const ::size_t index;                                                                                   \
    } msg##NAME VCPKG_UNUSED = {}

#define REGISTER_MESSAGE(NAME)                                                                                         \
    const ::size_t NAME##_msg_t::index = ::vcpkg::msg::detail::startup_register_message(                               \
        NAME##_msg_t::name,                                                                                            \
        NAME##_msg_t::default_format_string,                                                                           \
        ::vcpkg::msg::detail::get_examples_for_args(NAME##_msg_t::extra_comment, NAME##_msg_t{}))

#define DECLARE_AND_REGISTER_MESSAGE(NAME, ARGS, COMMENT, ...)                                                         \
    DECLARE_MESSAGE(NAME, ARGS, COMMENT, __VA_ARGS__);                                                                 \
    REGISTER_MESSAGE(NAME)

    DECLARE_MESSAGE(SeeURL, (msg::url), "", "See {url} for more information.");
    DECLARE_MESSAGE(NoteMessage, (), "", "note: ");
    DECLARE_MESSAGE(WarningMessage, (), "", "warning: ");
    DECLARE_MESSAGE(ErrorMessage, (), "", "error: ");
    DECLARE_MESSAGE(InternalErrorMessage, (), "", "internal error: ");
    DECLARE_MESSAGE(
        InternalErrorMessageContact,
        (),
        "",
        "Please open an issue at "
        "https://github.com/microsoft/vcpkg/issues/new?template=other-type-of-bug-report.md&labels=category:vcpkg-bug "
        "with detailed steps to reproduce the problem.");
    DECLARE_MESSAGE(BothYesAndNoOptionSpecifiedError,
                    (msg::option),
                    "",
                    "cannot specify both --no-{option} and --{option}.");

    void println_warning(const LocalizedString& s);
    template<class Message, class... Ts>
    typename Message::is_message_type println_warning(Message m, Ts... args)
    {
        println_warning(format(m, args...));
    }

    void println_error(const LocalizedString& s);
    template<class Message, class... Ts>
    typename Message::is_message_type println_error(Message m, Ts... args)
    {
        println_error(format(m, args...));
    }

    template<class Message, class... Ts, class = typename Message::is_message_type>
    LocalizedString format_warning(Message m, Ts... args)
    {
        return format(msgWarningMessage).append(m, args...);
    }

    template<class Message, class... Ts, class = typename Message::is_message_type>
    LocalizedString format_error(Message m, Ts... args)
    {
        return format(msgErrorMessage).append(m, args...);
    }

}

namespace vcpkg
{
    struct MessageSink
    {
        virtual void print(Color c, StringView sv) = 0;

        void println() { this->print(Color::none, "\n"); }
        void print(const LocalizedString& s) { this->print(Color::none, s); }
        void println(Color c, const LocalizedString& s)
        {
            this->print(c, s);
            this->print(Color::none, "\n");
        }
        inline void println(const LocalizedString& s)
        {
            this->print(Color::none, s);
            this->print(Color::none, "\n");
        }

        template<class Message, class... Ts>
        typename Message::is_message_type print(Message m, Ts... args)
        {
            this->print(Color::none, msg::format(m, args...));
        }

        template<class Message, class... Ts>
        typename Message::is_message_type println(Message m, Ts... args)
        {
            this->print(Color::none, msg::format(m, args...).append_raw('\n'));
        }

        template<class Message, class... Ts>
        typename Message::is_message_type print(Color c, Message m, Ts... args)
        {
            this->print(c, msg::format(m, args...));
        }

        template<class Message, class... Ts>
        typename Message::is_message_type println(Color c, Message m, Ts... args)
        {
            this->print(c, msg::format(m, args...).append_raw('\n'));
        }

        MessageSink(const MessageSink&) = delete;
        MessageSink& operator=(const MessageSink&) = delete;

    protected:
        MessageSink() = default;
        ~MessageSink() = default;
    };

    extern MessageSink& null_sink;
    extern MessageSink& stdout_sink;
    extern MessageSink& stderr_sink;

    DECLARE_MESSAGE(AddArtifactOnlyOne,
                    (msg::command_line),
                    "",
                    "'{command_line}' can only add one artifact at a time.");
    DECLARE_MESSAGE(AddFirstArgument,
                    (msg::command_line),
                    "",
                    "The first argument to '{command_line}' must be 'artifact' or 'port'.");
    DECLARE_MESSAGE(AddingCompletionEntry, (msg::path), "", "Adding vcpkg completion entry to {path}.");
    DECLARE_MESSAGE(AddPortRequiresManifest,
                    (msg::command_line),
                    "",
                    "'{command_line}' requires an active manifest file.");
    DECLARE_MESSAGE(AddPortSucceeded, (), "", "Succeeded in adding ports to vcpkg.json file.");
    DECLARE_MESSAGE(AddTripletExpressionNotAllowed,
                    (msg::package_name, msg::triplet),
                    "",
                    "triplet expressions are not allowed here. You may want to change "
                    "`{package_name}:{triplet}` to `{package_name}` instead.");
    DECLARE_MESSAGE(AddVersionAddedVersionToFile, (msg::version, msg::path), "", "added version {version} to {path}");
    DECLARE_MESSAGE(AddVersionCommitChangesReminder, (), "", "Did you remember to commit your changes?");
    DECLARE_MESSAGE(AddVersionCommitResultReminder, (), "", "Don't forget to commit the result!");
    DECLARE_MESSAGE(AddVersionDetectLocalChangesError,
                    (),
                    "",
                    "skipping detection of local changes due to unexpected format in git status output");
    DECLARE_MESSAGE(AddVersionFileNotFound, (msg::path), "", "couldn't find required file {path}");
    DECLARE_MESSAGE(AddVersionFormatPortSuggestion, (msg::command_line), "", "Run `{command_line}` to format the file");
    DECLARE_MESSAGE(AddVersionIgnoringOptionAll,
                    (msg::option),
                    "The -- before {option} must be preserved as they're part of the help message for the user.",
                    "ignoring --{option} since a port name argument was provided");
    DECLARE_MESSAGE(AddVersionLoadPortFailed, (msg::package_name), "", "can't load port {package_name}");
    DECLARE_MESSAGE(AddVersionNewFile, (), "", "(new file)");
    DECLARE_MESSAGE(AddVersionNewShaIs, (msg::value), "{value} is a 40-digit hexadecimal SHA", "new SHA: {value}");
    DECLARE_MESSAGE(AddVersionNoFilesUpdated, (), "", "No files were updated");
    DECLARE_MESSAGE(AddVersionNoFilesUpdatedForPort,
                    (msg::package_name),
                    "",
                    "No files were updated for {package_name}");
    DECLARE_MESSAGE(AddVersionNoGitSha, (msg::package_name), "", "can't obtain SHA for port {package_name}");
    DECLARE_MESSAGE(AddVersionOldShaIs, (msg::value), "{value} is a 40-digit hexadecimal SHA", "old SHA: {value}");
    DECLARE_MESSAGE(AddVersionOverwriteOptionSuggestion,
                    (msg::option),
                    "The -- before {option} must be preserved as they're part of the help message for the user.",
                    "Use --{option} to bypass this check");
    DECLARE_MESSAGE(AddVersionPortDoesNotExist, (msg::package_name), "", "{package_name} does not exist");
    DECLARE_MESSAGE(AddVersionPortFilesShaChanged,
                    (msg::package_name),
                    "",
                    "checked-in files for {package_name} have changed but the version was not updated");
    DECLARE_MESSAGE(AddVersionPortFilesShaUnchanged,
                    (msg::package_name, msg::version),
                    "",
                    "checked-in files for {package_name} are unchanged from version {version}");
    DECLARE_MESSAGE(AddVersionPortHasImproperFormat,
                    (msg::package_name),
                    "",
                    "{package_name} is not properly formatted");
    DECLARE_MESSAGE(AddVersionSuggestNewVersionScheme,
                    (msg::new_scheme, msg::old_scheme, msg::package_name, msg::option),
                    "The -- before {option} must be preserved as they're part of the help message for the user.",
                    "Use the version scheme \"{new_scheme}\" instead of \"{old_scheme}\" in port "
                    "\"{package_name}\".\nUse --{option} to disable this check.");
    DECLARE_MESSAGE(AddVersionUnableToParseVersionsFile, (msg::path), "", "unable to parse versions file {path}");
    DECLARE_MESSAGE(AddVersionUncommittedChanges,
                    (msg::package_name),
                    "",
                    "there are uncommitted changes for {package_name}");
    DECLARE_MESSAGE(AddVersionUpdateVersionReminder, (), "", "Did you remember to update the version or port version?");
    DECLARE_MESSAGE(AddVersionUseOptionAll,
                    (msg::command_name, msg::option),
                    "The -- before {option} must be preserved as they're part of the help message for the user.",
                    "{command_name} with no arguments requires passing --{option} to update all port versions at once");
    DECLARE_MESSAGE(AddVersionVersionAlreadyInFile,
                    (msg::version, msg::path),
                    "",
                    "version {version} is already in {path}");
    DECLARE_MESSAGE(AddVersionVersionIs, (msg::version), "", "version: {version}");
    DECLARE_MESSAGE(AllFormatArgsRawArgument,
                    (msg::value),
                    "example of {value} is 'foo {} bar'",
                    "format string \"{value}\" contains a raw format argument");
    DECLARE_MESSAGE(AllFormatArgsUnbalancedBraces,
                    (msg::value),
                    "example of {value} is 'foo bar {'",
                    "unbalanced brace in format string \"{value}\"");
    DECLARE_MESSAGE(AlreadyInstalled, (msg::spec), "", "{spec} is already installed");
    DECLARE_MESSAGE(AlreadyInstalledNotHead,
                    (msg::spec),
                    "'HEAD' means the most recent version of source code",
                    "{spec} is already installed -- not building from HEAD");
    DECLARE_MESSAGE(AnotherInstallationInProgress,
                    (),
                    "",
                    "Another installation is in progress on the machine, sleeping 6s before retrying.");
    DECLARE_MESSAGE(AppliedUserIntegration, (), "", "Applied user-wide integration for this vcpkg root.");
    DECLARE_MESSAGE(AttemptingToFetchPackagesFromVendor,
                    (msg::count, msg::vendor),
                    "",
                    "Attempting to fetch {count} package(s) from {vendor}");
    DECLARE_MESSAGE(AuthenticationMayRequireManualAction,
                    (msg::vendor),
                    "",
                    "One or more {vendor} credential providers requested manual action. Add the binary source "
                    "'interactive' to allow interactivity.");
    DECLARE_MESSAGE(AutomaticLinkingForMSBuildProjects,
                    (),
                    "",
                    "All MSBuild C++ projects can now #include any installed libraries. Linking will be handled "
                    "automatically. Installing new libraries will make them instantly available.");
    DECLARE_MESSAGE(AutoSettingEnvVar,
                    (msg::env_var, msg::url),
                    "An example of env_var is \"HTTP(S)_PROXY\""
                    "'--' at the beginning must be preserved",
                    "-- Automatically setting {env_var} environment variables to \"{url}\".");
    DECLARE_MESSAGE(BuildAlreadyInstalled,
                    (msg::spec),
                    "",
                    "{spec} is already installed; please remove {spec} before attempting to build it.");
    DECLARE_MESSAGE(BuildDependenciesMissing,
                    (),
                    "",
                    "The build command requires all dependencies to be already installed.\nThe following "
                    "dependencies are missing:");
    DECLARE_MESSAGE(BuildingFromHead,
                    (msg::spec),
                    "'HEAD' means the most recent version of source code",
                    "Building {spec} from HEAD...");
    DECLARE_MESSAGE(BuildingPackage, (msg::spec), "", "Building {spec}...");
    DECLARE_MESSAGE(BuildingPackageFailed,
                    (msg::spec, msg::build_result),
                    "",
                    "building {spec} failed with: {build_result}");
    DECLARE_MESSAGE(BuildingPackageFailedDueToMissingDeps,
                    (),
                    "Printed after BuildingPackageFailed, and followed by a list of dependencies that were missing.",
                    "due to the following missing dependencies:");
    DECLARE_MESSAGE(BuildResultBuildFailed,
                    (),
                    "Printed after the name of an installed entity to indicate that it failed to build.",
                    "BUILD_FAILED");
    DECLARE_MESSAGE(
        BuildResultCacheMissing,
        (),
        "Printed after the name of an installed entity to indicate that it was not present in the binary cache when "
        "the user has requested that things may only be installed from the cache rather than built.",
        "CACHE_MISSING");
    DECLARE_MESSAGE(BuildResultCascadeDueToMissingDependencies,
                    (),
                    "Printed after the name of an installed entity to indicate that it could not attempt "
                    "to be installed because one of its transitive dependencies failed to install.",
                    "CASCADED_DUE_TO_MISSING_DEPENDENCIES");
    DECLARE_MESSAGE(BuildResultDownloaded,
                    (),
                    "Printed after the name of an installed entity to indicate that it was successfully "
                    "downloaded but no build or install was requested.",
                    "DOWNLOADED");
    DECLARE_MESSAGE(BuildResultExcluded,
                    (),
                    "Printed after the name of an installed entity to indicate that the user explicitly "
                    "requested it not be installed.",
                    "EXCLUDED");
    DECLARE_MESSAGE(
        BuildResultFileConflicts,
        (),
        "Printed after the name of an installed entity to indicate that it conflicts with something already installed",
        "FILE_CONFLICTS");
    DECLARE_MESSAGE(BuildResultPostBuildChecksFailed,
                    (),
                    "Printed after the name of an installed entity to indicate that it built "
                    "successfully, but that it failed post build checks.",
                    "POST_BUILD_CHECKS_FAILED");
    DECLARE_MESSAGE(BuildResultRemoved,
                    (),
                    "Printed after the name of an uninstalled entity to indicate that it was successfully uninstalled.",
                    "REMOVED");
    DECLARE_MESSAGE(
        BuildResultSucceeded,
        (),
        "Printed after the name of an installed entity to indicate that it was built and installed successfully.",
        "SUCCEEDED");
    DECLARE_MESSAGE(BuildResultSummaryHeader,
                    (msg::triplet),
                    "Displayed before a list of a summary installation results.",
                    "SUMMARY FOR {triplet}");
    DECLARE_MESSAGE(BuildResultSummaryLine,
                    (msg::build_result, msg::count),
                    "Displayed to show a count of results of a build_result in a summary.",
                    "{build_result}: {count}");
    DECLARE_MESSAGE(BuildTroubleshootingMessage1,
                    (),
                    "First part of build troubleshooting message, printed before the URI to look for existing bugs.",
                    "Please ensure you're using the latest port files with `git pull` and `vcpkg "
                    "update`.\nThen check for known issues at:");
    DECLARE_MESSAGE(BuildTroubleshootingMessage2,
                    (),
                    "Second part of build troubleshooting message, printed after the URI to look for "
                    "existing bugs but before the URI to file one.",
                    "You can submit a new issue at:");
    DECLARE_MESSAGE(
        BuildTroubleshootingMessage3,
        (msg::package_name),
        "Third part of build troubleshooting message, printed after the URI to file a bug but "
        "before version information about vcpkg itself.",
        "Include '[{package_name}] Build error' in your bug report title, the following version information in your "
        "bug description, and attach any relevant failure logs from above.");
    DECLARE_MESSAGE(BuildTroubleshootingMessage4,
                    (msg::path),
                    "Fourth optional part of build troubleshooting message, printed after the version"
                    "information about vcpkg itself.",
                    "You can also use the prefilled template from {path}.");
    DECLARE_MESSAGE(ChecksFailedCheck, (), "", "vcpkg has crashed; no additional details are available.");
    DECLARE_MESSAGE(ChecksUnreachableCode, (), "", "unreachable code was reached");
    DECLARE_MESSAGE(ChecksUpdateVcpkg, (), "", "updating vcpkg by rerunning bootstrap-vcpkg may resolve this failure.");
    DECLARE_MESSAGE(CiBaselineAllowUnexpectedPassingRequiresBaseline,
                    (),
                    "",
                    "--allow-unexpected-passing can only be used if a baseline is provided via --ci-baseline.");
    DECLARE_MESSAGE(CiBaselineDisallowedCascade,
                    (msg::spec, msg::path),
                    "",
                    "REGRESSION: {spec} cascaded, but it is required to pass. ({path}).");
    DECLARE_MESSAGE(CiBaselineRegression,
                    (msg::spec, msg::build_result, msg::path),
                    "",
                    "REGRESSION: {spec} failed with {build_result}. If expected, add {spec}=fail to {path}.");
    DECLARE_MESSAGE(CiBaselineRegressionHeader,
                    (),
                    "Printed before a series of CiBaselineRegression and/or CiBaselineUnexpectedPass messages.",
                    "REGRESSIONS:");
    DECLARE_MESSAGE(CiBaselineUnexpectedPass,
                    (msg::spec, msg::path),
                    "",
                    "PASSING, REMOVE FROM FAIL LIST: {spec} ({path}).");
    DECLARE_MESSAGE(CmakeTargetsExcluded, (msg::count), "", "note: {count} additional targets are not displayed.");
    DECLARE_MESSAGE(CMakeTargetsUsage,
                    (msg::package_name),
                    "'targets' are a CMake and Makefile concept",
                    "{package_name} provides CMake targets:");
    DECLARE_MESSAGE(
        CMakeTargetsUsageHeuristicMessage,
        (),
        "Displayed after CMakeTargetsUsage; the # must be kept at the beginning so that the message remains a comment.",
        "# this is heuristically generated, and may not be correct");
    DECLARE_MESSAGE(CMakeToolChainFile,
                    (msg::path),
                    "",
                    "CMake projects should use: \"-DCMAKE_TOOLCHAIN_FILE={path}\"");
    DECLARE_MESSAGE(CommandFailed,
                    (msg::command_line),
                    "",
                    "command:\n"
                    "{command_line}\n"
                    "failed with the following results:");
    DECLARE_MESSAGE(CompressFolderFailed, (msg::path), "", "Failed to compress folder \"{path}\":");
    DECLARE_MESSAGE(CouldNotDeduceNugetIdAndVersion,
                    (msg::path),
                    "",
                    "Could not deduce nuget id and version from filename: {path}");
    DECLARE_MESSAGE(CreatedNuGetPackage, (msg::path), "", "Created nupkg: \"{path}\"");
    DECLARE_MESSAGE(CurlReportedUnexpectedResults,
                    (msg::command_line, msg::actual),
                    "{command_line} is the command line to call curl.exe, {actual} is the console output "
                    "of curl.exe locale-invariant download results.",
                    "curl has reported unexpected results to vcpkg and vcpkg cannot continue.\n"
                    "Please review the following text for sensitive information and open an issue on the "
                    "Microsoft/vcpkg GitHub to help fix this problem!\n"
                    "cmd: {command_line}\n"
                    "=== curl output ===\n"
                    "{actual}\n"
                    "=== end curl output ===");
    DECLARE_MESSAGE(DefaultBrowserLaunched, (msg::url), "", "Default browser launched to {url}.");
    DECLARE_MESSAGE(DefaultPathToBinaries,
                    (msg::path),
                    "",
                    "Based on your system settings, the default path to store binaries is \"{path}\". This consults "
                    "%LOCALAPPDATA%/%APPDATA% on Windows and $XDG_CACHE_HOME or $HOME on other platforms.");
    DECLARE_MESSAGE(DetectCompilerHash, (msg::triplet), "", "Detecting compiler hash for triplet {triplet}...");
    DECLARE_MESSAGE(DownloadAvailable,
                    (msg::env_var),
                    "",
                    "A downloadable copy of this tool is available and can be used by unsetting {env_var}.");
    DECLARE_MESSAGE(DownloadedSources, (msg::spec), "", "Downloaded sources for {spec}");
    DECLARE_MESSAGE(DownloadingVcpkgCeBundle, (msg::version), "", "Downloading vcpkg-ce bundle {version}...");
    DECLARE_MESSAGE(DownloadingVcpkgCeBundleLatest,
                    (),
                    "This message is normally displayed only in development.",
                    "Downloading latest vcpkg-ce bundle...");
    DECLARE_MESSAGE(EmailVcpkgTeam, (msg::url), "", "Send an email to {url} with any feedback.");
    DECLARE_MESSAGE(EmptyLicenseExpression, (), "", "SPDX license expression was empty.");
    DECLARE_MESSAGE(EnvStrFailedToExtract, (), "", "could not expand the environment string:");
    DECLARE_MESSAGE(ErrorDetectingCompilerInfo,
                    (msg::path),
                    "",
                    "while detecting compiler information:\nThe log file content at \"{path}\" is:");
    DECLARE_MESSAGE(ErrorIndividualPackagesUnsupported,
                    (),
                    "",
                    "In manifest mode, `vcpkg install` does not support individual package arguments.\nTo install "
                    "additional "
                    "packages, edit vcpkg.json and then run `vcpkg install` without any package arguments.");
    DECLARE_MESSAGE(ErrorInvalidClassicModeOption,
                    (msg::option),
                    "",
                    "The option --{option} is not supported in classic mode and no manifest was found.");
    DECLARE_MESSAGE(ErrorInvalidManifestModeOption,
                    (msg::option),
                    "",
                    "The option --{option} is not supported in manifest mode.");
    DECLARE_MESSAGE(
        ErrorMessageMustUsePrintError,
        (msg::value),
        "{value} is is a localized message name like ErrorMessageMustUsePrintError",
        "The message named {value} starts with error:, it must be changed to prepend ErrorMessage in code instead.");
    DECLARE_MESSAGE(
        ErrorMissingVcpkgRoot,
        (),
        "",
        "Could not detect vcpkg-root. If you are trying to use a copy of vcpkg that you've built, you must "
        "define the VCPKG_ROOT environment variable to point to a cloned copy of https://github.com/Microsoft/vcpkg.");
    DECLARE_MESSAGE(ErrorNoVSInstance,
                    (msg::triplet),
                    "",
                    "in triplet {triplet}: Unable to find a valid Visual Studio instance");
    DECLARE_MESSAGE(ErrorNoVSInstanceAt, (msg::path), "", "at \"{path}\"");
    DECLARE_MESSAGE(ErrorNoVSInstanceFullVersion, (msg::version), "", "with toolset version prefix {version}");
    DECLARE_MESSAGE(ErrorNoVSInstanceVersion, (msg::version), "", "with toolset version {version}");
    DECLARE_MESSAGE(ErrorRequireBaseline,
                    (),
                    "",
                    "this vcpkg instance requires a manifest with a specified baseline in order to "
                    "interact with ports. Please add 'builtin-baseline' to the manifest or add a "
                    "'vcpkg-configuration.json' that redefines the default registry.");
    DECLARE_MESSAGE(ErrorRequirePackagesList,
                    (),
                    "",
                    "`vcpkg install` requires a list of packages to install in classic mode.");
    DECLARE_MESSAGE(
        ErrorUnableToDetectCompilerInfo,
        (),
        "failure output will be displayed at the top of this",
        "vcpkg was unable to detect the active compiler's information. See above for the CMake failure output.");
    DECLARE_MESSAGE(ErrorVcvarsUnsupported,
                    (msg::triplet),
                    "",
                    "in triplet {triplet}: Use of Visual Studio's Developer Prompt is unsupported "
                    "on non-Windows hosts.\nDefine 'VCPKG_CMAKE_SYSTEM_NAME' or "
                    "'VCPKG_CHAINLOAD_TOOLCHAIN_FILE' in the triplet file.");
    DECLARE_MESSAGE(ErrorVsCodeNotFound,
                    (msg::env_var),
                    "",
                    "Visual Studio Code was not found and the environment variable {env_var} is not set or invalid.");
    DECLARE_MESSAGE(ErrorVsCodeNotFoundPathExamined, (), "", "The following paths were examined:");
    DECLARE_MESSAGE(ExcludedPackage, (msg::spec), "", "Excluded {spec}");
    DECLARE_MESSAGE(
        ExpectedCharacterHere,
        (msg::expected),
        "{expected} is a locale-invariant delimiter; for example, the ':' or '=' in 'zlib:x64-windows=skip'",
        "expected '{expected}' here");
    DECLARE_MESSAGE(ExpectedFailOrSkip, (), "", "expected 'fail', 'skip', or 'pass' here");
    DECLARE_MESSAGE(ExpectedPortName, (), "", "expected a port name here");
    DECLARE_MESSAGE(ExpectedTripletName, (), "", "expected a triplet name here");
    DECLARE_MESSAGE(ExtendedDocumenationAtUrl, (msg::url), "", "Extended documentation available at '{url}'.");
    DECLARE_MESSAGE(FailedToExtract, (msg::path), "", "Failed to extract \"{path}\":");
    DECLARE_MESSAGE(FailedToParseCMakeConsoleOut,
                    (),
                    "",
                    "Failed to parse CMake console output to locate block start/end markers.");
    DECLARE_MESSAGE(FailedToProvisionCe, (), "", "Failed to provision vcpkg-ce.");
    DECLARE_MESSAGE(FailedToRunToolToDetermineVersion,
                    (msg::tool_name, msg::path),
                    "Additional information, such as the command line output, if any, will be appended on "
                    "the line after this message",
                    "Failed to run \"{path}\" to determine the {tool_name} version.");
    DECLARE_MESSAGE(FailedToStoreBackToMirror, (), "", "failed to store back to mirror:");
    DECLARE_MESSAGE(FailedToStoreBinaryCache, (msg::path), "", "Failed to store binary cache {path}");
    DECLARE_MESSAGE(FailedVendorAuthentication,
                    (msg::vendor, msg::url),
                    "",
                    "One or more {vendor} credential providers failed to authenticate. See '{url}' for more details "
                    "on how to provide credentials.");
    DECLARE_MESSAGE(FeedbackAppreciated, (), "", "Thank you for your feedback!");
    DECLARE_MESSAGE(FishCompletion, (msg::path), "", "vcpkg fish completion is already added at \"{path}\".");
    DECLARE_MESSAGE(
        ForceSystemBinariesOnWeirdPlatforms,
        (),
        "",
        "Environment variable VCPKG_FORCE_SYSTEM_BINARIES must be set on arm, s390x, and ppc64le platforms.");
    DECLARE_MESSAGE(FormattedParseMessageExpression,
                    (msg::value),
                    "Example of {value} is 'x64 & windows'",
                    "on expression: {value}");
    DECLARE_MESSAGE(GenerateMsgErrorParsingFormatArgs,
                    (msg::value),
                    "example of {value} 'GenerateMsgNoComment'",
                    "parsing format string for {value}:");
    DECLARE_MESSAGE(GenerateMsgIncorrectComment,
                    (msg::value),
                    "example of {value} is 'GenerateMsgNoComment'",
                    R"(message {value} has an incorrect comment:)");
    DECLARE_MESSAGE(GenerateMsgNoArgumentValue,
                    (msg::value),
                    "example of {value} is 'arch'",
                    R"({{{value}}} was specified in a comment, but was not used in the message.)");
    DECLARE_MESSAGE(GenerateMsgNoCommentValue,
                    (msg::value),
                    "example of {value} is 'arch'",
                    R"({{{value}}} was used in the message, but not commented.)");
    DECLARE_MESSAGE(GitCommandFailed, (msg::command_line), "", "failed to execute: {command_line}");
    DECLARE_MESSAGE(GitStatusOutputExpectedFileName, (), "", "expected a file name");
    DECLARE_MESSAGE(GitStatusOutputExpectedNewLine, (), "", "expected new line");
    DECLARE_MESSAGE(GitStatusOutputExpectedRenameOrNewline, (), "", "expected renamed file or new lines");
    DECLARE_MESSAGE(GitStatusUnknownFileStatus,
                    (msg::value),
                    "{value} is a single character indicating file status, for example: A, U, M, D",
                    "unknown file status: {value}");
    DECLARE_MESSAGE(GitUnexpectedCommandOutput, (), "", "unexpected git output");
    DECLARE_MESSAGE(
        HashFileFailureToRead,
        (msg::path),
        "Printed after ErrorMessage and before the specific failing filesystem operation (like file not found)",
        "failed to read file \"{path}\" for hashing: ");
    DECLARE_MESSAGE(HeaderOnlyUsage,
                    (msg::package_name),
                    "'header' refers to C/C++ .h files",
                    "{package_name} is header-only and can be used from CMake via:");
    DECLARE_MESSAGE(IllegalFeatures, (), "", "List of features is not allowed in this context");
    DECLARE_MESSAGE(IllegalPlatformSpec, (), "", "Platform qualifier is not allowed in this context");
    DECLARE_MESSAGE(InfoSetEnvVar,
                    (msg::env_var),
                    "In this context 'editor' means IDE",
                    "You can also set the environment variable {env_var} to your editor of choice.");
    DECLARE_MESSAGE(InstallingFromLocation,
                    (msg::path),
                    "'--' at the beginning must be preserved",
                    "-- Installing port from location: {path}");
    DECLARE_MESSAGE(InstallingPackage,
                    (msg::action_index, msg::count, msg::spec),
                    "",
                    "Installing {action_index}/{count} {spec}...");
    DECLARE_MESSAGE(InstallPackageInstruction,
                    (msg::value, msg::path),
                    "'{value}' is the nuget id.",
                    "With a project open, go to Tools->NuGet Package Manager->Package Manager Console and "
                    "paste:\n Install-Package \"{value}\" -Source \"{path}\"");
    DECLARE_MESSAGE(InstallWithSystemManager,
                    (),
                    "",
                    "You may be able to install this tool via your system package manager.");
    DECLARE_MESSAGE(InstallWithSystemManagerMono,
                    (msg::url),
                    "",
                    "Ubuntu 18.04 users may need a newer version of mono, available at {url}.");
    DECLARE_MESSAGE(InstallWithSystemManagerPkg,
                    (msg::command_line),
                    "",
                    "You may be able to install this tool via your system package manager ({command_line}).");
    DECLARE_MESSAGE(IntegrationFailed, (), "", "Integration was not applied.");
    DECLARE_MESSAGE(InvalidArgument, (), "", "invalid argument");
    DECLARE_MESSAGE(
        InvalidArgumentRequiresAbsolutePath,
        (msg::binary_source),
        "",
        "invalid argument: binary config '{binary_source}' path arguments for binary config strings must be absolute");
    DECLARE_MESSAGE(
        InvalidArgumentRequiresBaseUrl,
        (msg::base_url, msg::binary_source),
        "",
        "invalid argument: binary config '{binary_source}' requires a {base_url} base url as the first argument");
    DECLARE_MESSAGE(InvalidArgumentRequiresBaseUrlAndToken,
                    (msg::binary_source),
                    "",
                    "invalid argument: binary config '{binary_source}' requires at least a base-url and a SAS token");
    DECLARE_MESSAGE(InvalidArgumentRequiresNoneArguments,
                    (msg::binary_source),
                    "",
                    "invalid argument: binary config '{binary_source}' does not take arguments");
    DECLARE_MESSAGE(InvalidArgumentRequiresOneOrTwoArguments,
                    (msg::binary_source),
                    "",
                    "invalid argument: binary config '{binary_source}' requires 1 or 2 arguments");
    DECLARE_MESSAGE(InvalidArgumentRequiresPathArgument,
                    (msg::binary_source),
                    "",
                    "invalid argument: binary config '{binary_source}' requires at least one path argument");
    DECLARE_MESSAGE(InvalidArgumentRequiresPrefix,
                    (msg::binary_source),
                    "",
                    "invalid argument: binary config '{binary_source}' requires at least one prefix");
    DECLARE_MESSAGE(InvalidArgumentRequiresSingleArgument,
                    (msg::binary_source),
                    "",
                    "invalid argument: binary config '{binary_source}' does not take more than 1 argument");
    DECLARE_MESSAGE(InvalidArgumentRequiresSingleStringArgument,
                    (msg::binary_source),
                    "",
                    "invalid argument: binary config '{binary_source}' expects a single string argument");
    DECLARE_MESSAGE(InvalidArgumentRequiresSourceArgument,
                    (msg::binary_source),
                    "",
                    "invalid argument: binary config '{binary_source}' requires at least one source argument");
    DECLARE_MESSAGE(InvalidArgumentRequiresTwoOrThreeArguments,
                    (msg::binary_source),
                    "",
                    "invalid argument: binary config '{binary_source}' requires 2 or 3 arguments");
    DECLARE_MESSAGE(InvalidArgumentRequiresValidToken,
                    (msg::binary_source),
                    "",
                    "invalid argument: binary config '{binary_source}' requires a SAS token without a "
                    "preceeding '?' as the second argument");
    DECLARE_MESSAGE(InvalidFormatString,
                    (msg::actual),
                    "{actual} is the provided format string",
                    "invalid format string: {actual}");
    DECLARE_MESSAGE(FailedToParseJson, (msg::path), "", "Failed to parse JSON file {path}");
    DECLARE_MESSAGE(FailedToRead, (msg::path, msg::error_msg), "", "Failed to read {path}: {error_msg}");
    DECLARE_MESSAGE(JsonErrorMustBeAnObject, (msg::path), "", "Expected \"{path}\" to be an object.");
    DECLARE_MESSAGE(LaunchingProgramFailed,
                    (msg::tool_name),
                    "A platform API call failure message is appended after this",
                    "Launching {tool_name}:");
    DECLARE_MESSAGE(LicenseExpressionContainsExtraPlus,
                    (),
                    "",
                    "SPDX license expression contains an extra '+'. These are only allowed directly "
                    "after a license identifier.");
    DECLARE_MESSAGE(LicenseExpressionContainsInvalidCharacter,
                    (msg::value),
                    "example of {value:02X} is '7B'\nexample of {value} is '{'",
                    "SPDX license expression contains an invalid character (0x{value:02X} '{value}').");
    DECLARE_MESSAGE(LicenseExpressionContainsUnicode,
                    (msg::value, msg::pretty_value),
                    "example of {value:04X} is '22BB'\nexample of {pretty_value} is '⊻'",
                    "SPDX license expression contains a unicode character (U+{value:04X} "
                    "'{pretty_value}'), but these expressions are ASCII-only.");
    DECLARE_MESSAGE(LicenseExpressionDocumentRefUnsupported,
                    (),
                    "",
                    "The current implementation does not support DocumentRef- SPDX references.");
    DECLARE_MESSAGE(LicenseExpressionExpectCompoundFoundParen,
                    (),
                    "",
                    "Expected a compound or the end of the string, found a parenthesis.");
    DECLARE_MESSAGE(LicenseExpressionExpectCompoundFoundWith,
                    (),
                    "AND, OR, and WITH are all keywords and should not be translated.",
                    "Expected either AND or OR, found WITH (WITH is only allowed after license names, not "
                    "parenthesized expressions).");
    DECLARE_MESSAGE(LicenseExpressionExpectCompoundFoundWord,
                    (msg::value),
                    "Example of {value} is 'MIT'.\nAND and OR are both keywords and should not be translated.",
                    "Expected either AND or OR, found a license or exception name: '{value}'.");
    DECLARE_MESSAGE(LicenseExpressionExpectCompoundOrWithFoundWord,
                    (msg::value),
                    "example of {value} is 'MIT'.\nAND, OR, and WITH are all keywords and should not be translated.",
                    "Expected either AND, OR, or WITH, found a license or exception name: '{value}'.");
    DECLARE_MESSAGE(LicenseExpressionExpectExceptionFoundCompound,
                    (msg::value),
                    "Example of {value} is 'AND'",
                    "Expected an exception name, found the compound {value}.");
    DECLARE_MESSAGE(LicenseExpressionExpectExceptionFoundEof,
                    (),
                    "",
                    "Expected an exception name, found the end of the string.");
    DECLARE_MESSAGE(LicenseExpressionExpectExceptionFoundParen,
                    (),
                    "",
                    "Expected an exception name, found a parenthesis.");
    DECLARE_MESSAGE(LicenseExpressionExpectLicenseFoundCompound,
                    (msg::value),
                    "Example of {value} is 'AND'",
                    "Expected a license name, found the compound {value}.");
    DECLARE_MESSAGE(LicenseExpressionExpectLicenseFoundEof,
                    (),
                    "",
                    "Expected a license name, found the end of the string.");
    DECLARE_MESSAGE(LicenseExpressionExpectLicenseFoundParen, (), "", "Expected a license name, found a parenthesis.");
    DECLARE_MESSAGE(LicenseExpressionImbalancedParens,
                    (),
                    "",
                    "There was a close parenthesis without an opening parenthesis.");
    DECLARE_MESSAGE(LicenseExpressionUnknownException,
                    (msg::value),
                    "Example of {value} is 'unknownexception'",
                    "Unknown license exception identifier '{value}'. Known values are listed at "
                    "https://spdx.org/licenses/exceptions-index.html");
    DECLARE_MESSAGE(LicenseExpressionUnknownLicense,
                    (msg::value),
                    "Example of {value} is 'unknownlicense'",
                    "Unknown license identifier '{value}'. Known values are listed at https://spdx.org/licenses/");
    DECLARE_MESSAGE(LoadingCommunityTriplet,
                    (msg::path),
                    "'-- [COMMUNITY]' at the beginning must be preserved",
                    "-- [COMMUNITY] Loading triplet configuration from: {path}");
    DECLARE_MESSAGE(LoadingDependencyInformation,
                    (msg::count),
                    "",
                    "Loading dependency information for {count} packages...");
    DECLARE_MESSAGE(LoadingOverlayTriplet,
                    (msg::path),
                    "'-- [OVERLAY]' at the beginning must be preserved",
                    "-- [OVERLAY] Loading triplet configuration from: {path}");
    DECLARE_MESSAGE(LocalizedMessageMustNotContainIndents,
                    (msg::value),
                    "{value} is is a localized message name like LocalizedMessageMustNotContainIndents. "
                    "The 'LocalizedString::append_indent' part is locale-invariant.",
                    "The message named {value} contains what appears to be indenting which must be "
                    "changed to use LocalizedString::append_indent instead.");
    DECLARE_MESSAGE(LocalizedMessageMustNotEndWithNewline,
                    (msg::value),
                    "{value} is a localized message name like LocalizedMessageMustNotEndWithNewline",
                    "The message named {value} ends with a newline which should be added by formatting "
                    "rather than by localization.");
    DECLARE_MESSAGE(Missing7zHeader, (), "", "Unable to find 7z header.");
    DECLARE_MESSAGE(MissingExtension, (msg::extension), "", "Missing '{extension}' extension.");
    DECLARE_MESSAGE(MonoInstructions,
                    (),
                    "",
                    "This may be caused by an incomplete mono installation. Full mono is "
                    "available on some systems via `sudo apt install mono-complete`. Ubuntu 18.04 users may "
                    "need a newer version of mono, available at https://www.mono-project.com/download/stable/");
    DECLARE_MESSAGE(MsiexecFailedToExtract,
                    (msg::path, msg::exit_code),
                    "",
                    "msiexec failed while extracting \"{path}\" with launch or exit code {exit_code} and message:");
    DECLARE_MESSAGE(NavigateToNPS, (msg::url), "", "Please navigate to {url} in your preferred browser.");
    DECLARE_MESSAGE(NewConfigurationAlreadyExists,
                    (msg::path),
                    "",
                    "Creating a manifest would overwrite a vcpkg-configuration.json at {path}.");
    DECLARE_MESSAGE(NewManifestAlreadyExists, (msg::path), "", "A manifest is already present at {path}.");
    DECLARE_MESSAGE(NewNameCannotBeEmpty, (), "", "--name cannot be empty.");
    DECLARE_MESSAGE(NewOnlyOneVersionKind,
                    (),
                    "",
                    "Only one of --version-relaxed, --version-date, or --version-string may be specified.");
    DECLARE_MESSAGE(NewSpecifyNameVersionOrApplication,
                    (),
                    "",
                    "Either specify --name and --version to produce a manifest intended for C++ libraries, or specify "
                    "--application to indicate that the manifest is not intended to be used as a port.");
    DECLARE_MESSAGE(NewVersionCannotBeEmpty, (), "", "--version cannot be empty.");
    DECLARE_MESSAGE(NoCachedPackages, (), "", "No packages are cached.");
    DECLARE_MESSAGE(NoLocalizationForMessages, (), "", "No localized messages for the following: ");
    DECLARE_MESSAGE(NoRegistryForPort, (msg::package_name), "", "no registry configured for port {package_name}");
    DECLARE_MESSAGE(NugetPackageFileSucceededButCreationFailed,
                    (msg::path),
                    "",
                    "NuGet package creation succeeded, but no .nupkg was produced. Expected: \"{path}\"");
    DECLARE_MESSAGE(PackageFailedtWhileExtracting,
                    (msg::value, msg::path),
                    "'{value}' is either a tool name or a package name.",
                    "'{value}' failed while extracting {path}.");
    DECLARE_MESSAGE(PackingVendorFailed,
                    (msg::vendor),
                    "",
                    "Packing {vendor} failed. Use --debug for more information.");
    DECLARE_MESSAGE(ParseControlErrorInfoInvalidFields, (), "", "The following fields were not expected:");
    DECLARE_MESSAGE(ParseControlErrorInfoMissingFields, (), "", "The following fields were missing:");
    DECLARE_MESSAGE(ParseControlErrorInfoTypesEntry,
                    (msg::value, msg::expected),
                    "{value} is the name of a field in an on-disk file, {expected} is a short description "
                    "of what it should be like 'a non-negative integer' (which isn't localized yet)",
                    "{value} was expected to be {expected}");
    DECLARE_MESSAGE(ParseControlErrorInfoWhileLoading,
                    (msg::path),
                    "Error messages are is printed after this.",
                    "while loading {path}:");
    DECLARE_MESSAGE(ParseControlErrorInfoWrongTypeFields, (), "", "The following fields had the wrong types:");
    DECLARE_MESSAGE(PortDependencyConflict,
                    (msg::package_name),
                    "",
                    "Port {package_name} has the following unsupported dependencies:");
    DECLARE_MESSAGE(PortNotInBaseline,
                    (msg::package_name),
                    "",
                    "the baseline does not contain an entry for port {package_name}");
    DECLARE_MESSAGE(PortSupportsField,
                    (msg::value),
                    "'{value}' is the value of the 'supports' field in the port's vcpkg.json.",
                    "(supports: \"{value}\")");
    DECLARE_MESSAGE(PreviousIntegrationFileRemains, (), "", "Previous integration file was not removed.");
    DECLARE_MESSAGE(ProcessorArchitectureMalformed,
                    (msg::arch),
                    "",
                    "Failed to parse %PROCESSOR_ARCHITECTURE% ({arch}) as a valid CPU architecture.");
    DECLARE_MESSAGE(ProcessorArchitectureMissing,
                    (),
                    "",
                    "The required environment variable %PROCESSOR_ARCHITECTURE% is missing.");
    DECLARE_MESSAGE(ProcessorArchitectureW6432Malformed,
                    (msg::arch),
                    "",
                    "Failed to parse %PROCESSOR_ARCHITEW6432% ({arch}) as a valid CPU architecture. "
                    "Falling back to %PROCESSOR_ARCHITECTURE%.");
    DECLARE_MESSAGE(ProgramReturnedNonzeroExitCode,
                    (msg::tool_name, msg::exit_code),
                    "The program's console output is appended after this.",
                    "{tool_name} failed with exit code: ({exit_code}).");
    DECLARE_MESSAGE(PushingVendorFailed,
                    (msg::vendor, msg::path),
                    "",
                    "Pushing {vendor} to \"{path}\" failed. Use --debug for more information.");
    DECLARE_MESSAGE(ReplaceSecretsError,
                    (msg::error_msg),
                    "",
                    "Replace secretes produced the following error: '{error_msg}'");
    DECLARE_MESSAGE(RestoredPackage, (msg::path), "", "Restored package from \"{path}\"");
    DECLARE_MESSAGE(
        RestoredPackagesFromVendor,
        (msg::count, msg::elapsed, msg::value),
        "{value} may be either a 'vendor' like 'Azure' or 'NuGet', or a file path like C:\\example or /usr/example",
        "Restored {count} package(s) from {value} in {elapsed}. Use --debug to see more details.");
    DECLARE_MESSAGE(ResultsHeader, (), "Displayed before a list of installation results.", "RESULTS");
    DECLARE_MESSAGE(SettingEnvVar,
                    (msg::env_var, msg::url),
                    "An example of env_var is \"HTTP(S)_PROXY\""
                    "'--' at the beginning must be preserved",
                    "-- Setting \"{env_var}\" environment variables to \"{url}\".");
    DECLARE_MESSAGE(SourceFieldPortNameMismatch,
                    (msg::package_name, msg::path),
                    "{package_name} and \"{path}\" are both names of installable ports/packages. 'Source', "
                    "'CONTROL', 'vcpkg.json', and 'name' references are locale-invariant.",
                    "The 'Source' field inside the CONTROL file, or \"name\" field inside the vcpkg.json "
                    "file has the name {package_name} and does not match the port directory \"{path}\".");
    DECLARE_MESSAGE(StoredBinaryCache, (msg::path), "", "Stored binary cache: \"{path}\"");
    DECLARE_MESSAGE(SuggestStartingBashShell,
                    (),
                    "",
                    "Please make sure you have started a new bash shell for the change to take effect.");
    DECLARE_MESSAGE(SupportedPort, (msg::package_name), "", "Port {package_name} is supported.");
    DECLARE_MESSAGE(SystemApiErrorMessage,
                    (msg::system_api, msg::exit_code, msg::error_msg),
                    "",
                    "calling {system_api} failed with {exit_code} ({error_msg})");
    DECLARE_MESSAGE(ToolFetchFailed, (msg::tool_name), "", "Could not fetch {tool_name}.");
    DECLARE_MESSAGE(ToolInWin10, (), "", "This utility is bundled with Windows 10 or later.");
    DECLARE_MESSAGE(UnexpectedErrorDuringBulkDownload, (), "", "an unexpected error occurred during bulk download.");
    DECLARE_MESSAGE(UnexpectedExtension, (msg::extension), "", "Unexpected archive extension: '{extension}'.");
    DECLARE_MESSAGE(UnexpectedFormat,
                    (msg::expected, msg::actual),
                    "{expected} is the expected format, {actual} is the actual format.",
                    "Expected format is [{expected}], but was [{actual}].");
    DECLARE_MESSAGE(UnexpectedToolOutput,
                    (msg::tool_name, msg::path),
                    "The actual command line output will be appended after this message.",
                    "{tool_name} ({path}) produced unexpected output when attempting to determine the version:");
    DECLARE_MESSAGE(UnknownBaselineFileContent,
                    (),
                    "",
                    "unrecognizable baseline entry; expected 'port:triplet=(fail|skip|pass)'");
    DECLARE_MESSAGE(UnknownBinaryProviderType,
                    (),
                    "",
                    "unknown binary provider type: valid providers are 'clear', 'default', 'nuget', "
                    "'nugetconfig','nugettimeout', 'interactive', 'x-azblob', 'x-gcs', 'x-aws', "
                    "'x-aws-config', 'http', and 'files'");
    DECLARE_MESSAGE(UnknownParameterForIntegrate,
                    (msg::value),
                    "'{value}' is a user-supplied command line option. For example, given vcpkg integrate frobinate, "
                    "{value} would be frobinate.",
                    "Unknown parameter '{value}' for integrate.");
    DECLARE_MESSAGE(UnknownTool, (), "", "vcpkg does not have a definition of this tool for this platform.");
    DECLARE_MESSAGE(
        UnknownVariablesInTemplate,
        (msg::value, msg::list),
        "{value} is the value provided by the user and {list} a list of unknown variables seperated by comma",
        "invalid argument: url template '{value}' contains unknown variables: {list}");
    DECLARE_MESSAGE(UnsupportedPort, (msg::package_name), "", "Port {package_name} is not supported.");
    DECLARE_MESSAGE(UnsupportedPortDependency,
                    (msg::value),
                    "'{value}' is the name of a port dependency.",
                    "- dependency {value} is not supported.");
    DECLARE_MESSAGE(UnsupportedSystemName,
                    (msg::system_name),
                    "",
                    "Could not map VCPKG_CMAKE_SYSTEM_NAME '{system_name}' to a vcvarsall platform. "
                    "Supported system names are '', 'Windows' and 'WindowsStore'.");
    DECLARE_MESSAGE(UnsupportedToolchain,
                    (msg::triplet, msg::arch, msg::path, msg::list),
                    "example for {list} is 'x86, arm64'",
                    "in triplet {triplet}: Unable to find a valid toolchain for requested target architecture {arch}.\n"
                    "The selected Visual Studio instance is at: {path}\n"
                    "The available toolchain combinations are: {list}");
    DECLARE_MESSAGE(
        UpdateBaselineAddBaselineNoManifest,
        (msg::option),
        "",
        "the --{option} switch was passed, but there is no manifest file to add a `builtin-baseline` field to.");
    DECLARE_MESSAGE(UpdateBaselineLocalGitError,
                    (msg::path),
                    "",
                    "git failed to parse HEAD for the local vcpkg registry at \"{path}\"");
    DECLARE_MESSAGE(UpdateBaselineNoConfiguration,
                    (),
                    "",
                    "neither `vcpkg.json` nor `vcpkg-configuration.json` exist to update.");
    DECLARE_MESSAGE(UpdateBaselineNoExistingBuiltinBaseline,
                    (msg::option),
                    "",
                    "the manifest file currently does not contain a `builtin-baseline` field; in order to "
                    "add one, pass the --{option} switch.");
    DECLARE_MESSAGE(UpdateBaselineNoUpdate,
                    (msg::url, msg::value),
                    "example of {value} is '5507daa796359fe8d45418e694328e878ac2b82f'",
                    "registry '{url}' not updated: '{value}'");
    DECLARE_MESSAGE(UpdateBaselineRemoteGitError, (msg::url), "", "git failed to fetch remote repository '{url}'");
    DECLARE_MESSAGE(UpdateBaselineUpdatedBaseline,
                    (msg::url, msg::old_value, msg::new_value),
                    "example of {old_value}, {new_value} is '5507daa796359fe8d45418e694328e878ac2b82f'",
                    "updated registry '{url}': baseline '{old_value}' -> '{new_value}'");
    DECLARE_MESSAGE(UploadedBinaries, (msg::count, msg::vendor), "", "Uploaded binaries to {count} {vendor}.");
    DECLARE_MESSAGE(UploadedPackagesToVendor,
                    (msg::count, msg::elapsed, msg::vendor),
                    "",
                    "Uploaded {count} package(s) to {vendor} in {elapsed}");
    DECLARE_MESSAGE(UploadingBinariesToVendor,
                    (msg::spec, msg::vendor, msg::path),
                    "",
                    "Uploading binaries for '{spec}' to '{vendor}' source \"{path}\".");
    DECLARE_MESSAGE(UploadingBinariesUsingVendor,
                    (msg::spec, msg::vendor, msg::path),
                    "",
                    "Uploading binaries for '{spec}' using '{vendor}' \"{path}\".");
    DECLARE_MESSAGE(UseEnvVar,
                    (msg::env_var),
                    "An example of env_var is \"HTTP(S)_PROXY\""
                    "'--' at the beginning must be preserved",
                    "-- Using {env_var} in environment variables.");
    DECLARE_MESSAGE(UserWideIntegrationDeleted, (), "", "User-wide integration is not installed.");
    DECLARE_MESSAGE(UserWideIntegrationRemoved, (), "", "User-wide integration was removed.");
    DECLARE_MESSAGE(UsingCommunityTriplet,
                    (msg::triplet),
                    "'--' at the beginning must be preserved",
                    "-- Using community triplet {triplet}. This triplet configuration is not guaranteed to succeed.");
    DECLARE_MESSAGE(UsingManifestAt, (msg::path), "", "Using manifest file at {path}.");
    DECLARE_MESSAGE(VcpkgCeIsExperimental,
                    (),
                    "",
                    "vcpkg-ce ('configure environment') is experimental and may change at any time.");
    DECLARE_MESSAGE(
        VcpkgCompletion,
        (msg::value, msg::path),
        "'{value}' is the subject for completion. i.e. bash, zsh, etc.",
        "vcpkg {value} completion is already imported to your \"{path}\" file.\nThe following entries were found:");
    DECLARE_MESSAGE(VcpkgDisallowedClassicMode,
                    (),
                    "",
                    "Could not locate a manifest (vcpkg.json) above the current working "
                    "directory.\nThis vcpkg distribution does not have a classic mode instance.");
    DECLARE_MESSAGE(
        VcpkgHasCrashed,
        (),
        "Printed at the start of a crash report.",
        "vcpkg has crashed. Please create an issue at https://github.com/microsoft/vcpkg containing a brief summary of "
        "what you were trying to do and the following information.");
    DECLARE_MESSAGE(VcpkgInvalidCommand, (msg::command_name), "", "invalid command: {command_name}");
    DECLARE_MESSAGE(VcpkgSendMetricsButDisabled, (), "", "passed --sendmetrics, but metrics are disabled.");
    DECLARE_MESSAGE(VersionCommandHeader,
                    (msg::version),
                    "",
                    "vcpkg package management program version {version}\n\nSee LICENSE.txt for license information.");
    DECLARE_MESSAGE(VersionConstraintViolated,
                    (msg::spec, msg::expected_version, msg::actual_version),
                    "",
                    "dependency {spec} was expected to be at least version "
                    "{expected_version}, but is currently {actual_version}.");
    DECLARE_MESSAGE(
        VersionInvalidDate,
        (msg::version),
        "",
        "`{version}` is not a valid date version. Dates must follow the format YYYY-MM-DD and disambiguators must be "
        "dot-separated positive integer values without leading zeroes.");
    DECLARE_MESSAGE(VersionInvalidRelaxed,
                    (msg::version),
                    "",
                    "`{version}` is not a valid relaxed version (semver with arbitrary numeric element count).");
    DECLARE_MESSAGE(VersionInvalidSemver,
                    (msg::version),
                    "",
                    "`{version}` is not a valid semantic version, consult <https://semver.org>.");
    DECLARE_MESSAGE(VersionSpecMismatch,
                    (msg::path, msg::expected_version, msg::actual_version),
                    "",
                    "Failed to load port because versions are inconsistent. The file \"{path}\" contains the version "
                    "{actual_version}, but the version database indicates that it should be {expected_version}.");
    DECLARE_MESSAGE(VSExaminedInstances, (), "", "The following Visual Studio instances were considered:");
    DECLARE_MESSAGE(VSExaminedPaths, (), "", "The following paths were examined for Visual Studio instances:");
    DECLARE_MESSAGE(VSNoInstances, (), "", "Could not locate a complete Visual Studio instance");
    DECLARE_MESSAGE(WaitingForChildrenToExit, (), "", "Waiting for child processes to exit...");
    DECLARE_MESSAGE(WaitingToTakeFilesystemLock, (msg::path), "", "waiting to take filesystem lock on {path}...");
    DECLARE_MESSAGE(WarningMessageMustUsePrintWarning,
                    (msg::value),
                    "{value} is is a localized message name like WarningMessageMustUsePrintWarning",
                    "The message named {value} starts with warning:, it must be changed to prepend "
                    "WarningMessage in code instead.");
    DECLARE_MESSAGE(WarningsTreatedAsErrors, (), "", "previous warnings being interpreted as errors");
    DECLARE_MESSAGE(FailedToParseManifest, (msg::path), "", "Failed to parse manifest file: {path}");
    DECLARE_MESSAGE(FailedToReadParagraph, (msg::path), "", "Failed to read paragraphs from {path}");
    DECLARE_MESSAGE(FailedToParseControl, (msg::path), "", "Failed to parse control file: {path}");
    DECLARE_MESSAGE(FailedToWriteManifest, (msg::path), "", "Failed to write manifest file {path}");
    DECLARE_MESSAGE(FailedToRemoveControl, (msg::path), "", "Failed to remove control file {path}");
    DECLARE_MESSAGE(FailedToFormatMissingFile,
                    (),
                    "",
                    "No files to format.\nPlease pass either --all, or the explicit files to format or convert.");
    DECLARE_MESSAGE(MissingArgFormatManifest,
                    (),
                    "",
                    "format-manifest was passed --convert-control without '--all'.\nThis doesn't do anything: we will "
                    "automatically convert all control files passed explicitly.");
    DECLARE_MESSAGE(ControlAndManifestFilesPresent,
                    (msg::path),
                    "",
                    "Both a manifest file and a CONTROL file exist in port directory: {path}");
    DECLARE_MESSAGE(ManifestFormatCompleted, (), "", "Succeeded in formatting the manifest files.");
    DECLARE_MESSAGE(SuggestGitPull, (), "", "The result may be outdated. Run `git pull` to get the latest results.");
    DECLARE_MESSAGE(MissingPortSuggestPullRequest,
                    (),
                    "",
                    "If your port is not listed, please open an issue at and/or consider making a pull request.");
    DECLARE_MESSAGE(ArtifactsOptionIncompatibility, (msg::option), "", "--{option} has no effect on find artifact.");
    DECLARE_MESSAGE(AddCommandFirstArg, (), "", "The first parameter to add must be 'artifact' or 'port'.");
}