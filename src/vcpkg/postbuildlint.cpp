#include <vcpkg/base/cofffilereader.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/message_sinks.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/parallel-algorithms.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.process.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.build.h>
#include <vcpkg/installedpaths.h>
#include <vcpkg/packagespec.h>
#include <vcpkg/postbuildlint.h>
#include <vcpkg/vcpkgpaths.h>

namespace vcpkg
{
    constexpr static std::array<StringLiteral, 4> windows_system_names = {
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

    // clang-format off
#define OUTDATED_V_NO_120 \
    StringLiteral{"msvcp100.dll"},          \
    StringLiteral{"msvcp100d.dll"},         \
    StringLiteral{"msvcp110.dll"},          \
    StringLiteral{"msvcp110_win.dll"},      \
    StringLiteral{"msvcp60.dll"},           \
    StringLiteral{"msvcp60.dll"},           \
                               \
    StringLiteral{"msvcrt.dll"},            \
    StringLiteral{"msvcr100.dll"},          \
    StringLiteral{"msvcr100d.dll"},         \
    StringLiteral{"msvcr100_clr0400.dll"},  \
    StringLiteral{"msvcr110.dll"},          \
    StringLiteral{"msvcrt20.dll"},          \
    StringLiteral{"msvcrt40.dll"}

    // clang-format on

    static View<StringLiteral> get_outdated_dynamic_crts(const Optional<std::string>& toolset_version)
    {
        static constexpr std::array<StringLiteral, 13> V_NO_120 = {OUTDATED_V_NO_120};

        static constexpr std::array<StringLiteral, 17> V_NO_MSVCRT = {
            OUTDATED_V_NO_120,
            StringLiteral{"msvcp120.dll"},
            StringLiteral{"msvcp120_clr0400.dll"},
            StringLiteral{"msvcr120.dll"},
            StringLiteral{"msvcr120_clr0400.dll"},
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
                                                           const BuildPolicies& policies,
                                                           const Path& package_dir,
                                                           MessageSink& msg_sink)
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
                msg_sink.println_warning(msgPortBugIncludeDirInCMakeHelperPort);
                return LintStatus::PROBLEM_DETECTED;
            }
            else
            {
                return LintStatus::SUCCESS;
            }
        }

