#include <vcpkg/base/cofffilereader.h>
#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/fmt.h>
#include <vcpkg/base/message_sinks.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/parallel-algorithms.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.process.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.build.h>
#include <vcpkg/dependencies.h>
#include <vcpkg/installedpaths.h>
#include <vcpkg/packagespec.h>
#include <vcpkg/postbuildlint.h>
#include <vcpkg/vcpkgpaths.h>

namespace vcpkg
{
    static constexpr StringLiteral debug_lib_relative_path = "debug" VCPKG_PREFERRED_SEPARATOR "lib";
    static constexpr StringLiteral release_lib_relative_path = "lib";
    static constexpr StringLiteral lib_relative_paths[] = {debug_lib_relative_path, release_lib_relative_path};
    static constexpr StringLiteral debug_bin_relative_path = "debug" VCPKG_PREFERRED_SEPARATOR "bin";
    static constexpr StringLiteral release_bin_relative_path = "bin";
    static constexpr StringLiteral bin_relative_paths[] = {debug_bin_relative_path, release_bin_relative_path};

    constexpr static StringLiteral windows_system_names[] = {
        "",
        "Windows",
        "WindowsStore",
        "MinGW",
    };

    enum class LintStatus
    {
        SUCCESS = 0,
        PROBLEM_DETECTED = 1
    };

    static void print_relative_paths(MessageSink& msg_sink,
                                     msg::MessageT<> kind_prefix,
                                     const Path& relative_dir,
                                     const std::vector<Path>& relative_paths)
    {
        auto ls =
            LocalizedString().append_raw(relative_dir).append_raw(": ").append_raw(NotePrefix).append(kind_prefix);
        for (const Path& package_relative_path : relative_paths)
        {
            auto as_generic = package_relative_path;
            as_generic.make_generic();
            ls.append_raw('\n').append_raw(NotePrefix).append_raw(as_generic);
        }

        msg_sink.println(ls);
    }

    // clang-format off
#define OUTDATED_V_NO_120 \
    "msvcp100.dll",         \
    "msvcp100d.dll",        \
    "msvcp110.dll",         \
    "msvcp110_win.dll",     \
    "msvcp60.dll",          \
    "msvcr60.dll",          \
                            \
    "msvcrt.dll",           \
    "msvcr100.dll",         \
    "msvcr100d.dll",        \
    "msvcr100_clr0400.dll", \
    "msvcr110.dll",         \
    "msvcr110d.dll",         \
    "msvcrt20.dll",         \
    "msvcrt40.dll"

    // clang-format on

    static View<StringLiteral> get_outdated_dynamic_crts(const Optional<std::string>& toolset_version)
    {
        static constexpr StringLiteral V_NO_120[] = {OUTDATED_V_NO_120};

        static constexpr StringLiteral V_NO_MSVCRT[] = {
            OUTDATED_V_NO_120,
            "msvcp120.dll",
            "msvcp120d.dll",
            "msvcp120_clr0400.dll",
            "msvcr120.dll",
            "msvcr120d.dll",
            "msvcr120_clr0400.dll",
        };

        const auto tsv = toolset_version.get();
        if (tsv && (*tsv) == "v120")
        {
            return V_NO_120;
        }

        // Default case for all version >= VS 2015.
        return V_NO_MSVCRT;
    }

#undef OUTDATED_V_NO_120

