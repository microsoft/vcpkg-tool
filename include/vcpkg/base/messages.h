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

    DECLARE_MESSAGE(AttemptingToFetchPackagesFromVendor,
                    (msg::count, msg::vendor),
                    "",
                    "Attempting to fetch {count} package(s) from {vendor}");
    DECLARE_MESSAGE(MsiexecFailedToExtract,
                    (msg::path, msg::exit_code),
                    "",
                    "msiexec failed while extracting '{path}' with launch or exit code {exit_code} and message:");
    DECLARE_MESSAGE(CouldNotDeduceNugetIdAndVersion,
                    (msg::path),
                    "",
                    "Could not deduce nuget id and version from filename: {path}");
    DECLARE_MESSAGE(BuildResultSummaryHeader,
                    (msg::triplet),
                    "Displayed before a list of a summary installation results.",
                    "SUMMARY FOR {triplet}");
    DECLARE_MESSAGE(BuildResultSummaryLine,
                    (msg::build_result, msg::count),
                    "Displayed to show a count of results of a build_result in a summary.",
                    "{build_result}: {count}");

    DECLARE_MESSAGE(
        BuildResultSucceeded,
        (),
        "Printed after the name of an installed entity to indicate that it was built and installed successfully.",
        "SUCCEEDED");

    DECLARE_MESSAGE(BuildResultBuildFailed,
                    (),
                    "Printed after the name of an installed entity to indicate that it failed to build.",
                    "BUILD_FAILED");

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

    DECLARE_MESSAGE(BuildResultCascadeDueToMissingDependencies,
                    (),
                    "Printed after the name of an installed entity to indicate that it could not attempt "
                    "to be installed because one of its transitive dependencies failed to install.",
                    "CASCADED_DUE_TO_MISSING_DEPENDENCIES");

    DECLARE_MESSAGE(BuildResultExcluded,
                    (),
                    "Printed after the name of an installed entity to indicate that the user explicitly "
                    "requested it not be installed.",
                    "EXCLUDED");

    DECLARE_MESSAGE(
        BuildResultCacheMissing,
        (),
        "Printed after the name of an installed entity to indicate that it was not present in the binary cache when "
        "the user has requested that things may only be installed from the cache rather than built.",
        "CACHE_MISSING");

    DECLARE_MESSAGE(BuildResultDownloaded,
                    (),
                    "Printed after the name of an installed entity to indicate that it was successfully "
                    "downloaded but no build or install was requested.",
                    "DOWNLOADED");

    DECLARE_MESSAGE(BuildResultRemoved,
                    (),
                    "Printed after the name of an uninstalled entity to indicate that it was successfully uninstalled.",
                    "REMOVED");

    DECLARE_MESSAGE(BuildingPackageFailed,
                    (msg::spec, msg::build_result),
                    "",
                    "building {spec} failed with: {build_result}");
    DECLARE_MESSAGE(BuildingPackageFailedDueToMissingDeps,
                    (),
                    "Printed after BuildingPackageFailed, and followed by a list of dependencies that were missing.",
                    "due to the following missing dependencies:");

    DECLARE_MESSAGE(BuildAlreadyInstalled,
                    (msg::spec),
                    "",
                    "{spec} is already installed; please remove {spec} before attempting to build it.");

    DECLARE_MESSAGE(SourceFieldPortNameMismatch,
                    (msg::package_name, msg::path),
                    "{package_name} and {path} are both names of installable ports/packages. 'Source', "
                    "'CONTROL', 'vcpkg.json', and 'name' references are locale-invariant.",
                    "The 'Source' field inside the CONTROL file, or \"name\" field inside the vcpkg.json "
                    "file has the name {package_name} and does not match the port directory {path}.");

    DECLARE_MESSAGE(BuildDependenciesMissing,
                    (),
                    "",
                    "The build command requires all dependencies to be already installed.\nThe following "
                    "dependencies are missing:");

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
    DECLARE_MESSAGE(DetectCompilerHash, (msg::triplet), "", "Detecting compiler hash for triplet \"{triplet}\"...");
    DECLARE_MESSAGE(UseEnvVar,
                    (msg::env_var),
                    "An example of env_var is \"HTTP(S)_PROXY\""
                    "'--' at the beginning must be preserved",
                    "-- Using {env_var} in environment variables.");
    DECLARE_MESSAGE(SettingEnvVar,
                    (msg::env_var, msg::url),
                    "An example of env_var is \"HTTP(S)_PROXY\""
                    "'--' at the beginning must be preserved",
                    "-- Setting \"{env_var}\" environment variables to \"{url}\".");
    DECLARE_MESSAGE(AutoSettingEnvVar,
                    (msg::env_var, msg::url),
                    "An example of env_var is \"HTTP(S)_PROXY\""
                    "'--' at the beginning must be preserved",
                    "-- Automatically setting {env_var} environment variables to \"{url}\".");
    DECLARE_MESSAGE(ErrorDetectingCompilerInfo,
                    (msg::path),
                    "",
                    "while detecting compiler information:\nThe log file content at \"{path}\" is:");
    DECLARE_MESSAGE(
        ErrorUnableToDetectCompilerInfo,
        (),
        "failure output will be displayed at the top of this",
        "vcpkg was unable to detect the active compiler's information. See above for the CMake failure output.");
    DECLARE_MESSAGE(UsingCommunityTriplet,
                    (msg::triplet),
                    "'--' at the beginning must be preserved",
                    "-- Using community triplet {triplet}. This triplet configuration is not guaranteed to succeed.");
    DECLARE_MESSAGE(LoadingCommunityTriplet,
                    (msg::path),
                    "'-- [COMMUNITY]' at the beginning must be preserved",
                    "-- [COMMUNITY] Loading triplet configuration from: {path}");
    DECLARE_MESSAGE(LoadingOverlayTriplet,
                    (msg::path),
                    "'-- [OVERLAY]' at the beginning must be preserved",
                    "-- [OVERLAY] Loading triplet configuration from: {path}");
    DECLARE_MESSAGE(InstallingFromLocation,
                    (msg::path),
                    "'--' at the beginning must be preserved",
                    "-- Installing port from location: {path}");
    DECLARE_MESSAGE(UnsupportedToolchain,
                    (msg::triplet, msg::arch, msg::path, msg::list),
                    "example for {list} is 'x86, arm64'",
                    "in triplet {triplet}: Unable to find a valid toolchain for requested target architecture {arch}.\n"
                    "The selected Visual Studio instance is at: {path}\n"
                    "The available toolchain combinations are: {list}");

    DECLARE_MESSAGE(UnsupportedSystemName,
                    (msg::system_name),
                    "",
                    "Could not map VCPKG_CMAKE_SYSTEM_NAME '{system_name}' to a vcvarsall platform. "
                    "Supported system names are '', 'Windows' and 'WindowsStore'.");
    DECLARE_MESSAGE(ExpectedPortName, (), "", "expected a port name here");
    DECLARE_MESSAGE(ExpectedTripletName, (), "", "expected a triplet name here");
    DECLARE_MESSAGE(ExpectedFailOrSkip, (), "", "expected 'fail', 'skip', or 'pass' here");
    DECLARE_MESSAGE(UnknownBaselineFileContent,
                    (),
                    "",
                    "unrecognizable baseline entry; expected 'port:triplet=(fail|skip|pass)'");

    DECLARE_MESSAGE(CiBaselineRegression,
                    (msg::spec, msg::build_result, msg::path),
                    "",
                    "REGRESSION: {spec} failed with {build_result}. If expected, add {spec}=fail to {path}.");

    DECLARE_MESSAGE(CiBaselineUnexpectedPass,
                    (msg::spec, msg::path),
                    "",
                    "PASSING, REMOVE FROM FAIL LIST: {spec} ({path}).");

    DECLARE_MESSAGE(CiBaselineDisallowedCascade,
                    (msg::spec, msg::path),
                    "",
                    "REGRESSION: {spec} cascaded, but it is required to pass. ({path}).");
    DECLARE_MESSAGE(AddTripletExpressionNotAllowed,
                    (msg::package_name, msg::triplet),
                    "",
                    "triplet expressions are not allowed here. You may want to change "
                    "`{package_name}:{triplet}` to `{package_name}` instead.");
    DECLARE_MESSAGE(AddFirstArgument,
                    (msg::command_line),
                    "",
                    "The first argument to '{command_line}' must be 'artifact' or 'port'.");

    DECLARE_MESSAGE(AddPortSucceded, (), "", "Succeeded in adding ports to vcpkg.json file.");
    DECLARE_MESSAGE(AddPortRequiresManifest,
                    (msg::command_line),
                    "",
                    "'{command_line}' requires an active manifest file.");

    DECLARE_MESSAGE(AddArtifactOnlyOne,
                    (msg::command_line),
                    "",
                    "'{command_line}' can only add one artifact at a time.");
    DECLARE_MESSAGE(AddVersionSuggestNewVersionScheme,
                    (msg::new_scheme, msg::old_scheme, msg::package_name, msg::option),
                    "The -- before {option} must be preserved as they're part of the help message for the user.",
                    "Use the version scheme \"{new_scheme}\" instead of \"{old_scheme}\" in port "
                    "\"{package_name}\".\nUse --{option} to disable this check.");
    DECLARE_MESSAGE(AddVersionVersionAlreadyInFile,
                    (msg::version, msg::path),
                    "",
                    "version {version} is already in {path}");
    DECLARE_MESSAGE(AddVersionAddedVersionToFile, (msg::version, msg::path), "", "added version {version} to {path}");
    DECLARE_MESSAGE(AddVersionNewFile, (), "", "(new file)");
    DECLARE_MESSAGE(AddVersionUncommittedChanges,
                    (msg::package_name),
                    "",
                    "there are uncommitted changes for {package_name}");
    DECLARE_MESSAGE(AddVersionPortFilesShaUnchanged,
                    (msg::package_name, msg::version),
                    "",
                    "checked-in files for {package_name} are unchanged from version {version}");
    DECLARE_MESSAGE(AddVersionCommitChangesReminder, (), "", "Did you remember to commit your changes?");
    DECLARE_MESSAGE(AddVersionNoFilesUpdated, (), "", "No files were updated");
    DECLARE_MESSAGE(AddVersionNoFilesUpdatedForPort,
                    (msg::package_name),
                    "",
                    "No files were updated for {package_name}");
    DECLARE_MESSAGE(AddVersionPortFilesShaChanged,
                    (msg::package_name),
                    "",
                    "checked-in files for {package_name} have changed but the version was not updated");
    DECLARE_MESSAGE(AddVersionVersionIs, (msg::version), "", "version: {version}");
    DECLARE_MESSAGE(AddVersionOldShaIs, (msg::value), "{value} is a 40-digit hexadecimal SHA", "old SHA: {value}");
    DECLARE_MESSAGE(AddVersionNewShaIs, (msg::value), "{value} is a 40-digit hexadecimal SHA", "new SHA: {value}");
    DECLARE_MESSAGE(AddVersionUpdateVersionReminder, (), "", "Did you remember to update the version or port version?");
    DECLARE_MESSAGE(AddVersionOverwriteOptionSuggestion,
                    (msg::option),
                    "The -- before {option} must be preserved as they're part of the help message for the user.",
                    "Use --{option} to bypass this check");
    DECLARE_MESSAGE(AddVersionUnableToParseVersionsFile, (msg::path), "", "unable to parse versions file {path}");
    DECLARE_MESSAGE(AddVersionFileNotFound, (msg::path), "", "couldn't find required file {path}");
    DECLARE_MESSAGE(AddVersionIgnoringOptionAll,
                    (msg::option),
                    "The -- before {option} must be preserved as they're part of the help message for the user.",
                    "ignoring --{option} since a port name argument was provided");
    DECLARE_MESSAGE(AddVersionUseOptionAll,
                    (msg::command_name, msg::option),
                    "The -- before {option} must be preserved as they're part of the help message for the user.",
                    "{command_name} with no arguments requires passing --{option} to update all port versions at once");
    DECLARE_MESSAGE(AddVersionLoadPortFailed, (msg::package_name), "", "can't load port {package_name}");
    DECLARE_MESSAGE(AddVersionPortHasImproperFormat,
                    (msg::package_name),
                    "",
                    "{package_name} is not properly formatted");
    DECLARE_MESSAGE(AddVersionFormatPortSuggestion, (msg::command_line), "", "Run `{command_line}` to format the file");
    DECLARE_MESSAGE(AddVersionCommitResultReminder, (), "", "Don't forget to commit the result!");
    DECLARE_MESSAGE(AddVersionNoGitSha, (msg::package_name), "", "can't obtain SHA for port {package_name}");
    DECLARE_MESSAGE(AddVersionPortDoesNotExist, (msg::package_name), "", "{package_name} does not exist");
    DECLARE_MESSAGE(AddVersionDetectLocalChangesError,
                    (),
                    "",
                    "skipping detection of local changes due to unexpected format in git status output");
    DECLARE_MESSAGE(EnvStrFailedToExtract, (), "", "could not expand the environment string:");

    DECLARE_MESSAGE(ErrorVsCodeNotFound,
                    (msg::env_var),
                    "",
                    "Visual Studio Code was not found and the environment variable '{env_var}' is not set or invalid.");

    DECLARE_MESSAGE(ErrorVsCodeNotFoundPathExamined, (), "", "The following paths were examined:");

    DECLARE_MESSAGE(InfoSetEnvVar,
                    (msg::env_var),
                    "In this context 'editor' means IDE",
                    "You can also set the environment variable '{env_var}' to your editor of choice.");
    DECLARE_MESSAGE(AllFormatArgsUnbalancedBraces,
                    (msg::value),
                    "example of {value} is 'foo bar {'",
                    "unbalanced brace in format string \"{value}\"");
    DECLARE_MESSAGE(AllFormatArgsRawArgument,
                    (msg::value),
                    "example of {value} is 'foo {} bar'",
                    "format string \"{value}\" contains a raw format argument");

    DECLARE_MESSAGE(
        ErrorMessageMustUsePrintError,
        (msg::value),
        "{value} is is a localized message name like ErrorMessageMustUsePrintError",
        "The message named {value} starts with error:, it must be changed to prepend ErrorMessage in code instead.");
    DECLARE_MESSAGE(WarningMessageMustUsePrintWarning,
                    (msg::value),
                    "{value} is is a localized message name like WarningMessageMustUsePrintWarning",
                    "The message named {value} starts with warning:, it must be changed to prepend "
                    "WarningMessage in code instead.");
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
    DECLARE_MESSAGE(GenerateMsgErrorParsingFormatArgs,
                    (msg::value),
                    "example of {value} 'GenerateMsgNoComment'",
                    "parsing format string for {value}:");

    DECLARE_MESSAGE(GenerateMsgIncorrectComment,
                    (msg::value),
                    "example of {value} is 'GenerateMsgNoComment'",
                    R"(message {value} has an incorrect comment:)");
    DECLARE_MESSAGE(GenerateMsgNoCommentValue,
                    (msg::value),
                    "example of {value} is 'arch'",
                    R"({{{value}}} was used in the message, but not commented.)");
    DECLARE_MESSAGE(GenerateMsgNoArgumentValue,
                    (msg::value),
                    "example of {value} is 'arch'",
                    R"({{{value}}} was specified in a comment, but was not used in the message.)");
    DECLARE_MESSAGE(UpdateBaselineNoConfiguration,
                    (),
                    "",
                    "neither `vcpkg.json` nor `vcpkg-configuration.json` exist to update.");

    DECLARE_MESSAGE(UpdateBaselineNoExistingBuiltinBaseline,
                    (msg::option),
                    "",
                    "the manifest file currently does not contain a `builtin-baseline` field; in order to "
                    "add one, pass the --{option} switch.");
    DECLARE_MESSAGE(
        UpdateBaselineAddBaselineNoManifest,
        (msg::option),
        "",
        "the --{option} switch was passed, but there is no manifest file to add a `builtin-baseline` field to.");

    DECLARE_MESSAGE(UpdateBaselineUpdatedBaseline,
                    (msg::url, msg::old_value, msg::new_value),
                    "example of {old_value}, {new_value} is '5507daa796359fe8d45418e694328e878ac2b82f'",
                    "updated registry '{url}': baseline '{old_value}' -> '{new_value}'");
    DECLARE_MESSAGE(UpdateBaselineNoUpdate,
                    (msg::url, msg::value),
                    "example of {value} is '5507daa796359fe8d45418e694328e878ac2b82f'",
                    "registry '{url}' not updated: '{value}'");
}