        if (!fs.exists(include_dir, IgnoreErrors{}) || fs.is_empty(include_dir, IgnoreErrors{}))
        {
            msg_sink.println_warning(msgPortBugMissingIncludeDir);
            return LintStatus::PROBLEM_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    static LintStatus check_for_restricted_include_files(const ReadOnlyFilesystem& fs,
                                                         const BuildPolicies& policies,
                                                         const Path& package_dir,
                                                         MessageSink& msg_sink)
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
            "base64.h",
            "Makefile.am",
            "Makefile.in",
            "Makefile",
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
            msg_sink.println_warning(msgPortBugRestrictedHeaderPaths);
            print_paths(msg_sink, violations);
            msg_sink.println(msgPortBugRestrictedHeaderPaths);
            return LintStatus::PROBLEM_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    static LintStatus check_for_files_in_debug_include_directory(const ReadOnlyFilesystem& fs,
                                                                 const Path& package_dir,
                                                                 MessageSink& msg_sink)
    {
        const auto debug_include_dir = package_dir / "debug" / "include";

        std::vector<Path> files_found = fs.get_regular_files_recursive(debug_include_dir, IgnoreErrors{});

        Util::erase_remove_if(files_found, [](const Path& target) { return target.extension() == ".ifc"; });

        if (!files_found.empty())
        {
            msg_sink.println_warning(msgPortBugDuplicateIncludeFiles);
            return LintStatus::PROBLEM_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    static LintStatus check_for_files_in_debug_share_directory(const ReadOnlyFilesystem& fs,
                                                               const Path& package_dir,
                                                               MessageSink& msg_sink)
    {
        const auto debug_share = package_dir / "debug" / "share";
        if (fs.exists(debug_share, IgnoreErrors{}))
        {
            msg_sink.println_warning(msgPortBugDebugShareDir);
            return LintStatus::PROBLEM_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    static LintStatus check_for_vcpkg_port_config(const ReadOnlyFilesystem& fs,
                                                  const BuildPolicies& policies,
                                                  const Path& package_dir,
                                                  const PackageSpec& spec,
                                                  MessageSink& msg_sink)
    {
        const auto relative_path = Path("share") / spec.name() / "vcpkg-port-config.cmake";
        const auto absolute_path = package_dir / relative_path;
        if (policies.is_enabled(BuildPolicy::CMAKE_HELPER_PORT))
        {
            if (!fs.exists(absolute_path, IgnoreErrors{}))
            {
                msg_sink.println_warning(msgPortBugMissingFile, msg::path = relative_path);
                return LintStatus::PROBLEM_DETECTED;
            }
        }

        return LintStatus::SUCCESS;
    }

    static LintStatus check_for_usage_forgot_install(const ReadOnlyFilesystem& fs,
                                                     const Path& port_dir,
                                                     const Path& package_dir,
                                                     const PackageSpec& spec,
                                                     MessageSink& msg_sink)
    {
        static constexpr StringLiteral STANDARD_INSTALL_USAGE =
            R"###(file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"))###";

        auto usage_path_from = port_dir / "usage";
        auto usage_path_to = package_dir / "share" / spec.name() / "usage";

        if (fs.is_regular_file(usage_path_from) && !fs.is_regular_file(usage_path_to))
        {
            msg_sink.println_warning(msg::format(msgPortBugMissingProvidedUsage, msg::package_name = spec.name())
                                         .append_raw('\n')
                                         .append_raw(STANDARD_INSTALL_USAGE));
            return LintStatus::PROBLEM_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    static LintStatus check_folder_lib_cmake(const ReadOnlyFilesystem& fs,
                                             const Path& package_dir,
                                             const PackageSpec& spec,
                                             MessageSink& msg_sink)
    {
        const auto lib_cmake = package_dir / "lib" / "cmake";
        if (fs.exists(lib_cmake, IgnoreErrors{}))
        {
            msg_sink.println_warning(msgPortBugMergeLibCMakeDir, msg::package_name = spec.name());
            return LintStatus::PROBLEM_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    static LintStatus check_for_misplaced_cmake_files(const ReadOnlyFilesystem& fs,
                                                      const Path& package_dir,
                                                      const PackageSpec& spec,
                                                      MessageSink& msg_sink)
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
            msg_sink.println_warning(msgPortBugMisplacedCMakeFiles, msg::spec = spec.name());
            print_paths(msg_sink, misplaced_cmake_files);
            return LintStatus::PROBLEM_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    static LintStatus check_folder_debug_lib_cmake(const ReadOnlyFilesystem& fs,
                                                   const Path& package_dir,
                                                   const PackageSpec& spec,
                                                   MessageSink& msg_sink)
    {
        const auto lib_cmake_debug = package_dir / "debug" / "lib" / "cmake";
        if (fs.exists(lib_cmake_debug, IgnoreErrors{}))
        {
            msg_sink.println_warning(msgPortBugMergeLibCMakeDir, msg::package_name = spec.name());
            return LintStatus::PROBLEM_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    static LintStatus check_for_dlls_in_lib_dir(const ReadOnlyFilesystem& fs,
                                                const Path& package_dir,
                                                MessageSink& msg_sink)
    {
        std::vector<Path> dlls = fs.get_regular_files_recursive(package_dir / "lib", IgnoreErrors{});
        Util::erase_remove_if(dlls, NotExtensionCaseInsensitive{".dll"});

        if (!dlls.empty())
        {
            msg_sink.println_warning(msgPortBugDllInLibDir);
            print_paths(msg_sink, dlls);
            return LintStatus::PROBLEM_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    static LintStatus check_for_copyright_file(const ReadOnlyFilesystem& fs,
                                               const PackageSpec& spec,
                                               const Path& package_dir,
                                               const VcpkgPaths& paths,
                                               MessageSink& msg_sink)
    {
        const auto copyright_file = package_dir / "share" / spec.name() / "copyright";

        switch (fs.status(copyright_file, IgnoreErrors{}))
        {
            case FileType::regular: return LintStatus::SUCCESS; break;
            case FileType::directory: msg_sink.println_warning(msgCopyrightIsDir, msg::path = "copyright"); break;
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

        msg_sink.println_warning(msgPortBugMissingLicense, msg::package_name = spec.name());
        if (potential_copyright_files.size() == 1)
        {
            // if there is only one candidate, provide the cmake lines needed to place it in the proper location
            const auto& found_file = potential_copyright_files[0];
            auto found_relative_native = found_file.native();
            found_relative_native.erase(current_buildtrees_dir.native().size() +
                                        1); // The +1 is needed to remove the "/"
            const Path relative_path = found_relative_native;
            msg_sink.print(Color::none,
                           fmt::format("\n    vcpkg_install_copyright(FILE_LIST \"${{SOURCE_PATH}}/{}/{}\")\n",
                                       relative_path.generic_u8string(),
                                       found_file.filename()));
        }
        else if (potential_copyright_files.size() > 1)
        {
            msg_sink.println_warning(msgPortBugFoundCopyrightFiles);
            print_paths(msg_sink, potential_copyright_files);
        }
        return LintStatus::PROBLEM_DETECTED;
    }

    static LintStatus check_for_exes(const ReadOnlyFilesystem& fs, const Path& package_dir, MessageSink& msg_sink)
    {
        std::vector<Path> exes = fs.get_regular_files_recursive(package_dir / "bin", IgnoreErrors{});
        Util::erase_remove_if(exes, NotExtensionCaseInsensitive{".exe"});

        if (!exes.empty())
        {
            msg_sink.println_warning(msgPortBugFoundExeInBinDir);
            print_paths(msg_sink, exes);
            return LintStatus::PROBLEM_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    struct PostBuildCheckDllData
    {
        Path path;
        MachineType machine_type;
        bool is_arm64_ec;
        bool has_exports;
        bool has_appcontainer;
        std::vector<std::string> dependencies;
    };

    static ExpectedL<PostBuildCheckDllData> try_load_dll_data(const ReadOnlyFilesystem& fs, const Path& path)
    {
        auto maybe_file = fs.try_open_for_read(path);
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

        return PostBuildCheckDllData{path,
                                     metadata->get_machine_type(),
                                     metadata->is_arm64_ec(),
                                     has_exports,
                                     has_appcontainer,
                                     std::move(*dependencies)};
    }

    static LintStatus check_exports_of_dlls(const BuildPolicies& policies,
                                            const std::vector<PostBuildCheckDllData>& dlls_data,
                                            MessageSink& msg_sink)
    {
        if (policies.is_enabled(BuildPolicy::DLLS_WITHOUT_EXPORTS)) return LintStatus::SUCCESS;

        std::vector<Path> dlls_with_no_exports;
        for (const PostBuildCheckDllData& dll_data : dlls_data)
        {
            if (!dll_data.has_exports)
            {
                dlls_with_no_exports.push_back(dll_data.path);
            }
        }

        if (!dlls_with_no_exports.empty())
        {
            msg_sink.println_warning(msgPortBugSetDllsWithoutExports);
            print_paths(msg_sink, dlls_with_no_exports);
            return LintStatus::PROBLEM_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    static LintStatus check_uwp_bit_of_dlls(StringView expected_system_name,
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
                dlls_with_improper_uwp_bit.push_back(dll_data.path);
            }
        }

        if (!dlls_with_improper_uwp_bit.empty())
        {
            msg_sink.println_warning(msgPortBugDllAppContainerBitNotSet);
            print_paths(msg_sink, dlls_with_improper_uwp_bit);
            return LintStatus::PROBLEM_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    struct FileAndArch
    {
        Path file;
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
            default: return fmt::format("unknown-{}", static_cast<uint16_t>(machine_type));
        }
    }

    static void print_invalid_architecture_files(const std::string& expected_architecture,
                                                 std::vector<FileAndArch> binaries_with_invalid_architecture,
                                                 MessageSink& msg_sink)
    {
        msg_sink.println_warning(msgBuiltWithIncorrectArchitecture);
        for (const FileAndArch& b : binaries_with_invalid_architecture)
        {
            msg_sink.println_warning(LocalizedString().append_indent().append(msgBinaryWithInvalidArchitecture,
                                                                              msg::path = b.file,
                                                                              msg::expected = expected_architecture,
                                                                              msg::actual = b.actual_arch));
        }
    }

    static LintStatus check_dll_architecture(const std::string& expected_architecture,
                                             const std::vector<PostBuildCheckDllData>& dlls_data,
                                             MessageSink& msg_sink)
    {
        std::vector<FileAndArch> binaries_with_invalid_architecture;

        for (const PostBuildCheckDllData& dll_data : dlls_data)
        {
            const std::string actual_architecture = get_printable_architecture(dll_data.machine_type);
            if (expected_architecture == "arm64ec")
            {
                if (dll_data.machine_type != MachineType::AMD64 || !dll_data.is_arm64_ec)
                {
                    binaries_with_invalid_architecture.push_back({dll_data.path, actual_architecture});
                }
            }
            else if (expected_architecture != actual_architecture)
            {
                binaries_with_invalid_architecture.push_back({dll_data.path, actual_architecture});
            }
        }

        if (!binaries_with_invalid_architecture.empty())
        {
            print_invalid_architecture_files(expected_architecture, binaries_with_invalid_architecture, msg_sink);
            return LintStatus::PROBLEM_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    static std::vector<Optional<LibInformation>> get_lib_info(const Filesystem& fs, View<Path> libs)
    {
        std::vector<Optional<LibInformation>> maybe_lib_infos(libs.size());
        std::transform(
            libs.begin(), libs.end(), maybe_lib_infos.begin(), [&fs](const Path& lib) -> Optional<LibInformation> {
                auto maybe_rfp = fs.try_open_for_read(lib);

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
    static LintStatus check_lib_architecture(const std::string& expected_architecture,
                                             View<Path> libs,
                                             View<Optional<LibInformation>> lib_infos,
                                             MessageSink& msg_sink)
    {
        std::vector<FileAndArch> binaries_with_invalid_architecture;
        for (size_t i = 0; i < libs.size(); ++i)
        {
            auto& maybe_lib_information = lib_infos[i];
            auto& lib = libs[i];

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
                binaries_with_invalid_architecture.push_back({lib, Strings::join(",", printable_machine_types)});
            }
        }

        if (!binaries_with_invalid_architecture.empty())
        {
            print_invalid_architecture_files(expected_architecture, binaries_with_invalid_architecture, msg_sink);
            return LintStatus::PROBLEM_DETECTED;
        }
        return LintStatus::SUCCESS;
    }

    static LintStatus check_no_dlls_present(const std::vector<Path>& dlls, MessageSink& msg_sink)
    {
        if (dlls.empty())
        {
            return LintStatus::SUCCESS;
        }
        msg_sink.println_warning(msgPortBugFoundDllInStaticBuild);
        print_paths(msg_sink, dlls);
        return LintStatus::PROBLEM_DETECTED;
    }

    static LintStatus check_matching_debug_and_release_binaries(const std::vector<Path>& debug_binaries,
                                                                const std::vector<Path>& release_binaries,
                                                                MessageSink& msg_sink)
    {
        const size_t debug_count = debug_binaries.size();
        const size_t release_count = release_binaries.size();
        if (debug_count == release_count)
        {
            return LintStatus::SUCCESS;
        }

        msg_sink.println_warning(msgPortBugMismatchedNumberOfBinaries);
        if (debug_count == 0)
        {
            msg_sink.println(msgPortBugMissingDebugBinaries);
        }
        else
        {
            msg_sink.println(msgPortBugFoundDebugBinaries, msg::count = debug_count);
            print_paths(msg_sink, debug_binaries);
        }

        if (release_count == 0)
        {
            msg_sink.println(msgPortBugMissingReleaseBinaries);
        }
        else
        {
            msg_sink.println(msgPortBugFoundReleaseBinaries, msg::count = release_count);
            print_paths(msg_sink, release_binaries);
        }

        return LintStatus::PROBLEM_DETECTED;
    }

    static LintStatus check_lib_files_are_available_if_dlls_are_available(const BuildPolicies& policies,
                                                                          const size_t lib_count,
                                                                          const size_t dll_count,
                                                                          const Path& lib_dir,
                                                                          MessageSink& msg_sink)
    {
        if (policies.is_enabled(BuildPolicy::DLLS_WITHOUT_LIBS)) return LintStatus::SUCCESS;

        if (lib_count == 0 && dll_count != 0)
        {
            msg_sink.println_warning(msgPortBugMissingImportedLibs, msg::path = lib_dir);
            return LintStatus::PROBLEM_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    static LintStatus check_bin_folders_are_not_present_in_static_build(const ReadOnlyFilesystem& fs,
                                                                        const Path& package_dir,
                                                                        MessageSink& msg_sink)
    {
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
            msg_sink.println_warning(msgPortBugBinDirExists, msg::path = bin);
        }

        if (debug_bin_exists)
        {
            msg_sink.println_warning(msgPortBugDebugBinDirExists, msg::path = debug_bin);
        }

        msg_sink.println_warning(msgPortBugRemoveBinDir);
        msg_sink.print(
            Color::warning,
            R"###(    if(VCPKG_LIBRARY_LINKAGE STREQUAL "static"))###"
            "\n"
            R"###(        file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/bin" "${CURRENT_PACKAGES_DIR}/debug/bin"))###"
            "\n"
            R"###(    endif())###"
            "\n\n");

        return LintStatus::PROBLEM_DETECTED;
    }

    static LintStatus check_no_empty_folders(const ReadOnlyFilesystem& fs, const Path& dir, MessageSink& msg_sink)
    {
        std::vector<Path> empty_directories = fs.get_directories_recursive(dir, IgnoreErrors{});
        Util::erase_remove_if(empty_directories,
                              [&fs](const Path& current) { return !fs.is_empty(current, IgnoreErrors{}); });

        if (!empty_directories.empty())
        {
            msg_sink.println_warning(msgPortBugFoundEmptyDirectories, msg::path = dir);
            print_paths(msg_sink, empty_directories);

            std::string dirs = "    file(REMOVE_RECURSE";
            for (auto&& empty_dir : empty_directories)
            {
                Strings::append(dirs,
                                " \"${CURRENT_PACKAGES_DIR}",
                                empty_dir.generic_u8string().substr(dir.generic_u8string().size()),
                                '"');
            }
            dirs += ")\n";
            msg_sink.println_warning(msg::format(msgPortBugRemoveEmptyDirectories).append_raw('\n').append_raw(dirs));

            return LintStatus::PROBLEM_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    static LintStatus check_pkgconfig_dir_only_in_lib_dir(const ReadOnlyFilesystem& fs,
                                                          const Path& dir_raw,
                                                          MessageSink& msg_sink)
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

        const auto share_dir = Path(dir) / "share" / "pkgconfig";
        const auto lib_dir = Path(dir) / "lib" / "pkgconfig";
        const auto debug_dir = Path(dir) / "debug" / "lib" / "pkgconfig";
        for (Path& path : fs.get_regular_files_recursive(dir, IgnoreErrors{}))
        {
            if (!Strings::ends_with(path, ".pc")) continue;
            const auto parent_path = Path(path.parent_path());
            // Always allow .pc files at 'lib/pkgconfig' and 'debug/lib/pkgconfig'
            if (parent_path == lib_dir || parent_path == debug_dir) continue;

            const bool contains_libs =
                Util::any_of(fs.read_lines(path).value_or_exit(VCPKG_LINE_INFO), [](const std::string& line) {
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
                misplaced_pkgconfig_files.push_back({std::move(path), MisplacedFile::Type::Share});
                continue;
            }

            const bool is_debug = Strings::starts_with(path, Path(dir) / "debug");
            if (is_debug)
            {
                misplaced_pkgconfig_files.push_back({std::move(path), MisplacedFile::Type::Debug});
                contains_debug = true;
            }
            else
            {
                misplaced_pkgconfig_files.push_back({std::move(path), MisplacedFile::Type::Release});
                contains_release = true;
            }
        }

        if (!misplaced_pkgconfig_files.empty())
        {
            msg_sink.println_warning(msgPortBugMisplacedPkgConfigFiles);
            for (const auto& item : misplaced_pkgconfig_files)
            {
                msg_sink.print(Color::warning, fmt::format("    {}\n", item.path));
            }
            msg_sink.println(Color::warning, msgPortBugMovePkgConfigFiles);

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

            msg_sink.print(Color::warning, create_directory_line);

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
                msg_sink.print(Color::warning, rename_line);
            }

            msg_sink.print(Color::warning, "    vcpkg_fixup_pkgconfig()\n    ");
            msg_sink.println(Color::warning, msgPortBugRemoveEmptyDirs);

            return LintStatus::PROBLEM_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    struct BuildTypeAndFile
    {
        Path file;
        bool has_static_release = false;
        bool has_static_debug = false;
        bool has_dynamic_release = false;
        bool has_dynamic_debug = false;
    };

    static LocalizedString format_linkage(LinkageType linkage, bool release)
    {
        switch (linkage)
        {
            case LinkageType::Dynamic:
                if (release)
                {
                    return msg::format(msgLinkageDynamicRelease);
                }
                else
                {
                    return msg::format(msgLinkageDynamicDebug);
                }
                break;
            case LinkageType::Static:
                if (release)
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

    // lib_infos[n] is the lib info for libs[n] for all n in [0, libs.size())
    static LintStatus check_crt_linkage_of_libs(const BuildInfo& build_info,
                                                bool expect_release,
                                                const std::vector<Path>& libs,
                                                View<Optional<LibInformation>> lib_infos,
                                                MessageSink& msg_sink)
    {
        std::vector<BuildTypeAndFile> libs_with_invalid_crt;
        for (size_t i = 0; i < libs.size(); ++i)
        {
            auto& maybe_lib_info = lib_infos[i];
            auto& lib = libs[i];

            if (!maybe_lib_info.has_value())
            {
                continue;
            }

            auto&& lib_info = maybe_lib_info.value_or_exit(VCPKG_LINE_INFO);
            Debug::println(
                "The lib ", lib.native(), " has directives: ", Strings::join(" ", lib_info.linker_directives));

            BuildTypeAndFile this_lib{lib};
            constexpr static StringLiteral static_release_crt = "/DEFAULTLIB:LIBCMT";
            constexpr static StringLiteral static_debug_crt = "/DEFAULTLIB:LIBCMTd";
            constexpr static StringLiteral dynamic_release_crt = "/DEFAULTLIB:MSVCRT";
            constexpr static StringLiteral dynamic_debug_crt = "/DEFAULTLIB:MSVCRTd";

            for (auto&& directive : lib_info.linker_directives)
            {
                if (Strings::case_insensitive_ascii_equals(directive, static_release_crt))
                {
                    this_lib.has_static_release = true;
                }
                else if (Strings::case_insensitive_ascii_equals(directive, static_debug_crt))
                {
                    this_lib.has_static_debug = true;
                }
                else if (Strings::case_insensitive_ascii_equals(directive, dynamic_release_crt))
                {
                    this_lib.has_dynamic_release = true;
                }
                else if (Strings::case_insensitive_ascii_equals(directive, dynamic_debug_crt))
                {
                    this_lib.has_dynamic_debug = true;
                }
            }

            bool fail = false;
            if (expect_release)
            {
                fail |= this_lib.has_static_debug;
                fail |= this_lib.has_dynamic_debug;
            }
            else
            {
                fail |= this_lib.has_static_release;
                fail |= this_lib.has_dynamic_release;
            }

            switch (build_info.crt_linkage)
            {
                case LinkageType::Dynamic:
                    fail |= this_lib.has_static_debug;
                    fail |= this_lib.has_static_release;
                    break;
                case LinkageType::Static:
                    fail |= this_lib.has_dynamic_debug;
                    fail |= this_lib.has_dynamic_release;
                    break;
                default: Checks::unreachable(VCPKG_LINE_INFO);
            }

            if (fail)
            {
                libs_with_invalid_crt.push_back(std::move(this_lib));
            }
        }

        if (!libs_with_invalid_crt.empty())
        {
            msg_sink.println_warning(msgPortBugInvalidCrtLinkage,
                                     msg::expected = format_linkage(build_info.crt_linkage, expect_release));
            std::vector<LocalizedString> printed_linkages;
            for (const BuildTypeAndFile& btf : libs_with_invalid_crt)
            {
                printed_linkages.clear();
                LocalizedString this_entry;
                this_entry.append_indent().append(msgPortBugInvalidCrtLinkageEntry, msg::path = btf.file);
                if (btf.has_dynamic_debug)
                {
                    printed_linkages.push_back(msg::format(msgLinkageDynamicDebug));
                }

                if (btf.has_dynamic_release)
                {
                    printed_linkages.push_back(msg::format(msgLinkageDynamicRelease));
                }

                if (btf.has_static_debug)
                {
                    printed_linkages.push_back(msg::format(msgLinkageStaticDebug));
                }

                if (btf.has_static_release)
                {
                    printed_linkages.push_back(msg::format(msgLinkageStaticRelease));
                }

                this_entry.append_floating_list(2, printed_linkages);
                msg_sink.println(this_entry);
            }

            msg_sink.println(msg::format(msgPortBugInspectFiles, msg::extension = "lib")
                                 .append_raw("\n  dumpbin.exe /directives mylibfile.lib"));
            return LintStatus::PROBLEM_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    struct OutdatedDynamicCrtAndFile
    {
        Path file;
        StringLiteral outdated_crt;
    };

    static LintStatus check_outdated_crt_linkage_of_dlls(const std::vector<PostBuildCheckDllData>& dlls_data,
                                                         const BuildInfo& build_info,
                                                         const PreBuildInfo& pre_build_info,
                                                         MessageSink& msg_sink)
    {
        if (build_info.policies.is_enabled(BuildPolicy::ALLOW_OBSOLETE_MSVCRT)) return LintStatus::SUCCESS;

        const auto outdated_crts = get_outdated_dynamic_crts(pre_build_info.platform_toolset);
        std::vector<OutdatedDynamicCrtAndFile> dlls_with_outdated_crt;

        for (const PostBuildCheckDllData& dll_data : dlls_data)
        {
            for (const StringLiteral& outdated_crt : outdated_crts)
            {
                if (Util::Vectors::contains(
                        dll_data.dependencies, outdated_crt, Strings::case_insensitive_ascii_equals))
                {
                    dlls_with_outdated_crt.push_back({dll_data.path, outdated_crt});
                    break;
                }
            }
        }

        if (!dlls_with_outdated_crt.empty())
        {
            msg_sink.println_warning(msgPortBugOutdatedCRT);
            for (const OutdatedDynamicCrtAndFile& btf : dlls_with_outdated_crt)
            {
                msg_sink.print(Color::warning, fmt::format("  {}:{}\n", btf.file, btf.outdated_crt));
            }
            msg_sink.println(msg::format(msgPortBugInspectFiles, msg::extension = "dll")
                                 .append_raw("\n  dumpbin.exe /dependents mylibfile.dll"));
            return LintStatus::PROBLEM_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    static LintStatus check_bad_kernel32_from_xbox(const std::vector<PostBuildCheckDllData>& dlls_data,
                                                   const PreBuildInfo& pre_build_info,
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
                Debug::println("Dependency: ", dependency);
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

        msg_sink.println(msgPortBugKernel32FromXbox);
        for (auto&& bad_dll : bad_dlls)
        {
            msg_sink.println(LocalizedString{}.append_indent().append_raw(bad_dll->path));
        }

        msg_sink.println(msg::format(msgPortBugInspectFiles, msg::extension = "dll")
                             .append_raw("\n  dumpbin.exe /dependents mylibfile.dll"));
        return LintStatus::PROBLEM_DETECTED;
    }

    static LintStatus check_no_files_in_dir(const ReadOnlyFilesystem& fs, const Path& dir, MessageSink& msg_sink)
    {
        std::vector<Path> misplaced_files = fs.get_regular_files_non_recursive(dir, IgnoreErrors{});
        Util::erase_remove_if(misplaced_files, [](const Path& target) {
            const auto filename = target.filename();
            return filename == "CONTROL" || filename == "BUILD_INFO" || filename == ".DS_Store";
        });

        if (!misplaced_files.empty())
        {
            msg_sink.println_warning(msg::format(msgPortBugMisplacedFiles, msg::path = dir).append_raw('\n'));
            print_paths(msg_sink, misplaced_files);
            msg_sink.println_warning(msgPortBugMisplacedFilesCont);
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
            extension == ".conf")
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
                                                 const Path& dir,
                                                 Span<Path> absolute_paths,
                                                 MessageSink& msg_sink)
    {
        std::vector<std::string> string_paths;
        for (const auto& path : absolute_paths)
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
        {
            std::mutex mtx;
            auto files = fs.get_regular_files_recursive(dir, IgnoreErrors{});

            parallel_for_each(files, [&](const Path& file) {
                if (file_contains_absolute_paths(fs, file, searcher_paths))
                {
                    std::lock_guard lock{mtx};
                    failing_files.push_back(file);
                }
            });
        } // destroy mtx

        if (failing_files.empty())
        {
            return LintStatus::SUCCESS;
        }

        Util::sort(failing_files);
        auto error_message = msg::format(msgFilesContainAbsolutePath1);
        for (auto&& absolute_path : absolute_paths)
        {
            error_message.append_raw('\n').append_indent().append_raw(absolute_path);
        }

        error_message.append_raw('\n').append(msgFilesContainAbsolutePath2);
        for (auto&& failure : failing_files)
        {
            error_message.append_raw('\n').append_indent().append_raw(failure);
        }

        msg_sink.println_warning(error_message);
        return LintStatus::PROBLEM_DETECTED;
    }

    static void operator+=(size_t& left, const LintStatus& right) { left += static_cast<size_t>(right); }

    static size_t perform_post_build_checks_dll_loads(const ReadOnlyFilesystem& fs,
                                                      std::vector<PostBuildCheckDllData>& dlls_data,
                                                      const std::vector<Path>& dll_files,
                                                      MessageSink& msg_sink)
    {
        size_t error_count = 0;
        for (const Path& dll : dll_files)
        {
            auto maybe_dll_data = try_load_dll_data(fs, dll);
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

    static size_t perform_all_checks_and_return_error_count(const PackageSpec& spec,
                                                            const VcpkgPaths& paths,
                                                            const PreBuildInfo& pre_build_info,
                                                            const BuildInfo& build_info,
                                                            const Path& port_dir,
                                                            MessageSink& msg_sink)
    {
        const auto& fs = paths.get_filesystem();
        const auto package_dir = paths.package_dir(spec);

        size_t error_count = 0;

        if (build_info.policies.is_enabled(BuildPolicy::EMPTY_PACKAGE))
        {
            return error_count;
        }

        error_count += check_for_files_in_include_directory(fs, build_info.policies, package_dir, msg_sink);
        error_count += check_for_restricted_include_files(fs, build_info.policies, package_dir, msg_sink);
        error_count += check_for_files_in_debug_include_directory(fs, package_dir, msg_sink);
        error_count += check_for_files_in_debug_share_directory(fs, package_dir, msg_sink);
        error_count += check_for_vcpkg_port_config(fs, build_info.policies, package_dir, spec, msg_sink);
        error_count += check_folder_lib_cmake(fs, package_dir, spec, msg_sink);
        error_count += check_for_misplaced_cmake_files(fs, package_dir, spec, msg_sink);
        error_count += check_folder_debug_lib_cmake(fs, package_dir, spec, msg_sink);
        error_count += check_for_dlls_in_lib_dir(fs, package_dir, msg_sink);
        error_count += check_for_dlls_in_lib_dir(fs, package_dir / "debug", msg_sink);
        error_count += check_for_copyright_file(fs, spec, package_dir, paths, msg_sink);
        error_count += check_for_exes(fs, package_dir, msg_sink);
        error_count += check_for_exes(fs, package_dir / "debug", msg_sink);
        error_count += check_for_usage_forgot_install(fs, port_dir, package_dir, spec, msg_sink);

        const auto debug_lib_dir = package_dir / "debug" / "lib";
        const auto release_lib_dir = package_dir / "lib";
        const auto debug_bin_dir = package_dir / "debug" / "bin";
        const auto release_bin_dir = package_dir / "bin";

        NotExtensionsCaseInsensitive lib_filter;
        const bool windows_target = Util::Vectors::contains(windows_system_names, pre_build_info.cmake_system_name);
        if (windows_target)
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
        {
            error_count += check_matching_debug_and_release_binaries(debug_libs, release_libs, msg_sink);
        }

        if (windows_target)
        {
            Debug::println("Running windows targeting post-build checks");

            auto release_lib_info = get_lib_info(fs, release_libs);
            Optional<std::vector<Optional<LibInformation>>> debug_lib_info;

            // Note that this condition is paired with the debug check_crt_linkage_of_libs below
            if (!build_info.policies.is_enabled(BuildPolicy::SKIP_ARCHITECTURE_CHECK) ||
                !build_info.policies.is_enabled(BuildPolicy::ONLY_RELEASE_CRT))
            {
                debug_lib_info.emplace(get_lib_info(fs, debug_libs));
            }

            if (!build_info.policies.is_enabled(BuildPolicy::SKIP_ARCHITECTURE_CHECK))
            {
                error_count += check_lib_architecture(
                    pre_build_info.target_architecture, debug_libs, *debug_lib_info.get(), msg_sink);
                error_count += check_lib_architecture(
                    pre_build_info.target_architecture, release_libs, release_lib_info, msg_sink);
            }

            std::vector<Path> debug_dlls = fs.get_regular_files_recursive(debug_bin_dir, IgnoreErrors{});
            Util::erase_remove_if(debug_dlls, NotExtensionCaseInsensitive{".dll"});
            std::vector<Path> release_dlls = fs.get_regular_files_recursive(release_bin_dir, IgnoreErrors{});
            Util::erase_remove_if(release_dlls, NotExtensionCaseInsensitive{".dll"});

            std::vector<PostBuildCheckDllData> dlls_data;
            dlls_data.reserve(debug_dlls.size() + release_dlls.size());
            error_count += perform_post_build_checks_dll_loads(fs, dlls_data, debug_dlls, msg_sink);
            error_count += perform_post_build_checks_dll_loads(fs, dlls_data, release_dlls, msg_sink);
            error_count += check_bad_kernel32_from_xbox(dlls_data, pre_build_info, msg_sink);

            if (!pre_build_info.build_type &&
                !build_info.policies.is_enabled(BuildPolicy::MISMATCHED_NUMBER_OF_BINARIES))
                error_count += check_matching_debug_and_release_binaries(debug_dlls, release_dlls, msg_sink);

            error_count += check_lib_files_are_available_if_dlls_are_available(
                build_info.policies, debug_libs.size(), debug_dlls.size(), debug_lib_dir, msg_sink);
            error_count += check_lib_files_are_available_if_dlls_are_available(
                build_info.policies, release_libs.size(), release_dlls.size(), release_lib_dir, msg_sink);

            error_count += check_exports_of_dlls(build_info.policies, dlls_data, msg_sink);
            error_count += check_uwp_bit_of_dlls(pre_build_info.cmake_system_name, dlls_data, msg_sink);
            error_count += check_outdated_crt_linkage_of_dlls(dlls_data, build_info, pre_build_info, msg_sink);
            if (!build_info.policies.is_enabled(BuildPolicy::SKIP_ARCHITECTURE_CHECK))
            {
                error_count += check_dll_architecture(pre_build_info.target_architecture, dlls_data, msg_sink);
            }

            if (build_info.library_linkage == LinkageType::Static &&
                !build_info.policies.is_enabled(BuildPolicy::DLLS_IN_STATIC_LIBRARY))
            {
                auto& dlls = debug_dlls;
                dlls.insert(dlls.end(),
                            std::make_move_iterator(release_dlls.begin()),
                            std::make_move_iterator(release_dlls.end()));
                error_count += check_no_dlls_present(dlls, msg_sink);
                error_count += check_bin_folders_are_not_present_in_static_build(fs, package_dir, msg_sink);
            }

            // Note that this condition is paired with the possible initialization of `debug_lib_info` above
            if (!build_info.policies.is_enabled(BuildPolicy::ONLY_RELEASE_CRT))
            {
                error_count += check_crt_linkage_of_libs(
                    build_info, false, debug_libs, debug_lib_info.value_or_exit(VCPKG_LINE_INFO), msg_sink);
            }

            error_count += check_crt_linkage_of_libs(build_info, true, release_libs, release_lib_info, msg_sink);
        }

        error_count += check_no_empty_folders(fs, package_dir, msg_sink);
        error_count += check_no_files_in_dir(fs, package_dir, msg_sink);
        error_count += check_no_files_in_dir(fs, package_dir / "debug", msg_sink);
        error_count += check_pkgconfig_dir_only_in_lib_dir(fs, package_dir, msg_sink);
        if (!build_info.policies.is_enabled(BuildPolicy::SKIP_ABSOLUTE_PATHS_CHECK))
        {
            Path tests[] = {package_dir, paths.installed().root(), paths.build_dir(spec), paths.downloads};
            error_count += check_no_absolute_paths_in(fs, package_dir, tests, msg_sink);
        }

        return error_count;
    }

    size_t perform_post_build_lint_checks(const PackageSpec& spec,
                                          const VcpkgPaths& paths,
                                          const PreBuildInfo& pre_build_info,
                                          const BuildInfo& build_info,
                                          const Path& port_dir,
                                          MessageSink& msg_sink)
    {
        msg_sink.println(msgPerformingPostBuildValidation);
        const size_t error_count =
            perform_all_checks_and_return_error_count(spec, paths, pre_build_info, build_info, port_dir, msg_sink);

        if (error_count != 0)
        {
            const auto portfile = port_dir / "portfile.cmake";
            msg_sink.println_error(msgFailedPostBuildChecks, msg::count = error_count, msg::path = portfile);
        }
        return error_count;
    }
}