    static LintStatus check_for_files_in_include_directory(const ReadOnlyFilesystem& fs,
                                                           const Path& package_dir,
                                                           const Path& portfile_cmake,
                                                           MessageSink& msg_sink)
    {
        const auto include_dir = package_dir / "include";
        if (!fs.exists(include_dir, IgnoreErrors{}) || fs.is_empty(include_dir, IgnoreErrors{}))
        {
            msg_sink.println(Color::warning,
                             LocalizedString::from_raw(portfile_cmake)
                                 .append_raw(": ")
                                 .append_raw(WarningPrefix)
                                 .append(msgPortBugMissingIncludeDir));

            return LintStatus::PROBLEM_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    static LintStatus check_for_no_files_in_cmake_helper_port_include_directory(const ReadOnlyFilesystem& fs,
                                                                                const Path& package_dir,
                                                                                const Path& portfile_cmake,
                                                                                MessageSink& msg_sink)
    {
        const auto include_dir = package_dir / "include";
        if (fs.exists(include_dir, IgnoreErrors{}))
        {
            msg_sink.println(Color::warning,
                             LocalizedString::from_raw(portfile_cmake)
                                 .append_raw(": ")
                                 .append_raw(WarningPrefix)
                                 .append(msgPortBugIncludeDirInCMakeHelperPort));
            return LintStatus::PROBLEM_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    static LintStatus check_for_restricted_include_files(const ReadOnlyFilesystem& fs,
                                                         const Path& package_dir,
                                                         const Path& portfile_cmake,
                                                         MessageSink& msg_sink)
    {
        // These files are taken from the libc6-dev package on Ubuntu inside /usr/include/x86_64-linux-gnu/sys/
        static constexpr StringLiteral restricted_sys_filenames[] = {
            "acct.h",      "auxv.h",        "bitypes.h",  "cdefs.h",    "debugreg.h",  "dir.h",         "elf.h",
            "epoll.h",     "errno.h",       "eventfd.h",  "fanotify.h", "fcntl.h",     "file.h",        "fsuid.h",
            "gmon.h",      "gmon_out.h",    "inotify.h",  "io.h",       "ioctl.h",     "ipc.h",         "kd.h",
            "klog.h",      "mman.h",        "mount.h",    "msg.h",      "mtio.h",      "param.h",       "pci.h",
            "perm.h",      "personality.h", "poll.h",     "prctl.h",    "procfs.h",    "profil.h",      "ptrace.h",
            "queue.h",     "quota.h",       "random.h",   "raw.h",      "reboot.h",    "reg.h",         "resource.h",
            "select.h",    "sem.h",         "sendfile.h", "shm.h",      "signal.h",    "signalfd.h",    "socket.h",
            "socketvar.h", "soundcard.h",   "stat.h",     "statfs.h",   "statvfs.h",   "stropts.h",     "swap.h",
            "syscall.h",   "sysctl.h",      "sysinfo.h",  "syslog.h",   "sysmacros.h", "termios.h",     "time.h",
            "timeb.h",     "timerfd.h",     "times.h",    "timex.h",    "ttychars.h",  "ttydefaults.h", "types.h",
            "ucontext.h",  "uio.h",         "un.h",       "unistd.h",   "user.h",      "ustat.h",       "utsname.h",
            "vfs.h",       "vlimit.h",      "vm86.h",     "vt.h",       "vtimes.h",    "wait.h",        "xattr.h",
        };
        // These files are taken from the libc6-dev package on Ubuntu inside the /usr/include/ folder
        static constexpr StringLiteral restricted_crt_filenames[] = {
            "_G_config.h", "aio.h",         "aliases.h",      "alloca.h",       "ar.h",        "argp.h",
            "argz.h",      "assert.h",      "byteswap.h",     "complex.h",      "cpio.h",      "crypt.h",
            "ctype.h",     "dirent.h",      "dlfcn.h",        "elf.h",          "endian.h",    "envz.h",
            "err.h",       "errno.h",       "error.h",        "execinfo.h",     "fcntl.h",     "features.h",
            "fenv.h",      "fmtmsg.h",      "fnmatch.h",      "fstab.h",        "fts.h",       "ftw.h",
            "gconv.h",     "getopt.h",      "glob.h",         "gnu-versions.h", "grp.h",       "gshadow.h",
            "iconv.h",     "ifaddrs.h",     "inttypes.h",     "langinfo.h",     "lastlog.h",   "libgen.h",
            "libintl.h",   "libio.h",       "limits.h",       "link.h",         "locale.h",    "malloc.h",
            "math.h",      "mcheck.h",      "memory.h",       "mntent.h",       "monetary.h",  "mqueue.h",
            "netash",      "netdb.h",       "nl_types.h",     "nss.h",          "obstack.h",   "paths.h",
            "poll.h",      "printf.h",      "proc_service.h", "pthread.h",      "pty.h",       "pwd.h",
            "re_comp.h",   "regex.h",       "regexp.h",       "resolv.h",       "sched.h",     "search.h",
            "semaphore.h", "setjmp.h",      "sgtty.h",        "shadow.h",       "signal.h",    "spawn.h",
            "stab.h",      "stdc-predef.h", "stdint.h",       "stdio.h",        "stdio_ext.h", "stdlib.h",
            "string.h",    "strings.h",     "stropts.h",      "syscall.h",      "sysexits.h",  "syslog.h",
            "tar.h",       "termio.h",      "termios.h",      "tgmath.h",       "thread_db.h", "time.h",
            "ttyent.h",    "uchar.h",       "ucontext.h",     "ulimit.h",       "unistd.h",    "ustat.h",
            "utime.h",     "utmp.h",        "utmpx.h",        "values.h",       "wait.h",      "wchar.h",
            "wctype.h",    "wordexp.h",
        };
        // These files are general names that have shown to be problematic in the past
        static constexpr StringLiteral restricted_general_filenames[] = {
            "json.h",
            "parser.h",
            "lexer.h",
            "config.h",
            "local.h",
            "slice.h",
            "platform.h",
            "base64.h",
            "Makefile.am",
            "Makefile.in",
            "Makefile",
            "module.modulemap",
        };
        static constexpr Span<const StringLiteral> restricted_lists[] = {
            restricted_sys_filenames, restricted_crt_filenames, restricted_general_filenames};
        const auto include_dir = package_dir / "include";
        const auto files = fs.get_files_non_recursive(include_dir, IgnoreErrors{});
        std::set<StringView> filenames_s;
        for (auto&& file : files)
        {
            filenames_s.insert(file.filename());
        }

        std::vector<std::string> violations;
        for (auto&& flist : restricted_lists)
        {
            for (auto&& f : flist)
            {
                if (Util::Sets::contains(filenames_s, f))
                {
                    violations.push_back(fmt::format(FMT_COMPILE("<{}>"), f));
                }
            }
        }

        if (!violations.empty())
        {
            Util::sort(violations);
            msg_sink.println(Color::warning,
                             LocalizedString::from_raw(portfile_cmake)
                                 .append_raw(": ")
                                 .append_raw(WarningPrefix)
                                 .append(msgPortBugRestrictedHeaderPaths));
            msg_sink.println(LocalizedString::from_raw(include_dir)
                                 .append_raw(": ")
                                 .append_raw(NotePrefix)
                                 .append(msgPortBugRestrictedHeaderPathsNote));
            for (auto&& violation : violations)
            {
                msg_sink.println(LocalizedString::from_raw(NotePrefix).append_raw(violation));
            }

            return LintStatus::PROBLEM_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    static LintStatus check_for_files_in_debug_include_directory(const ReadOnlyFilesystem& fs,
                                                                 const Path& package_dir,
                                                                 const Path& portfile_cmake,
                                                                 MessageSink& msg_sink)
    {
        const auto debug_include_dir = package_dir / "debug" VCPKG_PREFERRED_SEPARATOR "include";

        std::vector<Path> files_found = fs.get_regular_files_recursive(debug_include_dir, IgnoreErrors{});

        Util::erase_remove_if(files_found, [](const Path& target) { return target.extension() == ".ifc"; });

        if (!files_found.empty())
        {
            msg_sink.println(Color::warning,
                             LocalizedString::from_raw(portfile_cmake)
                                 .append_raw(": ")
                                 .append_raw(WarningPrefix)
                                 .append(msgPortBugDuplicateIncludeFiles));
            msg_sink.println(LocalizedString::from_raw(NotePrefix).append(msgPortBugDuplicateIncludeFilesFixIt));
            return LintStatus::PROBLEM_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    static LintStatus check_for_files_in_debug_share_directory(const ReadOnlyFilesystem& fs,
                                                               const Path& package_dir,
                                                               const Path& portfile_cmake,
                                                               MessageSink& msg_sink)
    {
        const auto debug_share = package_dir / FileDebug / FileShare;
        if (fs.exists(debug_share, IgnoreErrors{}))
        {
            msg_sink.println(Color::warning,
                             LocalizedString::from_raw(portfile_cmake)
                                 .append_raw(": ")
                                 .append_raw(WarningPrefix)
                                 .append(msgPortBugDebugShareDir));
            return LintStatus::PROBLEM_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    static LintStatus check_for_vcpkg_port_config_in_cmake_helper_port(const ReadOnlyFilesystem& fs,
                                                                       const Path& package_dir,
                                                                       StringView package_name,
                                                                       const Path& portfile_cmake,
                                                                       MessageSink& msg_sink)
    {
        if (!fs.exists(package_dir / FileShare / package_name / FileVcpkgPortConfig, IgnoreErrors{}))
        {
            msg_sink.println(Color::warning,
                             LocalizedString::from_raw(portfile_cmake)
                                 .append_raw(": ")
                                 .append_raw(WarningPrefix)
                                 .append(msgPortBugMissingCMakeHelperPortFile));
            return LintStatus::PROBLEM_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    static LintStatus check_for_usage_forgot_install(const ReadOnlyFilesystem& fs,
                                                     const Path& port_dir,
                                                     const Path& package_dir,
                                                     const StringView package_name,
                                                     const Path& portfile_cmake,
                                                     MessageSink& msg_sink)
    {
        static constexpr StringLiteral STANDARD_INSTALL_USAGE =
            R"###(file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"))###";

        auto usage_path_from = port_dir / FileUsage;
        auto usage_path_to = package_dir / FileShare / package_name / FileUsage;

        if (fs.is_regular_file(usage_path_from) && !fs.is_regular_file(usage_path_to))
        {
            msg_sink.println(Color::warning,
                             LocalizedString::from_raw(portfile_cmake)
                                 .append_raw(": ")
                                 .append_raw(WarningPrefix)
                                 .append(msgPortBugMissingProvidedUsage));
            msg_sink.println(LocalizedString::from_raw(usage_path_from)
                                 .append_raw(": ")
                                 .append_raw(NotePrefix)
                                 .append(msgUsageTextHere)
                                 .append_raw('\n')
                                 .append_raw(NotePrefix)
                                 .append(msgUsageInstallInstructions)
                                 .append_raw('\n')
                                 .append_raw(NotePrefix)
                                 .append_raw(STANDARD_INSTALL_USAGE));
            return LintStatus::PROBLEM_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    static LintStatus check_for_misplaced_cmake_files(const ReadOnlyFilesystem& fs,
                                                      const Path& package_dir,
                                                      const Path& portfile_cmake,
                                                      MessageSink& msg_sink)
    {
        static constexpr StringLiteral deny_relative_dirs[] = {
            "cmake",
            "debug" VCPKG_PREFERRED_SEPARATOR "cmake",
            "lib" VCPKG_PREFERRED_SEPARATOR "cmake",
            "debug" VCPKG_PREFERRED_SEPARATOR "lib" VCPKG_PREFERRED_SEPARATOR "cmake",
        };

        std::vector<Path> misplaced_cmake_files;
        for (auto&& deny_relative_dir : deny_relative_dirs)
        {
            for (auto&& file :
                 fs.get_regular_files_recursive_lexically_proximate(package_dir / deny_relative_dir, IgnoreErrors{}))
            {
                if (Strings::case_insensitive_ascii_equals(file.extension(), ".cmake"))
                {
                    misplaced_cmake_files.push_back(Path(deny_relative_dir) / std::move(file));
                }
            }
        }

        if (!misplaced_cmake_files.empty())
        {
            msg_sink.println(Color::warning,
                             LocalizedString::from_raw(portfile_cmake)
                                 .append_raw(": ")
                                 .append_raw(WarningPrefix)
                                 .append(msgPortBugMisplacedCMakeFiles));
            print_relative_paths(
                msg_sink, msgFilesRelativeToThePackageDirectoryHere, package_dir, misplaced_cmake_files);
            return LintStatus::PROBLEM_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    static LintStatus check_lib_cmake_merge(const ReadOnlyFilesystem& fs,
                                            const Path& package_dir,
                                            const Path& portfile_cmake,
                                            MessageSink& msg_sink)
    {
        if (fs.exists(package_dir / "lib" VCPKG_PREFERRED_SEPARATOR "cmake", IgnoreErrors{}) ||
            fs.exists(package_dir / "debug" VCPKG_PREFERRED_SEPARATOR "lib" VCPKG_PREFERRED_SEPARATOR "cmake",
                      IgnoreErrors{}))
        {
            msg_sink.println(Color::warning,
                             LocalizedString::from_raw(portfile_cmake)
                                 .append_raw(": ")
                                 .append_raw(WarningPrefix)
                                 .append(msgPortBugMergeLibCMakeDir));
            return LintStatus::PROBLEM_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    static void add_prefix_to_all(std::vector<Path>& paths, const Path& prefix)
    {
        for (auto&& path : paths)
        {
            path = prefix / std::move(path);
        }
    }

    static std::vector<Path> find_relative_dlls(const ReadOnlyFilesystem& fs,
                                                const Path& package_dir,
                                                StringLiteral relative_dir)
    {
        std::vector<Path> relative_dlls =
            fs.get_regular_files_recursive_lexically_proximate(package_dir / relative_dir, IgnoreErrors{});
        Util::erase_remove_if(relative_dlls, NotExtensionCaseInsensitive{".dll"});
        add_prefix_to_all(relative_dlls, relative_dir);
        return relative_dlls;
    }

    static LintStatus check_for_dlls_in_lib_dirs(const ReadOnlyFilesystem& fs,
                                                 const Path& package_dir,
                                                 const Path& portfile_cmake,
                                                 MessageSink& msg_sink)
    {
        std::vector<Path> bad_dlls;
        for (auto&& relative_path : lib_relative_paths)
        {
            Util::Vectors::append(bad_dlls, find_relative_dlls(fs, package_dir, relative_path));
        }

        if (!bad_dlls.empty())
        {
            msg_sink.println(Color::warning,
                             LocalizedString::from_raw(portfile_cmake)
                                 .append_raw(": ")
                                 .append_raw(WarningPrefix)
                                 .append(msgPortBugDllInLibDir));
            print_relative_paths(msg_sink, msgDllsRelativeToThePackageDirectoryHere, package_dir, bad_dlls);
            return LintStatus::PROBLEM_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    static LintStatus check_for_copyright_file(const ReadOnlyFilesystem& fs,
                                               StringView spec_name,
                                               const Path& package_dir,
                                               const Path& build_dir,
                                               const Path& portfile_cmake,
                                               MessageSink& msg_sink)
    {
        static constexpr StringLiteral copyright_filenames[] = {FileCopying, FileLicense, FileLicenseDotTxt};
        const auto copyright_file = package_dir / FileShare / spec_name / FileCopyright;

        switch (fs.status(copyright_file, IgnoreErrors{}))
        {
            case FileType::regular: return LintStatus::SUCCESS; break;
            case FileType::directory:
                msg_sink.println(Color::warning,
                                 LocalizedString::from_raw(portfile_cmake)
                                     .append_raw(": ")
                                     .append_raw(WarningPrefix)
                                     .append(msgCopyrightIsDir));
                return LintStatus::PROBLEM_DETECTED;
            default: break;
        }

        msg_sink.println(Color::warning,
                         LocalizedString::from_raw(portfile_cmake)
                             .append_raw(": ")
                             .append_raw(WarningPrefix)
                             .append(msgPortBugMissingLicense));

        // We only search in the root of each unpacked source archive to reduce false positives
        auto src_relative_dirs = Util::fmap(fs.get_directories_non_recursive(build_dir / "src", IgnoreErrors{}),
                                            [](Path&& full) -> Path { return Path(full.filename()); });

        if (src_relative_dirs.size() == 1)
        {
            // only one src dir, try to explain the vcpkg_install_copyright line for the user to use
            std::vector<StringLiteral> src_dir_relative_copyright_files;
            for (auto&& copyright_filename : copyright_filenames)
            {
                if (fs.is_regular_file(build_dir / "src" / src_relative_dirs[0] / copyright_filename))
                {
                    src_dir_relative_copyright_files.push_back(copyright_filename);
                }
            }

            if (!src_dir_relative_copyright_files.empty())
            {
                auto args = Util::fmap(src_dir_relative_copyright_files, [](StringLiteral copyright_file) {
                    return fmt::format(FMT_COMPILE("\"${{SOURCE_PATH}}/{}\""), copyright_file);
                });
                msg_sink.println(
                    LocalizedString::from_raw(portfile_cmake)
                        .append_raw(": ")
                        .append_raw(NotePrefix)
                        .append(msgPortBugMissingLicenseFixIt,
                                msg::value = fmt::format(FMT_COMPILE("vcpkg_install_copyright(FILE_LIST {})"),
                                                         fmt::join(args, " "))));
            }

            return LintStatus::PROBLEM_DETECTED;
        }

        Util::sort(src_relative_dirs);
        auto& build_dir_relative_dirs = src_relative_dirs;
        add_prefix_to_all(build_dir_relative_dirs, "src");
        std::vector<Path> build_dir_relative_copyright_files;
        for (auto&& build_dir_relative_dir : build_dir_relative_dirs)
        {
            for (auto&& copyright_filename : copyright_filenames)
            {
                if (fs.is_regular_file(build_dir / build_dir_relative_dir / copyright_filename))
                {
                    build_dir_relative_copyright_files.push_back(build_dir_relative_dir / copyright_filename);
                }
            }
        }

        if (!build_dir_relative_copyright_files.empty())
        {
            msg_sink.println(LocalizedString::from_raw(portfile_cmake)
                                 .append_raw(": ")
                                 .append_raw(NotePrefix)
                                 .append(msgPortBugFoundCopyrightFiles));
            print_relative_paths(
                msg_sink, msgFilesRelativeToTheBuildDirectoryHere, build_dir, build_dir_relative_copyright_files);
        }

        return LintStatus::PROBLEM_DETECTED;
    }

    static LintStatus check_for_exes_in_bin_dirs(const ReadOnlyFilesystem& fs,
                                                 const Path& package_dir,
                                                 const Path& portfile_cmake,
                                                 MessageSink& msg_sink)
    {
        std::vector<Path> exes;
        for (auto&& bin_relative_path : bin_relative_paths)
        {
            auto this_bad_exes =
                fs.get_regular_files_recursive_lexically_proximate(package_dir / bin_relative_path, IgnoreErrors{});
            Util::erase_remove_if(this_bad_exes, NotExtensionCaseInsensitive{".exe"});
            add_prefix_to_all(this_bad_exes, bin_relative_path);
            Util::Vectors::append(exes, std::move(this_bad_exes));
        }

        if (!exes.empty())
        {
            msg_sink.println(Color::warning,
                             LocalizedString::from_raw(portfile_cmake)
                                 .append_raw(": ")
                                 .append_raw(WarningPrefix)
                                 .append(msgPortBugFoundExeInBinDir));

            print_relative_paths(msg_sink, msgExecutablesRelativeToThePackageDirectoryHere, package_dir, exes);
            return LintStatus::PROBLEM_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    struct PostBuildCheckDllData
    {
        Path relative_path;
        MachineType machine_type;
        bool is_arm64_ec;
        bool has_exports;
        bool has_appcontainer;
        std::vector<std::string> dependencies;
    };

    static ExpectedL<PostBuildCheckDllData> try_load_dll_data(const ReadOnlyFilesystem& fs,
                                                              const Path& package_dir,
                                                              const Path& relative_path)
    {
        auto maybe_file = fs.try_open_for_read(package_dir / relative_path);
        auto file = maybe_file.get();
        if (!file)
        {
            return std::move(maybe_file).error();
        }

        auto maybe_metadata = try_read_dll_metadata_required(*file);
        auto metadata = maybe_metadata.get();
        if (!metadata)
        {
            return std::move(maybe_metadata).error();
        }

        auto maybe_has_exports = try_read_if_dll_has_exports(*metadata, *file);
        auto phas_exports = maybe_has_exports.get();
        if (!phas_exports)
        {
            return std::move(maybe_has_exports).error();
        }

        bool has_exports = *phas_exports;
        bool has_appcontainer;
        switch (metadata->pe_type)
        {
            case PEType::PE32:
                has_appcontainer = metadata->pe_headers.dll_characteristics & DllCharacteristics::AppContainer;
                break;
            case PEType::PE32Plus:
                has_appcontainer = metadata->pe_plus_headers.dll_characteristics & DllCharacteristics::AppContainer;
                break;
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }

        auto maybe_dependencies = try_read_dll_imported_dll_names(*metadata, *file);
        auto dependencies = maybe_dependencies.get();
        if (!dependencies)
        {
            return std::move(maybe_dependencies).error();
        }

        return PostBuildCheckDllData{relative_path,
                                     metadata->get_machine_type(),
                                     metadata->is_arm64_ec(),
                                     has_exports,
                                     has_appcontainer,
                                     std::move(*dependencies)};
    }

    static LintStatus check_exports_of_dlls(const std::vector<PostBuildCheckDllData>& dlls_data,
                                            const Path& package_dir,
                                            const Path& portfile_cmake,
                                            MessageSink& msg_sink)
    {
        std::vector<Path> dlls_with_no_exports;
        for (const PostBuildCheckDllData& dll_data : dlls_data)
        {
            if (!dll_data.has_exports)
            {
                dlls_with_no_exports.push_back(dll_data.relative_path);
            }
        }

        if (!dlls_with_no_exports.empty())
        {
            msg_sink.println(Color::warning,
                             LocalizedString::from_raw(portfile_cmake)
                                 .append_raw(": ")
                                 .append_raw(WarningPrefix)
                                 .append(msgPortBugSetDllsWithoutExports));
            print_relative_paths(msg_sink, msgDllsRelativeToThePackageDirectoryHere, package_dir, dlls_with_no_exports);
            return LintStatus::PROBLEM_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    static LintStatus check_appcontainer_bit_if_uwp(StringView expected_system_name,
                                                    const Path& package_dir,
                                                    const Path& portfile_cmake,
                                                    const std::vector<PostBuildCheckDllData>& dlls_data,
                                                    MessageSink& msg_sink)
    {
        if (expected_system_name != "WindowsStore")
        {
            return LintStatus::SUCCESS;
        }

        std::vector<Path> dlls_with_improper_uwp_bit;
        for (const PostBuildCheckDllData& dll_data : dlls_data)
        {
            if (!dll_data.has_appcontainer)
            {
                dlls_with_improper_uwp_bit.push_back(dll_data.relative_path);
            }
        }

        if (!dlls_with_improper_uwp_bit.empty())
        {
            msg_sink.println(Color::warning,
                             LocalizedString::from_raw(portfile_cmake)
                                 .append_raw(": ")
                                 .append_raw(WarningPrefix)
                                 .append(msgPortBugDllAppContainerBitNotSet));
            print_relative_paths(
                msg_sink, msgDllsRelativeToThePackageDirectoryHere, package_dir, dlls_with_improper_uwp_bit);
            return LintStatus::PROBLEM_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    struct FileAndArch
    {
        Path relative_file;
        std::string actual_arch;
    };

    static std::string get_printable_architecture(const MachineType& machine_type)
    {
        switch (machine_type)
        {
            case MachineType::UNKNOWN: return "unknown";
            case MachineType::AM33: return "matsushita-am33";
            case MachineType::AMD64: return "x64";
            case MachineType::ARM: return "arm";
            case MachineType::ARM64: return "arm64";
            case MachineType::ARM64EC: return "arm64ec";
            case MachineType::ARM64X: return "arm64x";
            case MachineType::ARMNT: return "arm";
            case MachineType::EBC: return "efi-byte-code";
            case MachineType::I386: return "x86";
            case MachineType::IA64: return "ia64";
            case MachineType::M32R: return "mitsubishi-m32r-le";
            case MachineType::MIPS16: return "mips16";
            case MachineType::MIPSFPU: return "mipsfpu";
            case MachineType::MIPSFPU16: return "mipsfpu16";
            case MachineType::POWERPC: return "ppc";
            case MachineType::POWERPCFP: return "ppcfp";
            case MachineType::R4000: return "mips-le";
            case MachineType::RISCV32: return "riscv-32";
            case MachineType::RISCV64: return "riscv-64";
            case MachineType::RISCV128: return "riscv-128";
            case MachineType::SH3: return "hitachi-sh3";
            case MachineType::SH3DSP: return "hitachi-sh3-dsp";
            case MachineType::SH4: return "hitachi-sh4";
            case MachineType::SH5: return "hitachi-sh5";
            case MachineType::THUMB: return "thumb";
            case MachineType::WCEMIPSV2: return "mips-le-wce-v2";
            case MachineType::LLVM_BITCODE: return "llvm-bitcode";
            case MachineType::LOONGARCH32: return "loongarch32";
            case MachineType::LOONGARCH64: return "loongarch64";
            default: return fmt::format(FMT_COMPILE("unknown-{}"), static_cast<uint16_t>(machine_type));
        }
    }

    static void print_invalid_architecture_files(const std::string& expected_architecture,
                                                 const Path& package_dir,
                                                 const Path& portfile_cmake,
                                                 std::vector<FileAndArch> binaries_with_invalid_architecture,
                                                 MessageSink& msg_sink)
    {
        msg_sink.println(Color::warning,
                         LocalizedString::from_raw(portfile_cmake)
                             .append_raw(": ")
                             .append_raw(WarningPrefix)
                             .append(msgBuiltWithIncorrectArchitecture, msg::arch = expected_architecture));

        auto msg = LocalizedString::from_raw(package_dir)
                       .append_raw(": ")
                       .append_raw(NotePrefix)
                       .append(msgBinariesRelativeToThePackageDirectoryHere);
        for (const FileAndArch& b : binaries_with_invalid_architecture)
        {
            msg.append_raw('\n')
                .append_raw(NotePrefix)
                .append(msgBinaryWithInvalidArchitecture,
                        msg::path = b.relative_file.generic_u8string(),
                        msg::arch = b.actual_arch);
        }

        msg_sink.println(msg);
    }

    static void check_dll_architecture(const std::string& expected_architecture,
                                       const std::vector<PostBuildCheckDllData>& dlls_data,
                                       std::vector<FileAndArch>& binaries_with_invalid_architecture)
    {
        for (const PostBuildCheckDllData& dll_data : dlls_data)
        {
            const std::string actual_architecture = get_printable_architecture(dll_data.machine_type);
            if (expected_architecture == "arm64ec")
            {
                if (dll_data.machine_type != MachineType::AMD64 || !dll_data.is_arm64_ec)
                {
                    binaries_with_invalid_architecture.push_back({dll_data.relative_path, actual_architecture});
                }
            }
            else if (expected_architecture != actual_architecture)
            {
                binaries_with_invalid_architecture.push_back({dll_data.relative_path, actual_architecture});
            }
        }
    }

    static std::vector<Optional<LibInformation>> get_lib_info(const Filesystem& fs,
                                                              const Path& relative_root,
                                                              View<Path> libs)
    {
        std::vector<Optional<LibInformation>> maybe_lib_infos(libs.size());
        std::transform(libs.begin(),
                       libs.end(),
                       maybe_lib_infos.begin(),
                       [&](const Path& relative_lib) -> Optional<LibInformation> {
                           auto maybe_rfp = fs.try_open_for_read(relative_root / relative_lib);

                           if (auto file_handle = maybe_rfp.get())
                           {
                               auto maybe_lib_info = read_lib_information(*file_handle);
                               if (auto lib_info = maybe_lib_info.get())
                               {
                                   return std::move(*lib_info);
                               }
                               return nullopt;
                           }
                           return nullopt;
                       });
        return maybe_lib_infos;
    }

    // lib_infos[n] is the lib info for libs[n] for all n in [0, libs.size())
    static void check_lib_architecture(const std::string& expected_architecture,
                                       View<Path> relative_libs,
                                       View<Optional<LibInformation>> lib_infos,
                                       std::vector<FileAndArch>& binaries_with_invalid_architecture)
    {
        for (size_t i = 0; i < relative_libs.size(); ++i)
        {
            auto& maybe_lib_information = lib_infos[i];
            auto& relative_lib = relative_libs[i];

            if (!maybe_lib_information.has_value())
            {
                continue;
            }

            auto machine_types = maybe_lib_information.value_or_exit(VCPKG_LINE_INFO).machine_types;
            {
                auto llvm_bitcode = std::find(machine_types.begin(), machine_types.end(), MachineType::LLVM_BITCODE);
                if (llvm_bitcode != machine_types.end())
                {
                    machine_types.erase(llvm_bitcode);
                }
            }

            auto printable_machine_types =
                Util::fmap(machine_types, [](MachineType mt) { return get_printable_architecture(mt); });
            // Either machine_types is empty (meaning this lib is architecture independent), or
            // we need at least one of the machine types to match.
            // Agnostic example: Folly's debug library, LLVM LTO libraries
            // Multiple example: arm64x libraries
            if (!printable_machine_types.empty() &&
                !Util::Vectors::contains(printable_machine_types, expected_architecture))
            {
                binaries_with_invalid_architecture.push_back(
                    {relative_lib, Strings::join(",", printable_machine_types)});
            }
        }
    }

    static LintStatus check_no_dlls_present(const Path& package_dir,
                                            const std::vector<Path>& relative_dlls,
                                            const Path& portfile_cmake,
                                            MessageSink& msg_sink)
    {
        if (relative_dlls.empty())
        {
            return LintStatus::SUCCESS;
        }

        msg_sink.println(Color::warning,
                         LocalizedString::from_raw(portfile_cmake)
                             .append_raw(": ")
                             .append_raw(WarningPrefix)
                             .append(msgPortBugFoundDllInStaticBuild));
        print_relative_paths(msg_sink, msgDllsRelativeToThePackageDirectoryHere, package_dir, relative_dlls);
        return LintStatus::PROBLEM_DETECTED;
    }

    static size_t total_size(View<View<Path>> path_sets)
    {
        size_t result = 0;
        for (auto&& paths : path_sets)
        {
            result += paths.size();
        }
        return result;
    }

    static void append_binary_set(LocalizedString& ls, View<View<Path>> relative_binary_sets)
    {
        for (auto&& binary_set : relative_binary_sets)
        {
            for (auto&& binary : binary_set)
            {
                auto as_generic = binary;
                as_generic.make_generic();
                ls.append_raw('\n').append_raw(NotePrefix).append_raw(as_generic);
            }
        }
    }

    static LintStatus check_matching_debug_and_release_binaries(const Path& package_dir,
                                                                View<View<Path>> relative_debug_binary_sets,
                                                                View<View<Path>> relative_release_binary_sets,
                                                                const Path& portfile_cmake,
                                                                MessageSink& msg_sink)
    {
        const size_t debug_count = total_size(relative_debug_binary_sets);
        const size_t release_count = total_size(relative_release_binary_sets);
        if (debug_count == release_count)
        {
            return LintStatus::SUCCESS;
        }

        msg_sink.println(Color::warning,
                         LocalizedString::from_raw(portfile_cmake)
                             .append_raw(": ")
                             .append_raw(WarningPrefix)
                             .append(msgPortBugMismatchingNumberOfBinaries));
        LocalizedString ls = LocalizedString::from_raw(package_dir)
                                 .append_raw(": ")
                                 .append_raw(NotePrefix)
                                 .append(msgBinariesRelativeToThePackageDirectoryHere);
        if (debug_count == 0)
        {
            ls.append_raw('\n').append_raw(NotePrefix).append(msgPortBugMissingDebugBinaries);
        }
        else
        {
            ls.append_raw('\n').append_raw(NotePrefix).append(msgPortBugFoundDebugBinaries);
            append_binary_set(ls, relative_debug_binary_sets);
        }

        if (release_count == 0)
        {
            ls.append_raw('\n').append_raw(NotePrefix).append(msgPortBugMissingReleaseBinaries);
        }
        else
        {
            ls.append_raw('\n').append_raw(NotePrefix).append(msgPortBugFoundReleaseBinaries);
            append_binary_set(ls, relative_release_binary_sets);
        }

        msg_sink.println(ls);
        return LintStatus::PROBLEM_DETECTED;
    }

    static LintStatus check_lib_files_are_available_if_dlls_are_available(const size_t lib_count,
                                                                          const size_t dll_count,
                                                                          const Path& portfile_cmake,
                                                                          MessageSink& msg_sink)
    {
        if (lib_count == 0 && dll_count != 0)
        {
            msg_sink.println(LocalizedString::from_raw(portfile_cmake)
                                 .append_raw(": ")
                                 .append_raw(WarningPrefix)
                                 .append(msgPortBugMissingImportedLibs));
            return LintStatus::PROBLEM_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    static size_t check_bin_folders_are_not_present_in_static_build(const ReadOnlyFilesystem& fs,
                                                                    const Path& package_dir,
                                                                    const Path& portfile_cmake,
                                                                    MessageSink& msg_sink)
    {
        std::vector<Path> bad_dirs;
        for (auto&& bin_relative_path : bin_relative_paths)
        {
            if (fs.exists(package_dir / bin_relative_path, IgnoreErrors{}))
            {
                bad_dirs.push_back(Path(bin_relative_path).generic_u8string());
            }
        }

        if (bad_dirs.empty())
        {
            return 0;
        }

        for (auto&& bad_dir : bad_dirs)
        {
            msg_sink.println(Color::warning,
                             LocalizedString::from_raw(portfile_cmake)
                                 .append_raw(": ")
                                 .append_raw(WarningPrefix)
                                 .append(msgPortBugBinDirExists, msg::path = bad_dir));
        }

        auto args = Util::fmap(bad_dirs, [](const Path& bad_dir) {
            return fmt::format(FMT_COMPILE("\"${{CURRENT_PACKAGES_DIR}}/{}\""), bad_dir);
        });
        msg_sink.println(
            LocalizedString::from_raw(NotePrefix)
                .append(msgPortBugRemoveBinDir)
                .append_raw('\n')
                .append_raw("if(VCPKG_LIBRARY_LINKAGE STREQUAL \"static\")\n")
                .append_indent()
                .append_raw(fmt::format(FMT_COMPILE("file(REMOVE_RECURSE {})\nendif()"), fmt::join(args, " "))));
        return bad_dirs.size();
    }

    static LintStatus check_no_empty_folders(const ReadOnlyFilesystem& fs,
                                             const Path& package_dir,
                                             const Path& portfile_cmake,
                                             MessageSink& msg_sink)
    {
        std::vector<Path> relative_empty_directories =
            fs.get_directories_recursive_lexically_proximate(package_dir, IgnoreErrors{});
        Util::erase_remove_if(relative_empty_directories,
                              [&](const Path& current) { return !fs.is_empty(package_dir / current, IgnoreErrors{}); });
        if (!relative_empty_directories.empty())
        {
            Util::sort(relative_empty_directories);
            msg_sink.println(Color::warning,
                             LocalizedString::from_raw(portfile_cmake)
                                 .append_raw(": ")
                                 .append_raw(WarningPrefix)
                                 .append(msgPortBugFoundEmptyDirectories));

            auto args = Util::fmap(relative_empty_directories, [](const Path& empty_dir) {
                return fmt::format(FMT_COMPILE("\"${{CURRENT_PACKAGES_DIR}}/{}\""), empty_dir.generic_u8string());
            });
            msg_sink.println(
                LocalizedString::from_raw(package_dir)
                    .append_raw(": ")
                    .append_raw(NotePrefix)
                    .append(msgDirectoriesRelativeToThePackageDirectoryHere)
                    .append_raw('\n')
                    .append_raw(NotePrefix)
                    .append_raw(fmt::format(FMT_COMPILE("file(REMOVE_RECURSE {})"), fmt::join(args, " "))));
            return LintStatus::PROBLEM_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    static LintStatus check_pkgconfig_dir_only_in_lib_dir(const ReadOnlyFilesystem& fs,
                                                          const Path& package_dir,
                                                          View<Path> relative_all_files,
                                                          const Path& portfile_cmake,
                                                          MessageSink& msg_sink)
    {
        struct MisplacedFile
        {
            Path relative_path;
            enum class Type
            {
                Release,
                Debug,
                Share
            } type;
        };
        std::vector<MisplacedFile> misplaced_pkgconfig_files;
        bool contains_release = false;
        bool contains_debug = false;
        bool contains_share = false;

        static constexpr StringLiteral share_dir = "share" VCPKG_PREFERRED_SEPARATOR "pkgconfig";
        static constexpr StringLiteral lib_dir = "lib" VCPKG_PREFERRED_SEPARATOR "pkgconfig";
        static constexpr StringLiteral debug_dir =
            "debug" VCPKG_PREFERRED_SEPARATOR "lib" VCPKG_PREFERRED_SEPARATOR "pkgconfig";

        static constexpr StringLiteral generic_share_dir = "share/pkgconfig";
        static constexpr StringLiteral generic_lib_dir = "lib/pkgconfig";
        static constexpr StringLiteral generic_debug_dir = "debug/lib/pkgconfig";
        for (const Path& path : relative_all_files)
        {
            if (!Strings::ends_with(path, ".pc")) continue;
            const auto parent_path = path.parent_path();
            // Always allow .pc files at 'lib/pkgconfig' and 'debug/lib/pkgconfig'
            if (parent_path == lib_dir || parent_path == debug_dir) continue;

            const bool contains_libs = Util::any_of(
                fs.read_lines(package_dir / path).value_or_exit(VCPKG_LINE_INFO), [](const std::string& line) {
                    if (Strings::starts_with(line, "Libs"))
                    {
                        // only consider "Libs:" or "Libs.private:" directives when they have a value
                        const auto colon = line.find_first_of(':');
                        if (colon != std::string::npos && line.find_first_not_of(' ', colon + 1) != std::string::npos)
                            return true;
                    }
                    return false;
                });
            // Allow .pc in "share/pkgconfig" if and only if it contains no "Libs:" or "Libs.private:" directives:
            if (!contains_libs)
            {
                if (parent_path == share_dir) continue;
                contains_share = true;
                misplaced_pkgconfig_files.push_back({path, MisplacedFile::Type::Share});
                continue;
            }

            const bool is_debug = Strings::starts_with(path, "debug" VCPKG_PREFERRED_SEPARATOR);
            if (is_debug)
            {
                misplaced_pkgconfig_files.push_back({path, MisplacedFile::Type::Debug});
                contains_debug = true;
            }
            else
            {
                misplaced_pkgconfig_files.push_back({path, MisplacedFile::Type::Release});
                contains_release = true;
            }
        }

        if (!misplaced_pkgconfig_files.empty())
        {
            msg_sink.println(Color::warning,
                             LocalizedString::from_raw(portfile_cmake)
                                 .append_raw(": ")
                                 .append_raw(WarningPrefix)
                                 .append(msgPortBugMisplacedPkgConfigFiles));
            print_relative_paths(msg_sink,
                                 msgFilesRelativeToThePackageDirectoryHere,
                                 package_dir,
                                 Util::fmap(misplaced_pkgconfig_files,
                                            [](const MisplacedFile& mf) -> Path { return mf.relative_path; }));

            msg_sink.println(LocalizedString::from_raw(NotePrefix).append(msgPortBugMovePkgConfigFiles));
            {
                auto create_directory_line = LocalizedString::from_raw("file(MAKE_DIRECTORY");
                std::vector<StringLiteral> directories;
                if (contains_debug)
                {
                    directories.push_back(generic_debug_dir);
                }

                if (contains_release)
                {
                    directories.push_back(generic_lib_dir);
                }

                if (contains_share)
                {
                    directories.push_back(generic_share_dir);
                }

                for (auto&& directory : directories)
                {
                    create_directory_line.append_raw(
                        fmt::format(FMT_COMPILE(R"###( "${{CURRENT_PACKAGES_DIR}}/{}")###"), directory));
                }

                create_directory_line.append_raw(")");
                msg_sink.println(create_directory_line);
            } // destroy create_directory_line

            for (const auto& item : misplaced_pkgconfig_files)
            {
                const StringLiteral* dir;
                switch (item.type)
                {
                    case MisplacedFile::Type::Debug: dir = &generic_debug_dir; break;
                    case MisplacedFile::Type::Release: dir = &generic_lib_dir; break;
                    case MisplacedFile::Type::Share: dir = &generic_share_dir; break;
                    default: Checks::unreachable(VCPKG_LINE_INFO);
                }

                msg_sink.println(LocalizedString::from_raw(fmt::format(
                    FMT_COMPILE(
                        R"###(file(RENAME "${{CURRENT_PACKAGES_DIR}}/{}" "${{CURRENT_PACKAGES_DIR}}/{}/{}"))###"),
                    item.relative_path.generic_u8string(),
                    *dir,
                    item.relative_path.filename())));
            }

            msg_sink.println(LocalizedString::from_raw("vcpkg_fixup_pkgconfig()"));
            msg_sink.println(msgPortBugRemoveEmptyDirs);

            return LintStatus::PROBLEM_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    // lib_infos[n] is the lib info for libs[n] for all n in [0, libs.size())
    struct LinkageAndBuildType
    {
        LinkageType kind;
        bool release;

        friend bool operator==(const LinkageAndBuildType& lhs, const LinkageAndBuildType& rhs) noexcept
        {
            return lhs.kind == rhs.kind && lhs.release == rhs.release;
        }
        friend bool operator!=(const LinkageAndBuildType& lhs, const LinkageAndBuildType& rhs) noexcept
        {
            return !(lhs == rhs);
        }

        bool operator<(const LinkageAndBuildType& other) const noexcept
        {
            if (static_cast<char>(kind) < static_cast<char>(other.kind))
            {
                return true;
            }

            if (static_cast<char>(kind) > static_cast<char>(other.kind))
            {
                return false;
            }

            return release < other.release;
        }
    };

    static LocalizedString to_string(const LinkageAndBuildType& linkage)
    {
        switch (linkage.kind)
        {
            case LinkageType::Dynamic:
                if (linkage.release)
                {
                    return msg::format(msgLinkageDynamicRelease);
                }
                else
                {
                    return msg::format(msgLinkageDynamicDebug);
                }
                break;
            case LinkageType::Static:
                if (linkage.release)
                {
                    return msg::format(msgLinkageStaticRelease);
                }
                else
                {
                    return msg::format(msgLinkageStaticDebug);
                }
                break;
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }
    }

    struct FileAndLinkages
    {
        Path relative_file;
        std::vector<LinkageAndBuildType> linkages;
    };

    static void check_crt_group_linkage_of_libs(
        LinkageAndBuildType expected,
        const Path& relative_root,
        const std::vector<Path>& relative_libs,
        View<Optional<LibInformation>> lib_infos,
        std::map<LinkageAndBuildType, std::vector<FileAndLinkages>>& groups_of_invalid_crt)
    {
        for (size_t i = 0; i < relative_libs.size(); ++i)
        {
            auto& maybe_lib_info = lib_infos[i];
            auto& relative_lib = relative_libs[i];

            if (!maybe_lib_info.has_value())
            {
                continue;
            }

            auto&& lib_info = maybe_lib_info.value_or_exit(VCPKG_LINE_INFO);
            Debug::println("The lib ",
                           (relative_root / relative_lib).native(),
                           " has directives: ",
                           Strings::join(" ", lib_info.linker_directives));

            FileAndLinkages this_lib{relative_lib};
            constexpr static StringLiteral dynamic_debug_crt = "/DEFAULTLIB:MSVCRTd";
            constexpr static StringLiteral dynamic_release_crt = "/DEFAULTLIB:MSVCRT";
            constexpr static StringLiteral static_debug_crt = "/DEFAULTLIB:LIBCMTd";
            constexpr static StringLiteral static_release_crt = "/DEFAULTLIB:LIBCMT";

            for (auto&& directive : lib_info.linker_directives)
            {
                if (Strings::case_insensitive_ascii_equals(directive, dynamic_debug_crt))
                {
                    this_lib.linkages.push_back(LinkageAndBuildType{LinkageType::Dynamic, false});
                }
                else if (Strings::case_insensitive_ascii_equals(directive, dynamic_release_crt))
                {
                    this_lib.linkages.push_back(LinkageAndBuildType{LinkageType::Dynamic, true});
                }
                else if (Strings::case_insensitive_ascii_equals(directive, static_debug_crt))
                {
                    this_lib.linkages.push_back(LinkageAndBuildType{LinkageType::Static, false});
                }
                else if (Strings::case_insensitive_ascii_equals(directive, static_release_crt))
                {
                    this_lib.linkages.push_back(LinkageAndBuildType{LinkageType::Static, true});
                }
            }

            Util::sort_unique_erase(this_lib.linkages);
            if (this_lib.linkages.size() > 1 || (this_lib.linkages.size() == 1 && this_lib.linkages[0] != expected))
            {
                groups_of_invalid_crt[expected].push_back(std::move(this_lib));
            }
        }
    }

    static LintStatus check_crt_linkage_of_libs(
        const Path& package_dir,
        const Path& portfile_cmake,
        std::map<LinkageAndBuildType, std::vector<FileAndLinkages>>& groups_of_invalid_crt,
        MessageSink& msg_sink)
    {
        if (groups_of_invalid_crt.empty())
        {
            return LintStatus::SUCCESS;
        }

        msg_sink.println(Color::warning,
                         LocalizedString::from_raw(portfile_cmake)
                             .append_raw(": ")
                             .append_raw(WarningPrefix)
                             .append(msgPortBugInvalidCrtLinkageHeader));

        msg_sink.println(LocalizedString::from_raw(package_dir)
                             .append_raw(": ")
                             .append_raw(NotePrefix)
                             .append(msgBinariesRelativeToThePackageDirectoryHere));
        for (auto&& group : groups_of_invalid_crt)
        {
            msg_sink.println(LocalizedString::from_raw(NotePrefix)
                                 .append(msgPortBugInvalidCrtLinkageCrtGroup, msg::expected = to_string(group.first)));
            for (auto&& file : group.second)
            {
                for (auto&& linkage : file.linkages)
                {
                    msg_sink.println(LocalizedString::from_raw(NotePrefix)
                                         .append(msgPortBugInvalidCrtLinkageEntry,
                                                 msg::path = file.relative_file.generic_u8string(),
                                                 msg::actual = to_string(linkage)));
                }
            }
        }

        return LintStatus::PROBLEM_DETECTED;
    }

    struct OutdatedDynamicCrtAndFile
    {
        Path file;
        StringLiteral outdated_crt;
    };

    static LintStatus check_outdated_crt_linkage_of_dlls(const std::vector<PostBuildCheckDllData>& dlls_data,
                                                         const Path& package_dir,
                                                         const PreBuildInfo& pre_build_info,
                                                         const Path& portfile_cmake,
                                                         MessageSink& msg_sink)
    {
        const auto outdated_crts = get_outdated_dynamic_crts(pre_build_info.platform_toolset);
        std::vector<OutdatedDynamicCrtAndFile> dlls_with_outdated_crt;

        for (const PostBuildCheckDllData& dll_data : dlls_data)
        {
            for (const StringLiteral& outdated_crt : outdated_crts)
            {
                if (Util::Vectors::contains(
                        dll_data.dependencies, outdated_crt, Strings::case_insensitive_ascii_equals))
                {
                    dlls_with_outdated_crt.push_back({dll_data.relative_path, outdated_crt});
                    break;
                }
            }
        }

        if (!dlls_with_outdated_crt.empty())
        {
            msg_sink.println(Color::warning,
                             LocalizedString::from_raw(portfile_cmake)
                                 .append_raw(": ")
                                 .append_raw(WarningPrefix)
                                 .append(msgPortBugOutdatedCRT));

            print_relative_paths(
                msg_sink,
                msgDllsRelativeToThePackageDirectoryHere,
                package_dir,
                Util::fmap(std::move(dlls_with_outdated_crt),
                           [](OutdatedDynamicCrtAndFile&& btf) -> Path { return std::move(btf.file); }));
            return LintStatus::PROBLEM_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    static LintStatus check_bad_kernel32_from_xbox(const std::vector<PostBuildCheckDllData>& dlls_data,
                                                   const Path& package_dir,
                                                   const PreBuildInfo& pre_build_info,
                                                   const Path& portfile_cmake,
                                                   MessageSink& msg_sink)
    {
        if (!pre_build_info.target_is_xbox)
        {
            return LintStatus::SUCCESS;
        }

        std::vector<const PostBuildCheckDllData*> bad_dlls;
        for (auto&& dll_data : dlls_data)
        {
            for (auto&& dependency : dll_data.dependencies)
            {
                if (Strings::case_insensitive_ascii_equals("kernel32.dll", dependency))
                {
                    bad_dlls.push_back(&dll_data);
                    break;
                }
            }
        }

        if (bad_dlls.empty())
        {
            return LintStatus::SUCCESS;
        }

        msg_sink.println(Color::warning,
                         LocalizedString::from_raw(portfile_cmake)
                             .append_raw(": ")
                             .append_raw(WarningPrefix)
                             .append(msgPortBugKernel32FromXbox));
        print_relative_paths(
            msg_sink,
            msgDllsRelativeToThePackageDirectoryHere,
            package_dir,
            Util::fmap(bad_dlls, [](const PostBuildCheckDllData* btf) -> Path { return btf->relative_path; }));

        return LintStatus::PROBLEM_DETECTED;
    }

    static LintStatus check_no_regular_files_in_relative_path(const ReadOnlyFilesystem& fs,
                                                              const Path& package_dir,
                                                              const Path& portfile_cmake,
                                                              View<StringLiteral> relative_paths,
                                                              MessageSink& msg_sink)
    {
        std::vector<Path> misplaced_files;
        for (auto&& relative_path : relative_paths)
        {
            auto start_in = package_dir;
            if (!relative_path.empty())
            {
                start_in /= relative_path;
            }

            for (auto&& absolute_path : fs.get_regular_files_non_recursive(start_in, IgnoreErrors{}))
            {
                auto filename = absolute_path.filename();
                if (filename == FileControl || filename == FileBuildInfo || filename == FileDotDsStore)
                {
                    continue;
                }

                if (relative_path.empty())
                {
                    misplaced_files.emplace_back(filename);
                }
                else
                {
                    misplaced_files.emplace_back(Path(relative_path) / filename);
                }
            }
        }

        if (!misplaced_files.empty())
        {
            msg_sink.println(Color::warning,
                             LocalizedString::from_raw(portfile_cmake)
                                 .append_raw(": ")
                                 .append_raw(WarningPrefix)
                                 .append(msgPortBugMisplacedFiles));
            print_relative_paths(msg_sink, msgFilesRelativeToThePackageDirectoryHere, package_dir, misplaced_files);
            return LintStatus::PROBLEM_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    static bool file_contains_absolute_paths(const ReadOnlyFilesystem& fs,
                                             const Path& file,
                                             View<Strings::vcpkg_searcher> searcher_paths)
    {
        const auto extension = file.extension();
        if (extension == ".h" || extension == ".hpp" || extension == ".hxx")
        {
            return Strings::contains_any_ignoring_c_comments(fs.read_contents(file, IgnoreErrors{}), searcher_paths);
        }

        if (extension == ".cfg" || extension == ".ini" || file.filename() == "usage")
        {
            const auto contents = fs.read_contents(file, IgnoreErrors{});
            return Strings::long_string_contains_any(contents, searcher_paths);
        }

        if (extension == ".py" || extension == ".sh" || extension == ".cmake" || extension == ".pc" ||
            extension == ".conf" || extension == ".csh" || extension == ".pl")
        {
            const auto contents = fs.read_contents(file, IgnoreErrors{});
            return Strings::contains_any_ignoring_hash_comments(contents, searcher_paths);
        }

        if (extension.empty())
        {
            const auto contents = fs.best_effort_read_contents_if_shebang(file);
            return Strings::contains_any_ignoring_hash_comments(contents, searcher_paths);
        }

        return false;
    }

    static LintStatus check_no_absolute_paths_in(const ReadOnlyFilesystem& fs,
                                                 const Path& package_dir,
                                                 View<Path> prohibited_absolute_paths,
                                                 View<Path> relative_all_files,
                                                 const Path& portfile_cmake,
                                                 MessageSink& msg_sink)
    {
        std::vector<std::string> string_paths;
        for (const auto& path : prohibited_absolute_paths)
        {
#if defined(_WIN32)
            // As supplied, all /s, and all \s
            string_paths.push_back(path.native());
            auto path_preferred = path;
            path_preferred.make_preferred();
            string_paths.push_back(path_preferred.native());
            string_paths.push_back(path.generic_u8string());
#else
            string_paths.push_back(path.native());
#endif
        }

        Util::sort_unique_erase(string_paths);

        const auto searcher_paths =
            Util::fmap(string_paths, [](std::string& s) { return Strings::vcpkg_searcher(s.begin(), s.end()); });

        std::vector<Path> failing_files;
        bool any_pc_file_fails = false;
        {
            std::mutex mtx;
            parallel_for_each(relative_all_files, [&](const Path& file) {
                if (file_contains_absolute_paths(fs, package_dir / file, searcher_paths))
                {
                    if (Strings::ends_with(file, ".pc"))
                    {
                        std::lock_guard lock{mtx};
                        any_pc_file_fails = true;
                        failing_files.push_back(file);
                    }
                    else
                    {
                        std::lock_guard lock{mtx};
                        failing_files.push_back(file);
                    }
                }
            });
        } // destroy mtx

        if (failing_files.empty())
        {
            return LintStatus::SUCCESS;
        }

        Util::sort(failing_files);
        msg_sink.println(Color::warning,
                         LocalizedString::from_raw(portfile_cmake)
                             .append_raw(": ")
                             .append_raw(WarningPrefix)
                             .append(msgFilesContainAbsolutePath1));
        for (auto&& absolute_path : prohibited_absolute_paths)
        {
            msg_sink.println(LocalizedString::from_raw(NotePrefix).append_raw(absolute_path));
        }

        if (any_pc_file_fails)
        {
            msg_sink.println(LocalizedString::from_raw(portfile_cmake)
                                 .append_raw(": ")
                                 .append_raw(NotePrefix)
                                 .append(msgFilesContainAbsolutePathPkgconfigNote));
        }

        for (auto&& failing_file : failing_files)
        {
            failing_file.make_preferred();
            msg_sink.println(LocalizedString::from_raw(package_dir / failing_file)
                                 .append_raw(": ")
                                 .append_raw(NotePrefix)
                                 .append(msgFilesContainAbsolutePath2));
        }

        return LintStatus::PROBLEM_DETECTED;
    }

    static void operator+=(size_t& left, const LintStatus& right) { left += static_cast<size_t>(right); }

    static size_t perform_post_build_checks_dll_loads(const ReadOnlyFilesystem& fs,
                                                      std::vector<PostBuildCheckDllData>& dlls_data,
                                                      const Path& package_dir,
                                                      const std::vector<Path>& relative_dll_files,
                                                      MessageSink& msg_sink)
    {
        size_t error_count = 0;
        for (const Path& relative_dll : relative_dll_files)
        {
            auto maybe_dll_data = try_load_dll_data(fs, package_dir, relative_dll);
            if (auto dll_data = maybe_dll_data.get())
            {
                dlls_data.emplace_back(std::move(*dll_data));
            }
            else
            {
                ++error_count;
                msg_sink.println(Color::warning, maybe_dll_data.error());
            }
        }

        return error_count;
    }

    static std::vector<Path> find_relative_static_libs(const ReadOnlyFilesystem& fs,
                                                       const bool windows_target,
                                                       const Path& package_dir,
                                                       const Path& relative_path)
    {
        View<StringLiteral> lib_extensions;
        if (windows_target)
        {
            static constexpr StringLiteral windows_lib_extensions[] = {".lib"};
            lib_extensions = windows_lib_extensions;
        }
        else
        {
            static constexpr StringLiteral unix_lib_extensions[] = {".so", ".a", ".dylib"};
            lib_extensions = unix_lib_extensions;
        }

        std::vector<Path> relative_libs =
            fs.get_regular_files_recursive_lexically_proximate(package_dir / relative_path, IgnoreErrors{});
        Util::erase_remove_if(relative_libs, NotExtensionsCaseInsensitive{lib_extensions});
        add_prefix_to_all(relative_libs, relative_path);
        return relative_libs;
    }

    static size_t perform_all_checks_and_return_error_count(const InstallPlanAction& action,
                                                            const VcpkgPaths& paths,
                                                            const PreBuildInfo& pre_build_info,
                                                            const BuildInfo& build_info,
                                                            const Path& port_dir,
                                                            const Path& portfile_cmake,
                                                            MessageSink& msg_sink)
    {
        const bool windows_target = Util::Vectors::contains(windows_system_names, pre_build_info.cmake_system_name);
        const auto& fs = paths.get_filesystem();
        const auto build_dir = paths.build_dir(action.spec);
        const auto& package_dir = action.package_dir.value_or_exit(VCPKG_LINE_INFO);
        const bool not_release_only = !pre_build_info.build_type;

        size_t error_count = 0;

        auto& policies = build_info.policies;
        if (policies.is_enabled(BuildPolicy::CMAKE_HELPER_PORT))
        {
            // no suppression for these because CMAKE_HELPER_PORT is opt-in
            error_count +=
                check_for_no_files_in_cmake_helper_port_include_directory(fs, package_dir, portfile_cmake, msg_sink);
            error_count += check_for_vcpkg_port_config_in_cmake_helper_port(
                fs, package_dir, action.spec.name(), portfile_cmake, msg_sink);
        }
        else if (!policies.is_enabled(BuildPolicy::EMPTY_INCLUDE_FOLDER))
        {
            error_count += check_for_files_in_include_directory(fs, package_dir, portfile_cmake, msg_sink);
        }

        if (!policies.is_enabled(BuildPolicy::ALLOW_RESTRICTED_HEADERS))
        {
            error_count += check_for_restricted_include_files(fs, package_dir, portfile_cmake, msg_sink);
        }
        if (!policies.is_enabled(BuildPolicy::ALLOW_DEBUG_INCLUDE))
        {
            error_count += check_for_files_in_debug_include_directory(fs, package_dir, portfile_cmake, msg_sink);
        }
        if (!policies.is_enabled(BuildPolicy::ALLOW_DEBUG_SHARE))
        {
            error_count += check_for_files_in_debug_share_directory(fs, package_dir, portfile_cmake, msg_sink);
        }
        if (!policies.is_enabled(BuildPolicy::SKIP_MISPLACED_CMAKE_FILES_CHECK))
        {
            error_count += check_for_misplaced_cmake_files(fs, package_dir, portfile_cmake, msg_sink);
        }
        if (!policies.is_enabled(BuildPolicy::SKIP_LIB_CMAKE_MERGE_CHECK))
        {
            error_count += check_lib_cmake_merge(fs, package_dir, portfile_cmake, msg_sink);
        }

        if (windows_target && !policies.is_enabled(BuildPolicy::ALLOW_DLLS_IN_LIB))
        {
            error_count += check_for_dlls_in_lib_dirs(fs, package_dir, portfile_cmake, msg_sink);
        }
        if (!policies.is_enabled(BuildPolicy::SKIP_COPYRIGHT_CHECK))
        {
            error_count +=
                check_for_copyright_file(fs, action.spec.name(), package_dir, build_dir, portfile_cmake, msg_sink);
        }
        if (windows_target && !policies.is_enabled(BuildPolicy::ALLOW_EXES_IN_BIN))
        {
            error_count += check_for_exes_in_bin_dirs(fs, package_dir, portfile_cmake, msg_sink);
        }
        if (!policies.is_enabled(BuildPolicy::SKIP_USAGE_INSTALL_CHECK))
        {
            error_count +=
                check_for_usage_forgot_install(fs, port_dir, package_dir, action.spec.name(), portfile_cmake, msg_sink);
        }

        std::vector<Path> relative_debug_libs =
            find_relative_static_libs(fs, windows_target, package_dir, debug_lib_relative_path);
        std::vector<Path> relative_release_libs =
            find_relative_static_libs(fs, windows_target, package_dir, release_lib_relative_path);
        std::vector<Path> relative_debug_dlls;
        std::vector<Path> relative_release_dlls;

        if (windows_target)
        {
            relative_debug_dlls = find_relative_dlls(fs, package_dir, debug_bin_relative_path);
            relative_release_dlls = find_relative_dlls(fs, package_dir, release_bin_relative_path);
        }

        if (not_release_only && !policies.is_enabled(BuildPolicy::MISMATCHED_NUMBER_OF_BINARIES))
        {
            View<Path> relative_debug_binary_sets[] = {relative_debug_libs, relative_debug_dlls};
            View<Path> relative_release_binary_sets[] = {relative_release_libs, relative_release_dlls};
            error_count += check_matching_debug_and_release_binaries(
                package_dir, relative_debug_binary_sets, relative_release_binary_sets, portfile_cmake, msg_sink);
        }

        if (windows_target)
        {
            Optional<std::vector<Optional<LibInformation>>> debug_lib_info;
            Optional<std::vector<Optional<LibInformation>>> release_lib_info;

            // Note that this condition is paired with the guarded calls to check_crt_linkage_of_libs below
            if (!policies.is_enabled(BuildPolicy::SKIP_ARCHITECTURE_CHECK) ||
                !policies.is_enabled(BuildPolicy::SKIP_CRT_LINKAGE_CHECK))
            {
                debug_lib_info.emplace(get_lib_info(fs, package_dir, relative_debug_libs));
                release_lib_info.emplace(get_lib_info(fs, package_dir, relative_release_libs));
            }

            std::vector<FileAndArch> binaries_with_invalid_architecture;
            if (!policies.is_enabled(BuildPolicy::SKIP_ARCHITECTURE_CHECK))
            {
                check_lib_architecture(pre_build_info.target_architecture,
                                       relative_debug_libs,
                                       debug_lib_info.value_or_exit(VCPKG_LINE_INFO),
                                       binaries_with_invalid_architecture);
                check_lib_architecture(pre_build_info.target_architecture,
                                       relative_release_libs,
                                       release_lib_info.value_or_exit(VCPKG_LINE_INFO),
                                       binaries_with_invalid_architecture);
            }

            std::vector<PostBuildCheckDllData> dlls_data;
            dlls_data.reserve(relative_debug_dlls.size() + relative_release_dlls.size());
            error_count +=
                perform_post_build_checks_dll_loads(fs, dlls_data, package_dir, relative_debug_dlls, msg_sink);
            error_count +=
                perform_post_build_checks_dll_loads(fs, dlls_data, package_dir, relative_release_dlls, msg_sink);
            if (!policies.is_enabled(BuildPolicy::ALLOW_KERNEL32_FROM_XBOX))
            {
                error_count +=
                    check_bad_kernel32_from_xbox(dlls_data, package_dir, pre_build_info, portfile_cmake, msg_sink);
            }
            if (!policies.is_enabled(BuildPolicy::DLLS_WITHOUT_LIBS))
            {
                if (check_lib_files_are_available_if_dlls_are_available(
                        relative_debug_libs.size(), relative_debug_dlls.size(), portfile_cmake, msg_sink) ==
                    LintStatus::PROBLEM_DETECTED)
                {
                    ++error_count;
                }
                else
                {
                    error_count += check_lib_files_are_available_if_dlls_are_available(
                        relative_release_libs.size(), relative_release_dlls.size(), portfile_cmake, msg_sink);
                }
            }
            if (!policies.is_enabled(BuildPolicy::DLLS_WITHOUT_EXPORTS))
            {
                error_count += check_exports_of_dlls(dlls_data, package_dir, portfile_cmake, msg_sink);
            }
            if (!policies.is_enabled(BuildPolicy::SKIP_APPCONTAINER_CHECK))
            {
                error_count += check_appcontainer_bit_if_uwp(
                    pre_build_info.cmake_system_name, package_dir, portfile_cmake, dlls_data, msg_sink);
            }
            if (!policies.is_enabled(BuildPolicy::ALLOW_OBSOLETE_MSVCRT))
            {
                error_count += check_outdated_crt_linkage_of_dlls(
                    dlls_data, package_dir, pre_build_info, portfile_cmake, msg_sink);
            }
            if (!policies.is_enabled(BuildPolicy::SKIP_ARCHITECTURE_CHECK))
            {
                check_dll_architecture(
                    pre_build_info.target_architecture, dlls_data, binaries_with_invalid_architecture);
            }
            if (!binaries_with_invalid_architecture.empty())
            {
                ++error_count;
                print_invalid_architecture_files(pre_build_info.target_architecture,
                                                 package_dir,
                                                 portfile_cmake,
                                                 binaries_with_invalid_architecture,
                                                 msg_sink);
            }
            if (build_info.library_linkage == LinkageType::Static &&
                !build_info.policies.is_enabled(BuildPolicy::DLLS_IN_STATIC_LIBRARY))
            {
                auto& relative_dlls = relative_debug_dlls;
                Util::Vectors::append(relative_dlls, std::move(relative_release_dlls));
                error_count += check_no_dlls_present(package_dir, relative_dlls, portfile_cmake, msg_sink);
                error_count +=
                    check_bin_folders_are_not_present_in_static_build(fs, package_dir, portfile_cmake, msg_sink);
            }

            // Note that this condition is paired with the possible initialization of `debug_lib_info` above
            if (!policies.is_enabled(BuildPolicy::SKIP_CRT_LINKAGE_CHECK))
            {
                std::map<LinkageAndBuildType, std::vector<FileAndLinkages>> groups_of_invalid_crt;
                check_crt_group_linkage_of_libs(
                    LinkageAndBuildType{build_info.crt_linkage,
                                        build_info.policies.is_enabled(BuildPolicy::ONLY_RELEASE_CRT)},
                    package_dir,
                    relative_debug_libs,
                    debug_lib_info.value_or_exit(VCPKG_LINE_INFO),
                    groups_of_invalid_crt);
                check_crt_group_linkage_of_libs(LinkageAndBuildType{build_info.crt_linkage, true},
                                                package_dir,
                                                relative_release_libs,
                                                release_lib_info.value_or_exit(VCPKG_LINE_INFO),
                                                groups_of_invalid_crt);

                error_count += check_crt_linkage_of_libs(package_dir, portfile_cmake, groups_of_invalid_crt, msg_sink);
            }
        }

        if (!policies.is_enabled(BuildPolicy::ALLOW_EMPTY_FOLDERS))
        {
            error_count += check_no_empty_folders(fs, package_dir, portfile_cmake, msg_sink);
        }
        if (!policies.is_enabled(BuildPolicy::SKIP_MISPLACED_REGULAR_FILES_CHECK))
        {
            static constexpr StringLiteral bad_dirs[] = {"debug", ""};
            error_count += check_no_regular_files_in_relative_path(fs, package_dir, portfile_cmake, bad_dirs, msg_sink);
        }

        std::vector<Path> relative_all_files;
        if (!policies.is_enabled(BuildPolicy::SKIP_PKGCONFIG_CHECK) ||
            !policies.is_enabled(BuildPolicy::SKIP_ABSOLUTE_PATHS_CHECK))
        {
            relative_all_files = fs.get_regular_files_recursive_lexically_proximate(package_dir, IgnoreErrors{});
            Util::sort(relative_all_files);
        }

        if (!policies.is_enabled(BuildPolicy::SKIP_PKGCONFIG_CHECK))
        {
            error_count +=
                check_pkgconfig_dir_only_in_lib_dir(fs, package_dir, relative_all_files, portfile_cmake, msg_sink);
        }
        if (!policies.is_enabled(BuildPolicy::SKIP_ABSOLUTE_PATHS_CHECK))
        {
            Path prohibited_absolute_paths[] = {
                paths.packages(), paths.installed().root(), paths.buildtrees(), paths.downloads};
            error_count += check_no_absolute_paths_in(
                fs, package_dir, prohibited_absolute_paths, relative_all_files, portfile_cmake, msg_sink);
        }

        return error_count;
    }

    static bool should_skip_all_post_build_checks(const BuildPolicies& policies,
                                                  BuildPolicy tested_policy,
                                                  MessageSink& msg_sink)
    {
        if (policies.is_enabled(tested_policy))
        {
            msg_sink.println(LocalizedString::from_raw("-- ").append(
                msgSkippingPostBuildValidationDueTo, msg::cmake_var = to_cmake_variable(tested_policy)));
            return true;
        }

        return false;
    }

    size_t perform_post_build_lint_checks(const InstallPlanAction& action,
                                          const VcpkgPaths& paths,
                                          const PreBuildInfo& pre_build_info,
                                          const BuildInfo& build_info,
                                          MessageSink& msg_sink)
    {
        auto& policies = build_info.policies;
        if (should_skip_all_post_build_checks(policies, BuildPolicy::EMPTY_PACKAGE, msg_sink) ||
            should_skip_all_post_build_checks(policies, BuildPolicy::SKIP_ALL_POST_BUILD_CHECKS, msg_sink))
        {
            return 0;
        }

        msg_sink.println(LocalizedString::from_raw("-- ").append(msgPerformingPostBuildValidation));
        const auto& scfl = action.source_control_file_and_location.value_or_exit(VCPKG_LINE_INFO);
        auto port_dir = scfl.port_directory();
        const auto portfile_cmake = port_dir / FilePortfileDotCMake;
        const size_t error_count = perform_all_checks_and_return_error_count(
            action, paths, pre_build_info, build_info, port_dir, portfile_cmake, msg_sink);
        if (error_count != 0)
        {
            msg_sink.println(Color::warning,
                             LocalizedString::from_raw(portfile_cmake)
                                 .append_raw(": ")
                                 .append_raw(WarningPrefix)
                                 .append(msgFailedPostBuildChecks, msg::count = error_count));
        }

        return error_count;
    }
}
