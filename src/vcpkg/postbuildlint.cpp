#include <vcpkg/base/cofffilereader.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/system.print.h>
#include <vcpkg/base/system.process.h>
#include <vcpkg/base/util.h>

#include <vcpkg/build.h>
#include <vcpkg/packagespec.h>
#include <vcpkg/postbuildlint.buildtype.h>
#include <vcpkg/postbuildlint.h>
#include <vcpkg/vcpkgpaths.h>

namespace vcpkg::PostBuildLint
{
    constexpr static const StringLiteral windows_system_names[] = {
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

    struct OutdatedDynamicCrt
    {
        std::string name;
    };

    static Span<const OutdatedDynamicCrt> get_outdated_dynamic_crts(const Optional<std::string>& toolset_version)
    {
        static const std::vector<OutdatedDynamicCrt> V_NO_120 = {
            {"msvcp100.dll"},
            {"msvcp100d.dll"},
            {"msvcp110.dll"},
            {"msvcp110_win.dll"},
            {"msvcp60.dll"},
            {"msvcp60.dll"},

            {"msvcrt.dll"},
            {"msvcr100.dll"},
            {"msvcr100d.dll"},
            {"msvcr100_clr0400.dll"},
            {"msvcr110.dll"},
            {"msvcrt20.dll"},
            {"msvcrt40.dll"},
        };

        static const std::vector<OutdatedDynamicCrt> V_NO_MSVCRT = [&]() {
            auto ret = V_NO_120;
            ret.push_back({"msvcp120.dll"});
            ret.push_back({"msvcp120_clr0400.dll"});
            ret.push_back({"msvcr120.dll"});
            ret.push_back({"msvcr120_clr0400.dll"});
            return ret;
        }();

        const auto tsv = toolset_version.get();
        if (tsv && (*tsv) == "v120")
        {
            return V_NO_120;
        }

        // Default case for all version >= VS 2015.
        return V_NO_MSVCRT;
    }

    static LintStatus check_for_files_in_include_directory(const Filesystem& fs,
                                                           const BuildPolicies& policies,
                                                           const Path& package_dir)
    {
        if (policies.is_enabled(BuildPolicy::EMPTY_INCLUDE_FOLDER))
        {
            return LintStatus::SUCCESS;
        }

        const auto include_dir = package_dir / "include";

        if (policies.is_enabled(BuildPolicy::CMAKE_HELPER_PORT))
        {
            if (fs.exists(include_dir, IgnoreErrors{}))
            {
                msg::println_warning(msgPortBugIncludeDirInCMakeHelperPort);
                return LintStatus::PROBLEM_DETECTED;
            }
            else
            {
                return LintStatus::SUCCESS;
            }
        }

        if (!fs.exists(include_dir, IgnoreErrors{}) || fs.is_empty(include_dir, IgnoreErrors{}))
        {
            msg::println_warning(msgPortBugMissingIncludeDir);
            return LintStatus::PROBLEM_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    static LintStatus check_for_restricted_include_files(const Filesystem& fs,
                                                         const BuildPolicies& policies,
                                                         const Path& package_dir)
    {
        if (policies.is_enabled(BuildPolicy::ALLOW_RESTRICTED_HEADERS))
        {
            return LintStatus::SUCCESS;
        }

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

        std::vector<Path> violations;
        for (auto&& flist : restricted_lists)
            for (auto&& f : flist)
            {
                if (Util::Sets::contains(filenames_s, f))
                {
                    violations.push_back(Path("include") / f);
                }
            }

        if (!violations.empty())
        {
            msg::println_warning(msgPortBugRestrictedHeaderPaths,
                                 msg::env_var = to_cmake_variable(BuildPolicy::ALLOW_RESTRICTED_HEADERS));
            print_paths(violations);
            msg::println(msgPortBugRestrictedHeaderPaths,
                         msg::env_var = to_cmake_variable(BuildPolicy::ALLOW_RESTRICTED_HEADERS));
            return LintStatus::PROBLEM_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    static LintStatus check_for_files_in_debug_include_directory(const Filesystem& fs, const Path& package_dir)
    {
        const auto debug_include_dir = package_dir / "debug" / "include";

        std::vector<Path> files_found = fs.get_regular_files_recursive(debug_include_dir, IgnoreErrors{});

        Util::erase_remove_if(files_found, [](const Path& target) { return target.extension() == ".ifc"; });

        if (!files_found.empty())
        {
            msg::println_warning(msgPortBugDuplicateIncludeFiles);
            return LintStatus::PROBLEM_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    static LintStatus check_for_files_in_debug_share_directory(const Filesystem& fs, const Path& package_dir)
    {
        const auto debug_share = package_dir / "debug" / "share";
        if (fs.exists(debug_share, IgnoreErrors{}))
        {
            msg::println_warning(msgPortBugDebugShareDir);
            return LintStatus::PROBLEM_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    static LintStatus check_for_vcpkg_port_config(const Filesystem& fs,
                                                  const BuildPolicies& policies,
                                                  const Path& package_dir,
                                                  const PackageSpec& spec)
    {
        const auto relative_path = Path("share") / spec.name() / "vcpkg-port-config.cmake";
        const auto absolute_path = package_dir / relative_path;
        if (policies.is_enabled(BuildPolicy::CMAKE_HELPER_PORT))
        {
            if (!fs.exists(absolute_path, IgnoreErrors{}))
            {
                msg::println_warning(msgPortBugMissingFile, msg::path = relative_path);
                return LintStatus::PROBLEM_DETECTED;
            }
        }

        return LintStatus::SUCCESS;
    }

    static LintStatus check_folder_lib_cmake(const Filesystem& fs, const Path& package_dir, const PackageSpec& spec)
    {
        const auto lib_cmake = package_dir / "lib" / "cmake";
        if (fs.exists(lib_cmake, IgnoreErrors{}))
        {
            msg::println_warning(msgPortBugMergeLibCMakeDir, msg::spec = spec.name());
            return LintStatus::PROBLEM_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    static LintStatus check_for_misplaced_cmake_files(const Filesystem& fs,
                                                      const Path& package_dir,
                                                      const PackageSpec& spec)
    {
        std::vector<Path> dirs = {
            package_dir / "cmake",
            package_dir / "debug" / "cmake",
            package_dir / "lib" / "cmake",
            package_dir / "debug" / "lib" / "cmake",
        };

        std::vector<Path> misplaced_cmake_files;
        for (auto&& dir : dirs)
        {
            for (auto&& file : fs.get_regular_files_recursive(dir, IgnoreErrors{}))
            {
                if (Strings::case_insensitive_ascii_equals(file.extension(), ".cmake"))
                {
                    misplaced_cmake_files.push_back(std::move(file));
                }
            }
        }

        if (!misplaced_cmake_files.empty())
        {
            msg::println_warning(msgPortBugMisplacedCMakeFiles, msg::spec = spec.name());
            print_paths(misplaced_cmake_files);
            return LintStatus::PROBLEM_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    static LintStatus check_folder_debug_lib_cmake(const Filesystem& fs,
                                                   const Path& package_dir,
                                                   const PackageSpec& spec)
    {
        const auto lib_cmake_debug = package_dir / "debug" / "lib" / "cmake";
        if (fs.exists(lib_cmake_debug, IgnoreErrors{}))
        {
            msg::println_warning(msgPortBugMergeLibCMakeDir, msg::spec = spec.name());
            return LintStatus::PROBLEM_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    static LintStatus check_for_dlls_in_lib_dir(const Filesystem& fs, const Path& package_dir)
    {
        std::vector<Path> dlls = fs.get_regular_files_recursive(package_dir / "lib", IgnoreErrors{});
        Util::erase_remove_if(dlls, NotExtensionCaseInsensitive{".dll"});

        if (!dlls.empty())
        {
            msg::println_warning(msgPortBugDllInLibDir);
            print_paths(dlls);
            return LintStatus::PROBLEM_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    static LintStatus check_for_copyright_file(const Filesystem& fs, const PackageSpec& spec, const VcpkgPaths& paths)
    {
        const auto packages_dir = paths.packages() / spec.dir();
        const auto copyright_file = packages_dir / "share" / spec.name() / "copyright";

        switch (fs.status(copyright_file, IgnoreErrors{}))
        {
            case FileType::regular: return LintStatus::SUCCESS; break;
            case FileType::directory: msg::println_warning(msgCopyrightIsDir, msg::path = "copyright"); break;
            default: break;
        }

        const auto current_buildtrees_dir = paths.build_dir(spec);
        const auto current_buildtrees_dir_src = current_buildtrees_dir / "src";

        std::vector<Path> potential_copyright_files;
        // We only search in the root of each unpacked source archive to reduce false positives
        auto src_dirs = fs.get_directories_non_recursive(current_buildtrees_dir_src, IgnoreErrors{});
        for (auto&& src_dir : src_dirs)
        {
            for (auto&& src_file : fs.get_regular_files_non_recursive(src_dir, IgnoreErrors{}))
            {
                const auto filename = src_file.filename();
                if (Strings::case_insensitive_ascii_equals(filename, "LICENSE") ||
                    Strings::case_insensitive_ascii_equals(filename, "LICENSE.txt") ||
                    Strings::case_insensitive_ascii_equals(filename, "COPYING"))
                {
                    potential_copyright_files.push_back(src_file);
                }
            }
        }

        msg::println_warning(msgPortBugMissingLicense, msg::spec = spec.name());
        if (potential_copyright_files.size() == 1)
        {
            // if there is only one candidate, provide the cmake lines needed to place it in the proper location
            const auto found_file = potential_copyright_files[0];
            auto found_relative_native = found_file.native();
            found_relative_native.erase(current_buildtrees_dir.native().size() +
                                        1); // The +1 is needed to remove the "/"
            const Path relative_path = found_relative_native;
            msg::write_unlocalized_text_to_stdout(
                Color::none,
                fmt::format("\n    configure_file(\"${CURRENT_BUILDTREES_DIR}/{}/{}\" "
                            "\"${CURRENT_PACKAGES_DIR}/share/{}/copyright\" COPYONLY)\n",
                            relative_path.generic_u8string(),
                            found_file.filename(),
                            spec.name()));
        }
        else if (potential_copyright_files.size() > 1)
        {
            msg::println_warning(msgPortBugFoundCopyrightFiles);
            print_paths(potential_copyright_files);
        }
        return LintStatus::PROBLEM_DETECTED;
    }

    static LintStatus check_for_exes(const Filesystem& fs, const Path& package_dir)
    {
        std::vector<Path> exes = fs.get_regular_files_recursive(package_dir / "bin", IgnoreErrors{});
        Util::erase_remove_if(exes, NotExtensionCaseInsensitive{".exe"});

        if (!exes.empty())
        {
            msg::println_warning(msgPortBugFoundExeInBinDir);
            print_paths(exes);
            return LintStatus::PROBLEM_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    static LintStatus check_exports_of_dlls(const BuildPolicies& policies,
                                            const std::vector<Path>& dlls,
                                            const Path& dumpbin_exe)
    {
        if (policies.is_enabled(BuildPolicy::DLLS_WITHOUT_EXPORTS)) return LintStatus::SUCCESS;

        std::vector<Path> dlls_with_no_exports;
        for (const Path& dll : dlls)
        {
            auto cmd_line = Command(dumpbin_exe).string_arg("/exports").string_arg(dll);
            const auto maybe_output = flatten_out(cmd_execute_and_capture_output(cmd_line), "dumpbin");
            if (const auto output = maybe_output.get())
            {
                if (output->find("ordinal hint RVA      name") == std::string::npos)
                {
                    dlls_with_no_exports.push_back(dll);
                }
            }
            else
            {
                Checks::msg_exit_with_message(VCPKG_LINE_INFO,
                                              msg::format(msgCommandFailed, msg::command_line = cmd_line.command_line())
                                                  .append_raw('\n')
                                                  .append(maybe_output.error()));
            }
        }

        if (!dlls_with_no_exports.empty())
        {
            msg::println_warning(msgPortBugSetDllsWithoutExports);
            print_paths(dlls_with_no_exports);
            return LintStatus::PROBLEM_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    static LintStatus check_uwp_bit_of_dlls(const std::string& expected_system_name,
                                            const std::vector<Path>& dlls,
                                            const Path dumpbin_exe)
    {
        if (expected_system_name != "WindowsStore")
        {
            return LintStatus::SUCCESS;
        }

        std::vector<Path> dlls_with_improper_uwp_bit;
        for (const Path& dll : dlls)
        {
            auto cmd_line = Command(dumpbin_exe).string_arg("/headers").string_arg(dll);
            const auto maybe_output = flatten_out(cmd_execute_and_capture_output(cmd_line), "dumpbin");
            if (const auto output = maybe_output.get())
            {
                if (output->find("App Container") == std::string::npos)
                {
                    dlls_with_improper_uwp_bit.push_back(dll);
                }
            }
            else
            {
                Checks::msg_exit_with_message(VCPKG_LINE_INFO,
                                              msg::format(msgCommandFailed, msg::command_line = cmd_line.command_line())
                                                  .append_raw('\n')
                                                  .append(maybe_output.error()));
            }
        }

        if (!dlls_with_improper_uwp_bit.empty())
        {
            msg::println_warning(msgPortBugDllAppContainerBitNotSet);
            print_paths(dlls_with_improper_uwp_bit);
            msg::println_warning(msgPortBugDllAppContainerBitNotSet);
            return LintStatus::PROBLEM_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    struct FileAndArch
    {
        Path file;
        std::string actual_arch;
    };

    static std::string get_actual_architecture(const MachineType& machine_type)
    {
        switch (machine_type)
        {
            case MachineType::AMD64:
            case MachineType::IA64: return "x64";
            case MachineType::I386: return "x86";
            case MachineType::ARM:
            case MachineType::ARMNT: return "arm";
            case MachineType::ARM64: return "arm64";
            case MachineType::ARM64EC: return "arm64ec";
            default: return "Machine Type Code = " + std::to_string(static_cast<uint16_t>(machine_type));
        }
    }

    static void print_invalid_architecture_files(const std::string& expected_architecture,
                                                 std::vector<FileAndArch> binaries_with_invalid_architecture)
    {
        msg::println_warning(msgBuiltWithIncorrectArchitecture);
        for (const FileAndArch& b : binaries_with_invalid_architecture)
        {
            msg::println_warning(LocalizedString().append_indent().append(msgBinaryWithInvalidArchitecture,
                                                                          msg::path = b.file,
                                                                          msg::expected = expected_architecture,
                                                                          msg::actual = b.actual_arch));
        }
    }

#if defined(_WIN32)
    static LintStatus check_dll_architecture(const std::string& expected_architecture,
                                             const std::vector<Path>& files,
                                             const Filesystem& fs)
    {
        std::vector<FileAndArch> binaries_with_invalid_architecture;

        for (const Path& file : files)
        {
            if (!Strings::case_insensitive_ascii_equals(file.extension(), ".dll"))
            {
                continue;
            }

            auto maybe_opened = fs.try_open_for_read(file);
            if (const auto opened = maybe_opened.get())
            {
                auto maybe_metadata = try_read_dll_metadata(*opened);
                if (auto* metadata = maybe_metadata.get())
                {
                    const auto machine_type = metadata->get_machine_type();
                    const std::string actual_architecture = get_actual_architecture(machine_type);
                    if (expected_architecture == "arm64ec")
                    {
                        if (actual_architecture != "x64" || !metadata->is_arm64_ec())
                        {
                            binaries_with_invalid_architecture.push_back({file, actual_architecture});
                        }
                    }
                    else if (expected_architecture != actual_architecture)
                    {
                        binaries_with_invalid_architecture.push_back({file, actual_architecture});
                    }
                }
            }
        }

        if (!binaries_with_invalid_architecture.empty())
        {
            print_invalid_architecture_files(expected_architecture, binaries_with_invalid_architecture);
            return LintStatus::PROBLEM_DETECTED;
        }

        return LintStatus::SUCCESS;
    }
#endif

    static LintStatus check_lib_architecture(const std::string& expected_architecture,
                                             const std::string& cmake_system_name,
                                             const std::vector<Path>& files,
                                             const Filesystem& fs)
    {
        std::vector<FileAndArch> binaries_with_invalid_architecture;
        if (Util::Vectors::contains(windows_system_names, cmake_system_name))
        {
            for (const Path& file : files)
            {
                Checks::msg_check_exit(VCPKG_LINE_INFO,
                                       Strings::case_insensitive_ascii_equals(file.extension(), ".lib"),
                                       msgExpectedExtension,
                                       msg::extension = ".lib",
                                       msg::path = file);

                const auto machine_types = Util::fmap(read_lib_machine_types(fs.open_for_read(file, VCPKG_LINE_INFO)),
                                                      [](MachineType mt) { return get_actual_architecture(mt); });
                // Either machine_types is empty (meaning this lib is architecture independent), or
                // we need at least one of the machine types to match.
                // Agnostic example: Folly's debug library
                // Multiple example: arm64x libraries
                if (!machine_types.empty() && !Util::Vectors::contains(machine_types, expected_architecture))
                {
                    binaries_with_invalid_architecture.push_back({file, Strings::join(",", machine_types)});
                }
            }
        }
        else if (cmake_system_name == "Darwin")
        {
            const auto requested_arch = expected_architecture == "x64" ? "x86_64" : expected_architecture;
            for (const Path& file : files)
            {
                auto cmd_line = Command("lipo").string_arg("-archs").string_arg(file);
                auto maybe_output = flatten_out(cmd_execute_and_capture_output(cmd_line), "lipo");
                if (const auto output = maybe_output.get())
                {
                    if (!Util::Vectors::contains(Strings::split(Strings::trim(*output), ' '), requested_arch))
                    {
                        binaries_with_invalid_architecture.push_back({file, std::move(*output)});
                    }
                }
                else
                {
                    msg::println_error(msg::format(msgFailedToDetermineArchitecture,
                                                   msg::path = file,
                                                   msg::command_line = cmd_line.command_line())
                                           .append_raw('\n')
                                           .append(maybe_output.error()));
                }
            }
        }

        if (!binaries_with_invalid_architecture.empty())
        {
            print_invalid_architecture_files(expected_architecture, binaries_with_invalid_architecture);
            return LintStatus::PROBLEM_DETECTED;
        }
        return LintStatus::SUCCESS;
    }

    static LintStatus check_no_dlls_present(const BuildPolicies& policies, const std::vector<Path>& dlls)
    {
        if (dlls.empty() || policies.is_enabled(BuildPolicy::DLLS_IN_STATIC_LIBRARY))
        {
            return LintStatus::SUCCESS;
        }
        msg::println_warning(msgPortBugFoundDllInStaticBuild);
        print_paths(dlls);
        return LintStatus::PROBLEM_DETECTED;
    }

    static LintStatus check_matching_debug_and_release_binaries(const std::vector<Path>& debug_binaries,
                                                                const std::vector<Path>& release_binaries)
    {
        const size_t debug_count = debug_binaries.size();
        const size_t release_count = release_binaries.size();
        if (debug_count == release_count)
        {
            return LintStatus::SUCCESS;
        }

        msg::println_warning(msgPortBugMismatchedNumberOfBinaries);
        msg::println_warning(msgPortBugFoundDebugBinaries, msg::count = debug_count);
        print_paths(debug_binaries);
        msg::println_warning(msgPortBugFoundDebugBinaries, msg::count = release_count);
        print_paths(release_binaries);

        if (debug_count == 0)
        {
            msg::println_warning(msgPortBugMissingDebugBinaries);
        }
        if (release_count == 0)
        {
            msg::println_warning(msgPortBugMissingReleaseBinaries);
        }
        return LintStatus::PROBLEM_DETECTED;
    }

    static LintStatus check_lib_files_are_available_if_dlls_are_available(const BuildPolicies& policies,
                                                                          const size_t lib_count,
                                                                          const size_t dll_count,
                                                                          const Path& lib_dir)
    {
        if (policies.is_enabled(BuildPolicy::DLLS_WITHOUT_LIBS)) return LintStatus::SUCCESS;

        if (lib_count == 0 && dll_count != 0)
        {
            msg::println_warning(msgPortBugMissingImportedLibs, msg::path = lib_dir);
            return LintStatus::PROBLEM_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    static LintStatus check_bin_folders_are_not_present_in_static_build(const BuildPolicies& policies,
                                                                        const Filesystem& fs,
                                                                        const Path& package_dir)
    {
        if (policies.is_enabled(BuildPolicy::DLLS_IN_STATIC_LIBRARY)) return LintStatus::SUCCESS;

        const auto bin = package_dir / "bin";
        const auto debug_bin = package_dir / "debug" / "bin";

        const bool bin_exists = fs.exists(bin, IgnoreErrors{});
        const bool debug_bin_exists = fs.exists(debug_bin, IgnoreErrors{});
        if (!bin_exists && !debug_bin_exists)
        {
            return LintStatus::SUCCESS;
        }

        if (bin_exists)
        {
            msg::println_warning(msgPortBugBinDirExists, msg::path = bin);
        }

        if (debug_bin_exists)
        {
            msg::println_warning(msgPortBugDebugBinDirExists, msg::path = debug_bin);
        }

        msg::println_warning(msgPortBugRemoveBinDir);
        msg::write_unlocalized_text_to_stdout(
            Color::warning,
            R"###(    if(VCPKG_LIBRARY_LINKAGE STREQUAL "static"))###"
            "\n"
            R"###(        file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/bin" "${CURRENT_PACKAGES_DIR}/debug/bin"))###"
            "\n"
            R"###(    endif())###"
            "\n\n");

        return LintStatus::PROBLEM_DETECTED;
    }

    static LintStatus check_no_empty_folders(const Filesystem& fs, const Path& dir)
    {
        std::vector<Path> empty_directories = fs.get_directories_recursive(dir, IgnoreErrors{});
        Util::erase_remove_if(empty_directories,
                              [&fs](const Path& current) { return !fs.is_empty(current, IgnoreErrors{}); });

        if (!empty_directories.empty())
        {
            msg::println_warning(msgPortBugFoundEmptyDirectories, msg::path = dir);
            print_paths(empty_directories);

            std::string dirs = "    file(REMOVE_RECURSE";
            for (auto&& empty_dir : empty_directories)
            {
                Strings::append(dirs,
                                " \"${CURRENT_PACKAGES_DIR}",
                                empty_dir.generic_u8string().substr(dir.generic_u8string().size()),
                                '"');
            }
            dirs += ")\n";
            msg::println_warning(msg::format(msgPortBugRemoveEmptyDirectories).append_raw('\n').append_raw(dirs));

            return LintStatus::PROBLEM_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    static LintStatus check_pkgconfig_dir_only_in_lib_dir(const Filesystem& fs, const Path& dir_raw)
    {
        struct MisplacedFile
        {
            Path path;
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

        auto dir = dir_raw.lexically_normal().generic_u8string(); // force /s
        for (Path& path : fs.get_regular_files_recursive(dir, IgnoreErrors{}))
        {
            if (!Strings::ends_with(path, ".pc")) continue;
            // Always forbid .pc files not in a "pkgconfig" directory:
            const auto parent_path = Path(path.parent_path());
            if (parent_path.filename() != "pkgconfig") continue;
            const auto pkgconfig_parent_name = Path(parent_path.parent_path()).filename().to_string();
            // Always allow .pc files in "lib/pkgconfig":
            if (pkgconfig_parent_name == "lib") continue;
            // Allow .pc in "share/pkgconfig" if and only if it contains no "Libs:" or "Libs.private:" directives:
            const bool contains_libs = Util::any_of(fs.read_lines(path, VCPKG_LINE_INFO), [](const std::string& line) {
                if (Strings::starts_with(line, "Libs"))
                {
                    // only consider "Libs:" or "Libs.private:" directives when they have a value
                    const auto colon = line.find_first_of(':');
                    if (colon != std::string::npos && line.find_first_not_of(' ', colon + 1) != std::string::npos)
                        return true;
                }
                return false;
            });
            if (pkgconfig_parent_name == "share" && !contains_libs) continue;
            if (!contains_libs)
            {
                contains_share = true;
                misplaced_pkgconfig_files.push_back({std::move(path), MisplacedFile::Type::Share});
                continue;
            }
            const bool is_release = path.native().find("debug/") == std::string::npos;
            misplaced_pkgconfig_files.push_back(
                {std::move(path), is_release ? MisplacedFile::Type::Release : MisplacedFile::Type::Debug});
            if (is_release)
                contains_release = true;
            else
                contains_debug = true;
        }

        if (!misplaced_pkgconfig_files.empty())
        {
            msg::println_warning(msgPortBugMisplacedPkgConfigFiles);
            for (const auto& item : misplaced_pkgconfig_files)
            {
                msg::write_unlocalized_text_to_stdout(Color::warning, fmt::format("    {}\n", item.path));
            }
            msg::println_warning(msgPortBugMovePkgConfigFiles);

            std::string create_directory_line("    file(MAKE_DIRECTORY");
            if (contains_release)
            {
                create_directory_line += R"###( "${CURRENT_PACKAGES_DIR}/lib/pkgconfig")###";
            }

            if (contains_debug)
            {
                create_directory_line += R"###( "${CURRENT_PACKAGES_DIR}/lib/debug/pkgconfig")###";
            }

            if (contains_share)
            {
                create_directory_line += R"###( "${CURRENT_PACKAGES_DIR}/share/pkgconfig")###";
            }

            create_directory_line.append(")\n");

            msg::write_unlocalized_text_to_stdout(Color::warning, create_directory_line);

            for (const auto& item : misplaced_pkgconfig_files)
            {
                auto relative = item.path.native().substr(dir.size());
                std::string rename_line(R"###(    file(RENAME "${CURRENT_PACKAGES_DIR})###");
                rename_line.append(relative);
                rename_line.append(R"###(" "${CURRENT_PACKAGES_DIR}/)###");
                switch (item.type)
                {
                    case MisplacedFile::Type::Debug: rename_line.append("debug/lib/pkgconfig/"); break;
                    case MisplacedFile::Type::Release: rename_line.append("lib/pkgconfig/"); break;
                    case MisplacedFile::Type::Share: rename_line.append("share/pkgconfig/"); break;
                }
                rename_line.append(item.path.filename().to_string());
                rename_line.append("\")\n");
                msg::write_unlocalized_text_to_stdout(Color::warning, rename_line);
            }

            msg::write_unlocalized_text_to_stdout(Color::warning, "    vcpkg_fixup_pkgconfig()\nfile(REMOVE_RECURSE ");
            msg::println_warning(msgPortBugRemoveEmptyDirs);

            return LintStatus::PROBLEM_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    struct BuildTypeAndFile
    {
        Path file;
        BuildType build_type;
    };

    static LintStatus check_crt_linkage_of_libs(const BuildType& expected_build_type,
                                                const std::vector<Path>& libs,
                                                const Path& dumpbin_exe)
    {
        auto bad_build_types = std::vector<BuildType>({
            {ConfigurationType::DEBUG, LinkageType::STATIC},
            {ConfigurationType::DEBUG, LinkageType::DYNAMIC},
            {ConfigurationType::RELEASE, LinkageType::STATIC},
            {ConfigurationType::RELEASE, LinkageType::DYNAMIC},
        });
        Util::erase_remove(bad_build_types, expected_build_type);

        std::vector<BuildTypeAndFile> libs_with_invalid_crt;

        for (const Path& lib : libs)
        {
            auto cmd_line = Command(dumpbin_exe).string_arg("/directives").string_arg(lib);
            const auto maybe_output = flatten_out(cmd_execute_and_capture_output(cmd_line), "dumpbin");
            if (const auto output = maybe_output.get())
            {
                for (const BuildType& bad_build_type : bad_build_types)
                {
                    if (bad_build_type.has_crt_linker_option(*output))
                    {
                        libs_with_invalid_crt.push_back({lib, bad_build_type});
                        break;
                    }
                }
            }
            else
            {
                Checks::msg_exit_with_message(VCPKG_LINE_INFO,
                                              msg::format(msgCommandFailed, msg::command_line = cmd_line.command_line())
                                                  .append_raw('\n')
                                                  .append_raw(maybe_output.error()));
            }
        }

        if (!libs_with_invalid_crt.empty())
        {
            msg::println_warning(msgPortBugInvalidCrtLinkage, msg::expected = expected_build_type.to_string());

            for (const BuildTypeAndFile& btf : libs_with_invalid_crt)
            {
                msg::write_unlocalized_text_to_stdout(
                    Color::warning, fmt::format("    {}: {}\n", btf.file, btf.build_type.to_string()));
            }

            msg::println_warning(msg::format(msgPortBugInspectFiles, msg::extension = "lib")
                                     .append_raw("\n    dumpbin.exe /directives mylibfile.lib"));
            return LintStatus::PROBLEM_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    struct OutdatedDynamicCrtAndFile
    {
        Path file;
        OutdatedDynamicCrt outdated_crt;
    };

    static LintStatus check_outdated_crt_linkage_of_dlls(const std::vector<Path>& dlls,
                                                         const Path& dumpbin_exe,
                                                         const BuildInfo& build_info,
                                                         const PreBuildInfo& pre_build_info)
    {
        if (build_info.policies.is_enabled(BuildPolicy::ALLOW_OBSOLETE_MSVCRT)) return LintStatus::SUCCESS;

        std::vector<OutdatedDynamicCrtAndFile> dlls_with_outdated_crt;

        for (const Path& dll : dlls)
        {
            auto cmd_line = Command(dumpbin_exe).string_arg("/dependents").string_arg(dll);
            const auto maybe_output = flatten_out(cmd_execute_and_capture_output(cmd_line), "dumpbin");
            if (const auto output = maybe_output.get())
            {
                for (const OutdatedDynamicCrt& outdated_crt :
                     get_outdated_dynamic_crts(pre_build_info.platform_toolset))
                {
                    if (Strings::case_insensitive_ascii_contains(*output, outdated_crt.name))
                    {
                        dlls_with_outdated_crt.push_back({dll, outdated_crt});
                        break;
                    }
                }
            }
            else
            {
                Checks::msg_exit_with_message(VCPKG_LINE_INFO,
                                              msg::format(msgCommandFailed, msg::command_line = cmd_line.command_line())
                                                  .append_raw('\n')
                                                  .append_raw(maybe_output.error()));
            }
        }

        if (!dlls_with_outdated_crt.empty())
        {
            msg::println_warning(msgPortBugOutdatedCRT);
            for (const OutdatedDynamicCrtAndFile& btf : dlls_with_outdated_crt)
            {
                msg::write_unlocalized_text_to_stdout(Color::warning,
                                                      fmt::format("    {}:{}\n", btf.file, btf.outdated_crt.name));
            }
            msg::println_warning(msg::format(msgPortBugInspectFiles, msg::extension = "dll")
                                     .append_raw("\n    dumpbin.exe /dependents mylibfile.dll"));
            return LintStatus::PROBLEM_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    static LintStatus check_no_files_in_dir(const Filesystem& fs, const Path& dir)
    {
        std::vector<Path> misplaced_files = fs.get_regular_files_non_recursive(dir, IgnoreErrors{});
        Util::erase_remove_if(misplaced_files, [](const Path& target) {
            const auto filename = target.filename();
            return filename == "CONTROL" || filename == "BUILD_INFO" || filename == ".DS_Store";
        });

        if (!misplaced_files.empty())
        {
            msg::println_warning(msg::format(msgPortBugMisplacedFiles, msg::path = dir).append_raw('\n'));
            print_paths(misplaced_files);
            msg::println_warning(msgPortBugMisplacedFilesCont);
            return LintStatus::PROBLEM_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    static void operator+=(size_t& left, const LintStatus& right) { left += static_cast<size_t>(right); }

    static size_t perform_all_checks_and_return_error_count(const PackageSpec& spec,
                                                            const VcpkgPaths& paths,
                                                            const PreBuildInfo& pre_build_info,
                                                            const BuildInfo& build_info)
    {
        const auto& fs = paths.get_filesystem();

        // for dumpbin
        const Toolset& toolset = paths.get_toolset(pre_build_info);
        const auto package_dir = paths.package_dir(spec);

        size_t error_count = 0;

        if (build_info.policies.is_enabled(BuildPolicy::EMPTY_PACKAGE))
        {
            return error_count;
        }

        error_count += check_for_files_in_include_directory(fs, build_info.policies, package_dir);
        error_count += check_for_restricted_include_files(fs, build_info.policies, package_dir);
        error_count += check_for_files_in_debug_include_directory(fs, package_dir);
        error_count += check_for_files_in_debug_share_directory(fs, package_dir);
        error_count += check_for_vcpkg_port_config(fs, build_info.policies, package_dir, spec);
        error_count += check_folder_lib_cmake(fs, package_dir, spec);
        error_count += check_for_misplaced_cmake_files(fs, package_dir, spec);
        error_count += check_folder_debug_lib_cmake(fs, package_dir, spec);
        error_count += check_for_dlls_in_lib_dir(fs, package_dir);
        error_count += check_for_dlls_in_lib_dir(fs, package_dir / "debug");
        error_count += check_for_copyright_file(fs, spec, paths);
        error_count += check_for_exes(fs, package_dir);
        error_count += check_for_exes(fs, package_dir / "debug");

        const auto debug_lib_dir = package_dir / "debug" / "lib";
        const auto release_lib_dir = package_dir / "lib";
        const auto debug_bin_dir = package_dir / "debug" / "bin";
        const auto release_bin_dir = package_dir / "bin";

        NotExtensionsCaseInsensitive lib_filter;
        if (Util::Vectors::contains(windows_system_names, pre_build_info.cmake_system_name))
        {
            lib_filter = NotExtensionsCaseInsensitive{{".lib"}};
        }
        else
        {
            lib_filter = NotExtensionsCaseInsensitive{{".so", ".a", ".dylib"}};
        }

        std::vector<Path> debug_libs = fs.get_regular_files_recursive(debug_lib_dir, IgnoreErrors{});
        Util::erase_remove_if(debug_libs, lib_filter);
        std::vector<Path> release_libs = fs.get_regular_files_recursive(release_lib_dir, IgnoreErrors{});
        Util::erase_remove_if(release_libs, lib_filter);

        if (!pre_build_info.build_type && !build_info.policies.is_enabled(BuildPolicy::MISMATCHED_NUMBER_OF_BINARIES))
            error_count += check_matching_debug_and_release_binaries(debug_libs, release_libs);

        if (!build_info.policies.is_enabled(BuildPolicy::SKIP_ARCHITECTURE_CHECK))
        {
            std::vector<Path> libs;
            libs.insert(libs.cend(), debug_libs.cbegin(), debug_libs.cend());
            libs.insert(libs.cend(), release_libs.cbegin(), release_libs.cend());
            error_count +=
                check_lib_architecture(pre_build_info.target_architecture, pre_build_info.cmake_system_name, libs, fs);
        }

        std::vector<Path> debug_dlls = fs.get_regular_files_recursive(debug_bin_dir, IgnoreErrors{});
        Util::erase_remove_if(debug_dlls, NotExtensionCaseInsensitive{".dll"});
        std::vector<Path> release_dlls = fs.get_regular_files_recursive(release_bin_dir, IgnoreErrors{});
        Util::erase_remove_if(release_dlls, NotExtensionCaseInsensitive{".dll"});

        switch (build_info.library_linkage)
        {
            case LinkageType::DYNAMIC:
            {
                if (!pre_build_info.build_type &&
                    !build_info.policies.is_enabled(BuildPolicy::MISMATCHED_NUMBER_OF_BINARIES))
                    error_count += check_matching_debug_and_release_binaries(debug_dlls, release_dlls);

                error_count += check_lib_files_are_available_if_dlls_are_available(
                    build_info.policies, debug_libs.size(), debug_dlls.size(), debug_lib_dir);
                error_count += check_lib_files_are_available_if_dlls_are_available(
                    build_info.policies, release_libs.size(), release_dlls.size(), release_lib_dir);

                std::vector<Path> dlls;
                dlls.insert(dlls.cend(), debug_dlls.cbegin(), debug_dlls.cend());
                dlls.insert(dlls.cend(), release_dlls.cbegin(), release_dlls.cend());

                if (!toolset.dumpbin.empty() && !build_info.policies.is_enabled(BuildPolicy::SKIP_DUMPBIN_CHECKS))
                {
                    error_count += check_exports_of_dlls(build_info.policies, dlls, toolset.dumpbin);
                    error_count += check_uwp_bit_of_dlls(pre_build_info.cmake_system_name, dlls, toolset.dumpbin);
                    error_count +=
                        check_outdated_crt_linkage_of_dlls(dlls, toolset.dumpbin, build_info, pre_build_info);
                }

#if defined(_WIN32)
                error_count += check_dll_architecture(pre_build_info.target_architecture, dlls, fs);
#endif
                break;
            }
            case LinkageType::STATIC:
            {
                auto dlls = release_dlls;
                dlls.insert(dlls.end(), debug_dlls.begin(), debug_dlls.end());
                error_count += check_no_dlls_present(build_info.policies, dlls);

                error_count += check_bin_folders_are_not_present_in_static_build(build_info.policies, fs, package_dir);

                if (!toolset.dumpbin.empty() && !build_info.policies.is_enabled(BuildPolicy::SKIP_DUMPBIN_CHECKS))
                {
                    if (!build_info.policies.is_enabled(BuildPolicy::ONLY_RELEASE_CRT))
                    {
                        error_count += check_crt_linkage_of_libs(
                            BuildType(ConfigurationType::DEBUG, build_info.crt_linkage), debug_libs, toolset.dumpbin);
                    }
                    error_count += check_crt_linkage_of_libs(
                        BuildType(ConfigurationType::RELEASE, build_info.crt_linkage), release_libs, toolset.dumpbin);
                }
                break;
            }
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }

        error_count += check_no_empty_folders(fs, package_dir);
        error_count += check_no_files_in_dir(fs, package_dir);
        error_count += check_no_files_in_dir(fs, package_dir / "debug");
        error_count += check_pkgconfig_dir_only_in_lib_dir(fs, package_dir);

        return error_count;
    }

    size_t perform_all_checks(const PackageSpec& spec,
                              const VcpkgPaths& paths,
                              const PreBuildInfo& pre_build_info,
                              const BuildInfo& build_info,
                              const Path& port_dir)
    {
        msg::println(msgPerformingPostBuildValidation);
        const size_t error_count = perform_all_checks_and_return_error_count(spec, paths, pre_build_info, build_info);

        if (error_count != 0)
        {
            const auto portfile = port_dir / "portfile.cmake";
            msg::println_error(msgFailedPostBuildChecks, msg::count = error_count, msg::path = portfile);
        }
        return error_count;
    }
}
