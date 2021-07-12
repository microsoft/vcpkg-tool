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

using vcpkg::Build::BuildInfo;
using vcpkg::Build::BuildPolicy;
using vcpkg::Build::PreBuildInfo;

namespace vcpkg::PostBuildLint
{
    static auto not_extension_pred(const Filesystem& fs, const std::string& ext)
    {
        return [&fs, ext](const path& target) { return fs.is_directory(target) || target.extension() != ext; };
    }

    enum class LintStatus
    {
        SUCCESS = 0,
        ERROR_DETECTED = 1
    };

    struct OutdatedDynamicCrt
    {
        std::string name;
        std::regex regex;

        OutdatedDynamicCrt(const std::string& name, const std::string& regex_as_string)
            : name(name), regex(std::regex(regex_as_string, std::regex_constants::icase))
        {
        }
    };

    static Span<const OutdatedDynamicCrt> get_outdated_dynamic_crts(const Optional<std::string>& toolset_version)
    {
        static const std::vector<OutdatedDynamicCrt> V_NO_120 = {
            {"msvcp100.dll", R"(msvcp100\.dll)"},
            {"msvcp100d.dll", R"(msvcp100d\.dll)"},
            {"msvcp110.dll", R"(msvcp110\.dll)"},
            {"msvcp110_win.dll", R"(msvcp110_win\.dll)"},
            {"msvcp60.dll", R"(msvcp60\.dll)"},
            {"msvcp60.dll", R"(msvcp60\.dll)"},

            {"msvcrt.dll", R"(msvcrt\.dll)"},
            {"msvcr100.dll", R"(msvcr100\.dll)"},
            {"msvcr100d.dll", R"(msvcr100d\.dll)"},
            {"msvcr100_clr0400.dll", R"(msvcr100_clr0400\.dll)"},
            {"msvcr110.dll", R"(msvcr110\.dll)"},
            {"msvcrt20.dll", R"(msvcrt20\.dll)"},
            {"msvcrt40.dll", R"(msvcrt40\.dll)"},
        };

        static const std::vector<OutdatedDynamicCrt> V_NO_MSVCRT = [&]() {
            auto ret = V_NO_120;
            ret.push_back({"msvcp120.dll", R"(msvcp120\.dll)"});
            ret.push_back({"msvcp120_clr0400.dll", R"(msvcp120_clr0400\.dll)"});
            ret.push_back({"msvcr120.dll", R"(msvcr120\.dll)"});
            ret.push_back({"msvcr120_clr0400.dll", R"(msvcr120_clr0400\.dll)"});
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
                                                           const Build::BuildPolicies& policies,
                                                           const path& package_dir)
    {
        if (policies.is_enabled(BuildPolicy::EMPTY_INCLUDE_FOLDER))
        {
            return LintStatus::SUCCESS;
        }

        const path include_dir = package_dir / "include";

        if (policies.is_enabled(BuildPolicy::CMAKE_HELPER_PORT))
        {
            if (fs.exists(include_dir))
            {
                print2(Color::warning,
                       "The folder /include exists in a cmake helper port; this is incorrect, since only cmake "
                       "files should be installed\n");
                return LintStatus::ERROR_DETECTED;
            }
            else
            {
                return LintStatus::SUCCESS;
            }
        }

        if (!fs.exists(include_dir) || fs.is_empty(include_dir))
        {
            print2(Color::warning,
                   "The folder /include is empty or not present. This indicates the library was not correctly "
                   "installed.\n");
            return LintStatus::ERROR_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    static LintStatus check_for_restricted_include_files(const Filesystem& fs,
                                                         const Build::BuildPolicies& policies,
                                                         const path& package_dir)
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
        const path include_dir = package_dir / "include";
        auto files = fs.get_files_non_recursive(include_dir);
        auto filenames_v = Util::fmap(files, [](const auto& file) { return vcpkg::u8string(file.filename()); });
        std::set<std::string> filenames_s(filenames_v.begin(), filenames_v.end());

        std::vector<path> violations;
        for (auto&& flist : restricted_lists)
            for (auto&& f : flist)
            {
                if (Util::Sets::contains(filenames_s, f))
                {
                    violations.push_back(vcpkg::u8path("include") / vcpkg::u8path(f.c_str()));
                }
            }

        if (!violations.empty())
        {
            print2(Color::warning,
                   "Restricted headers paths are present. These files can prevent the core C++ runtime and "
                   "other packages from compiling correctly:\n");
            print_paths(violations);
            print2("In exceptional circumstances, this policy can be disabled via ",
                   Build::to_cmake_variable(BuildPolicy::ALLOW_RESTRICTED_HEADERS),
                   "\n");
            return LintStatus::ERROR_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    static LintStatus check_for_files_in_debug_include_directory(const Filesystem& fs, const path& package_dir)
    {
        const path debug_include_dir = package_dir / "debug" / "include";

        std::vector<path> files_found = fs.get_files_recursive(debug_include_dir);

        Util::erase_remove_if(
            files_found, [&fs](const path& target) { return fs.is_directory(target) || target.extension() == ".ifc"; });

        if (!files_found.empty())
        {
            print2(Color::warning,
                   "Include files should not be duplicated into the /debug/include directory. If this cannot "
                   "be disabled in the project cmake, use\n"
                   "    file(REMOVE_RECURSE \"${CURRENT_PACKAGES_DIR}/debug/include\")\n");
            return LintStatus::ERROR_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    static LintStatus check_for_files_in_debug_share_directory(const Filesystem& fs, const path& package_dir)
    {
        const path debug_share = package_dir / "debug" / "share";

        if (fs.exists(debug_share))
        {
            print2(Color::warning,
                   "/debug/share should not exist. Please reorganize any important files, then use\n"
                   "    file(REMOVE_RECURSE \"${CURRENT_PACKAGES_DIR}/debug/share\")\n");
            return LintStatus::ERROR_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    static LintStatus check_for_vcpkg_port_config(const Filesystem& fs,
                                                  const Build::BuildPolicies& policies,
                                                  const path& package_dir,
                                                  const PackageSpec& spec)
    {
        const path relative_path =
            vcpkg::u8path("share") / vcpkg::u8path(spec.name()) / vcpkg::u8path("vcpkg-port-config.cmake");
        const path absolute_path = package_dir / relative_path;

        if (policies.is_enabled(BuildPolicy::CMAKE_HELPER_PORT))
        {
            if (!fs.exists(absolute_path))
            {
                print2(Color::warning,
                       "The /",
                       vcpkg::u8string(relative_path),
                       " file does not exist. This file must exist for CMake helper ports.\n");
                return LintStatus::ERROR_DETECTED;
            }
        }

        return LintStatus::SUCCESS;
    }

    static LintStatus check_folder_lib_cmake(const Filesystem& fs, const path& package_dir, const PackageSpec& spec)
    {
        const path lib_cmake = package_dir / "lib" / "cmake";
        if (fs.exists(lib_cmake))
        {
            vcpkg::printf(Color::warning,
                          "The /lib/cmake folder should be merged with /debug/lib/cmake and moved to "
                          "/share/%s/cmake.\nPlease use the helper function `vcpkg_fixup_cmake_targets()`\n",
                          spec.name());
            return LintStatus::ERROR_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    static LintStatus check_for_misplaced_cmake_files(const Filesystem& fs,
                                                      const path& package_dir,
                                                      const PackageSpec& spec)
    {
        std::vector<path> dirs = {
            package_dir / "cmake",
            package_dir / "debug" / "cmake",
            package_dir / "lib" / "cmake",
            package_dir / "debug" / "lib" / "cmake",
        };

        std::vector<path> misplaced_cmake_files;
        for (auto&& dir : dirs)
        {
            auto files = fs.get_files_recursive(dir);
            for (auto&& file : files)
            {
                if (!fs.is_directory(file) && file.extension() == ".cmake")
                    misplaced_cmake_files.push_back(std::move(file));
            }
        }

        if (!misplaced_cmake_files.empty())
        {
            vcpkg::printf(
                Color::warning,
                "The following cmake files were found outside /share/%s. Please place cmake files in /share/%s.\n",
                spec.name(),
                spec.name());
            print_paths(misplaced_cmake_files);
            return LintStatus::ERROR_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    static LintStatus check_folder_debug_lib_cmake(const Filesystem& fs,
                                                   const path& package_dir,
                                                   const PackageSpec& spec)
    {
        const path lib_cmake_debug = package_dir / "debug" / "lib" / "cmake";
        if (fs.exists(lib_cmake_debug))
        {
            vcpkg::printf(Color::warning,
                          "The /debug/lib/cmake folder should be merged with /lib/cmake into /share/%s\n",
                          spec.name());
            return LintStatus::ERROR_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    static LintStatus check_for_dlls_in_lib_dir(const Filesystem& fs, const path& package_dir)
    {
        std::vector<path> dlls = fs.get_files_recursive(package_dir / "lib");
        Util::erase_remove_if(dlls, not_extension_pred(fs, ".dll"));

        if (!dlls.empty())
        {
            print2(Color::warning,
                   "\nThe following dlls were found in /lib or /debug/lib. Please move them to /bin or "
                   "/debug/bin, respectively.\n");
            print_paths(dlls);
            return LintStatus::ERROR_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    static LintStatus check_for_copyright_file(const Filesystem& fs, const PackageSpec& spec, const VcpkgPaths& paths)
    {
        const path packages_dir = paths.packages / spec.dir();
        const path copyright_file = packages_dir / "share" / spec.name() / "copyright";
        if (fs.exists(copyright_file))
        {
            return LintStatus::SUCCESS;
        }
        const path current_buildtrees_dir = paths.build_dir(spec);
        const path current_buildtrees_dir_src = current_buildtrees_dir / "src";

        std::vector<path> potential_copyright_files;
        // We only search in the root of each unpacked source archive to reduce false positives
        auto src_dirs = fs.get_files_non_recursive(current_buildtrees_dir_src);
        for (auto&& src_dir : src_dirs)
        {
            if (!fs.is_directory(src_dir)) continue;

            for (auto&& src_file : fs.get_files_non_recursive(src_dir))
            {
                const std::string filename = u8string(src_file.filename());

                if (filename == "LICENSE" || filename == "LICENSE.txt" || filename == "COPYING")
                {
                    potential_copyright_files.push_back(src_file);
                }
            }
        }

        vcpkg::printf(Color::warning,
                      "The software license must be available at ${CURRENT_PACKAGES_DIR}/share/%s/copyright\n",
                      spec.name());
        if (potential_copyright_files.size() == 1)
        {
            // if there is only one candidate, provide the cmake lines needed to place it in the proper location
            const path found_file = potential_copyright_files[0];
            auto found_relative_native = found_file.native();
            found_relative_native.erase(current_buildtrees_dir.native().size() +
                                        1); // The +1 is needed to remove the "/"
            const path relative_path = found_relative_native;
            vcpkg::printf("\n    configure_file(\"${CURRENT_BUILDTREES_DIR}/%s/%s\" "
                          "\"${CURRENT_PACKAGES_DIR}/share/%s/copyright\" COPYONLY)\n",
                          generic_u8string(relative_path),
                          generic_u8string(found_file.filename()),
                          spec.name());
        }
        else if (potential_copyright_files.size() > 1)
        {
            print2(Color::warning, "The following files are potential copyright files:\n");
            print_paths(potential_copyright_files);
        }
        return LintStatus::ERROR_DETECTED;
    }

    static LintStatus check_for_exes(const Filesystem& fs, const path& package_dir)
    {
        std::vector<path> exes = fs.get_files_recursive(package_dir / "bin");
        Util::erase_remove_if(exes, not_extension_pred(fs, ".exe"));

        if (!exes.empty())
        {
            print2(Color::warning,
                   "The following EXEs were found in /bin or /debug/bin. EXEs are not valid distribution targets.\n");
            print_paths(exes);
            return LintStatus::ERROR_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    static LintStatus check_exports_of_dlls(const Build::BuildPolicies& policies,
                                            const std::vector<path>& dlls,
                                            const path& dumpbin_exe)
    {
        if (policies.is_enabled(BuildPolicy::DLLS_WITHOUT_EXPORTS)) return LintStatus::SUCCESS;

        std::vector<path> dlls_with_no_exports;
        for (const path& dll : dlls)
        {
            auto cmd_line = Command(dumpbin_exe).string_arg("/exports").path_arg(dll);
            ExitCodeAndOutput ec_data = cmd_execute_and_capture_output(cmd_line);
            Checks::check_exit(
                VCPKG_LINE_INFO, ec_data.exit_code == 0, "Running command:\n   %s\n failed", cmd_line.command_line());

            if (ec_data.output.find("ordinal hint RVA      name") == std::string::npos)
            {
                dlls_with_no_exports.push_back(dll);
            }
        }

        if (!dlls_with_no_exports.empty())
        {
            print2(Color::warning, "The following DLLs have no exports:\n");
            print_paths(dlls_with_no_exports);
            print2(Color::warning, "DLLs without any exports are likely a bug in the build script.\n");
            vcpkg::printf(Color::warning,
                          "If this is intended, add the following line in the portfile:\n"
                          "    SET(%s enabled)\n",
                          to_cmake_variable(BuildPolicy::DLLS_WITHOUT_EXPORTS));
            return LintStatus::ERROR_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    static LintStatus check_uwp_bit_of_dlls(const std::string& expected_system_name,
                                            const std::vector<path>& dlls,
                                            const path dumpbin_exe)
    {
        if (expected_system_name != "WindowsStore")
        {
            return LintStatus::SUCCESS;
        }

        std::vector<path> dlls_with_improper_uwp_bit;
        for (const path& dll : dlls)
        {
            auto cmd_line = Command(dumpbin_exe).string_arg("/headers").path_arg(dll);
            ExitCodeAndOutput ec_data = cmd_execute_and_capture_output(cmd_line);
            Checks::check_exit(
                VCPKG_LINE_INFO, ec_data.exit_code == 0, "Running command:\n   %s\n failed", cmd_line.command_line());

            if (ec_data.output.find("App Container") == std::string::npos)
            {
                dlls_with_improper_uwp_bit.push_back(dll);
            }
        }

        if (!dlls_with_improper_uwp_bit.empty())
        {
            print2(Color::warning, "The following DLLs do not have the App Container bit set:\n");
            print_paths(dlls_with_improper_uwp_bit);
            print2(Color::warning, "This bit is required for Windows Store apps.\n");
            return LintStatus::ERROR_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    struct FileAndArch
    {
        path file;
        std::string actual_arch;
    };

#if defined(_WIN32)
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
            default: return "Machine Type Code = " + std::to_string(static_cast<uint16_t>(machine_type));
        }
    }
#endif

#if defined(_WIN32)
    static void print_invalid_architecture_files(const std::string& expected_architecture,
                                                 std::vector<FileAndArch> binaries_with_invalid_architecture)
    {
        print2(Color::warning, "The following files were built for an incorrect architecture:\n\n");
        for (const FileAndArch& b : binaries_with_invalid_architecture)
        {
            print2("    ",
                   vcpkg::u8string(b.file),
                   "\n"
                   "Expected ",
                   expected_architecture,
                   ", but was: ",
                   b.actual_arch,
                   "\n\n");
        }
    }

    static LintStatus check_dll_architecture(const std::string& expected_architecture,
                                             const std::vector<path>& files,
                                             const Filesystem& fs)
    {
        const auto dot_dll = vcpkg::u8path(".dll");
        std::vector<FileAndArch> binaries_with_invalid_architecture;

        for (const path& file : files)
        {
            Checks::check_exit(VCPKG_LINE_INFO,
                               file.extension() == dot_dll,
                               "The file extension was not .dll: %s",
                               generic_u8string(file));
            const CoffFileReader::DllInfo info = CoffFileReader::read_dll(fs.open_for_read(VCPKG_LINE_INFO, file));
            const std::string actual_architecture = get_actual_architecture(info.machine_type);

            if (expected_architecture != actual_architecture)
            {
                binaries_with_invalid_architecture.push_back({file, actual_architecture});
            }
        }

        if (!binaries_with_invalid_architecture.empty())
        {
            print_invalid_architecture_files(expected_architecture, binaries_with_invalid_architecture);
            return LintStatus::ERROR_DETECTED;
        }

        return LintStatus::SUCCESS;
    }
#endif

    static LintStatus check_lib_architecture(const std::string& expected_architecture,
                                             const std::vector<path>& files,
                                             const Filesystem& fs)
    {
#if defined(_WIN32)
        const auto dot_lib = vcpkg::u8path(".lib");
        std::vector<FileAndArch> binaries_with_invalid_architecture;

        for (const path& file : files)
        {
            Checks::check_exit(VCPKG_LINE_INFO,
                               file.extension() == dot_lib,
                               "The file extension was not .lib: %s",
                               generic_u8string(file));
            CoffFileReader::LibInfo info = CoffFileReader::read_lib(fs.open_for_read(VCPKG_LINE_INFO, file));

            // This is zero for folly's debug library
            // TODO: Why?
            if (info.machine_types.size() == 0) return LintStatus::SUCCESS;

            Checks::check_exit(VCPKG_LINE_INFO,
                               info.machine_types.size() == 1,
                               "Found more than 1 architecture in file %s",
                               generic_u8string(file));

            const std::string actual_architecture = get_actual_architecture(info.machine_types.at(0));
            if (expected_architecture != actual_architecture)
            {
                binaries_with_invalid_architecture.push_back({file, actual_architecture});
            }
        }

        if (!binaries_with_invalid_architecture.empty())
        {
            print_invalid_architecture_files(expected_architecture, binaries_with_invalid_architecture);
            return LintStatus::ERROR_DETECTED;
        }
#endif
        (void)expected_architecture;
        (void)files;
        (void)fs;
        return LintStatus::SUCCESS;
    }

    static LintStatus check_no_dlls_present(const Build::BuildPolicies& policies, const std::vector<path>& dlls)
    {
        if (dlls.empty() || policies.is_enabled(BuildPolicy::DLLS_IN_STATIC_LIBRARY))
        {
            return LintStatus::SUCCESS;
        }

        print2(Color::warning, "DLLs should not be present in a static build, but the following DLLs were found:\n");
        print_paths(dlls);
        return LintStatus::ERROR_DETECTED;
    }

    static LintStatus check_matching_debug_and_release_binaries(const std::vector<path>& debug_binaries,
                                                                const std::vector<path>& release_binaries)
    {
        const size_t debug_count = debug_binaries.size();
        const size_t release_count = release_binaries.size();
        if (debug_count == release_count)
        {
            return LintStatus::SUCCESS;
        }

        vcpkg::printf(Color::warning,
                      "Mismatching number of debug and release binaries. Found %zd for debug but %zd for release.\n",
                      debug_count,
                      release_count);
        print2("Debug binaries\n");
        print_paths(debug_binaries);

        print2("Release binaries\n");
        print_paths(release_binaries);

        if (debug_count == 0)
        {
            print2(Color::warning, "Debug binaries were not found\n");
        }
        if (release_count == 0)
        {
            print2(Color::warning, "Release binaries were not found\n");
        }

        print2("\n");

        return LintStatus::ERROR_DETECTED;
    }

    static LintStatus check_lib_files_are_available_if_dlls_are_available(const Build::BuildPolicies& policies,
                                                                          const size_t lib_count,
                                                                          const size_t dll_count,
                                                                          const path& lib_dir)
    {
        if (policies.is_enabled(BuildPolicy::DLLS_WITHOUT_LIBS)) return LintStatus::SUCCESS;

        if (lib_count == 0 && dll_count != 0)
        {
            print2(Color::warning, "Import libs were not present in ", vcpkg::u8string(lib_dir), "\n");
            vcpkg::printf(Color::warning,
                          "If this is intended, add the following line in the portfile:\n"
                          "    SET(%s enabled)\n",
                          to_cmake_variable(BuildPolicy::DLLS_WITHOUT_LIBS));
            return LintStatus::ERROR_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    static LintStatus check_bin_folders_are_not_present_in_static_build(const Build::BuildPolicies& policies,
                                                                        const Filesystem& fs,
                                                                        const path& package_dir)
    {
        if (policies.is_enabled(BuildPolicy::DLLS_IN_STATIC_LIBRARY)) return LintStatus::SUCCESS;

        const path bin = package_dir / "bin";
        const path debug_bin = package_dir / "debug" / "bin";

        if (!fs.exists(bin) && !fs.exists(debug_bin))
        {
            return LintStatus::SUCCESS;
        }

        if (fs.exists(bin))
        {
            vcpkg::printf(Color::warning,
                          R"(There should be no bin\ directory in a static build, but %s is present.)"
                          "\n",
                          vcpkg::u8string(bin));
        }

        if (fs.exists(debug_bin))
        {
            vcpkg::printf(Color::warning,
                          R"(There should be no debug\bin\ directory in a static build, but %s is present.)"
                          "\n",
                          vcpkg::u8string(debug_bin));
        }

        print2(
            Color::warning,
            R"(If the creation of bin\ and/or debug\bin\ cannot be disabled, use this in the portfile to remove them)"
            "\n"
            "\n"
            R"###(    if(VCPKG_LIBRARY_LINKAGE STREQUAL "static"))###"
            "\n"
            R"###(        file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/bin" "${CURRENT_PACKAGES_DIR}/debug/bin"))###"
            "\n"
            R"###(    endif())###"
            "\n\n");

        return LintStatus::ERROR_DETECTED;
    }

    static LintStatus check_no_empty_folders(const Filesystem& fs, const path& dir)
    {
        std::vector<path> empty_directories = fs.get_files_recursive(dir);

        Util::erase_remove_if(empty_directories, [&fs](const path& current) {
            return !fs.is_directory(current) || !fs.is_empty(current);
        });

        if (!empty_directories.empty())
        {
            print2(Color::warning, "There should be no empty directories in ", vcpkg::u8string(dir), "\n");
            print2("The following empty directories were found:\n");
            print_paths(empty_directories);
            print2(
                Color::warning,
                "If a directory should be populated but is not, this might indicate an error in the portfile.\n"
                "If the directories are not needed and their creation cannot be disabled, use something like this in "
                "the portfile to remove them:\n"
                "\n"
                R"###(    file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/a/dir" "${CURRENT_PACKAGES_DIR}/some/other/dir"))###"
                "\n"
                "\n"
                "\n");
            return LintStatus::ERROR_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    struct BuildTypeAndFile
    {
        path file;
        BuildType build_type;
    };

    static LintStatus check_crt_linkage_of_libs(const BuildType& expected_build_type,
                                                const std::vector<path>& libs,
                                                const path dumpbin_exe)
    {
        std::vector<BuildType> bad_build_types(BuildTypeC::VALUES.cbegin(), BuildTypeC::VALUES.cend());
        bad_build_types.erase(std::remove(bad_build_types.begin(), bad_build_types.end(), expected_build_type),
                              bad_build_types.end());

        std::vector<BuildTypeAndFile> libs_with_invalid_crt;

        for (const path& lib : libs)
        {
            auto cmd_line = Command(dumpbin_exe).string_arg("/directives").path_arg(lib);
            ExitCodeAndOutput ec_data = cmd_execute_and_capture_output(cmd_line);
            Checks::check_exit(VCPKG_LINE_INFO,
                               ec_data.exit_code == 0,
                               "Running command:\n   %s\n failed with message:\n%s",
                               cmd_line.command_line(),
                               ec_data.output);

            for (const BuildType& bad_build_type : bad_build_types)
            {
                if (std::regex_search(ec_data.output.cbegin(), ec_data.output.cend(), bad_build_type.crt_regex()))
                {
                    libs_with_invalid_crt.push_back({lib, bad_build_type});
                    break;
                }
            }
        }

        if (!libs_with_invalid_crt.empty())
        {
            vcpkg::printf(Color::warning,
                          "Invalid crt linkage. Expected %s, but the following libs had:\n\n",
                          expected_build_type.to_string());
            for (const BuildTypeAndFile& btf : libs_with_invalid_crt)
            {
                vcpkg::printf("    %s: %s\n", generic_u8string(btf.file), btf.build_type.to_string());
            }
            print2("\n");

            print2(Color::warning, "To inspect the lib files, use:\n    dumpbin.exe /directives mylibfile.lib\n");
            return LintStatus::ERROR_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    struct OutdatedDynamicCrtAndFile
    {
        path file;
        OutdatedDynamicCrt outdated_crt;
    };

    static LintStatus check_outdated_crt_linkage_of_dlls(const std::vector<path>& dlls,
                                                         const path dumpbin_exe,
                                                         const BuildInfo& build_info,
                                                         const PreBuildInfo& pre_build_info)
    {
        if (build_info.policies.is_enabled(BuildPolicy::ALLOW_OBSOLETE_MSVCRT)) return LintStatus::SUCCESS;

        std::vector<OutdatedDynamicCrtAndFile> dlls_with_outdated_crt;

        for (const path& dll : dlls)
        {
            auto cmd_line = Command(dumpbin_exe).string_arg("/dependents").path_arg(dll);
            ExitCodeAndOutput ec_data = cmd_execute_and_capture_output(cmd_line);
            Checks::check_exit(
                VCPKG_LINE_INFO, ec_data.exit_code == 0, "Running command:\n   %s\n failed", cmd_line.command_line());

            for (const OutdatedDynamicCrt& outdated_crt : get_outdated_dynamic_crts(pre_build_info.platform_toolset))
            {
                if (std::regex_search(ec_data.output.cbegin(), ec_data.output.cend(), outdated_crt.regex))
                {
                    dlls_with_outdated_crt.push_back({dll, outdated_crt});
                    break;
                }
            }
        }

        if (!dlls_with_outdated_crt.empty())
        {
            print2(Color::warning, "Detected outdated dynamic CRT in the following files:\n\n");
            for (const OutdatedDynamicCrtAndFile& btf : dlls_with_outdated_crt)
            {
                print2("    ", vcpkg::u8string(btf.file), ": ", btf.outdated_crt.name, "\n");
            }
            print2("\n");

            print2(Color::warning, "To inspect the dll files, use:\n    dumpbin.exe /dependents mydllfile.dll\n");
            return LintStatus::ERROR_DETECTED;
        }

        return LintStatus::SUCCESS;
    }

    static LintStatus check_no_files_in_dir(const Filesystem& fs, const path& dir)
    {
        std::vector<path> misplaced_files = fs.get_files_non_recursive(dir);
        Util::erase_remove_if(misplaced_files, [&fs](const path& target) {
            const std::string filename = u8string(target.filename());
            if (Strings::case_insensitive_ascii_equals(filename, "CONTROL") ||
                Strings::case_insensitive_ascii_equals(filename, "BUILD_INFO"))
            {
                return true;
            }

            return fs.is_directory(target);
        });

        if (!misplaced_files.empty())
        {
            print2(Color::warning, "The following files are placed in\n", vcpkg::u8string(dir), ":\n");
            print_paths(misplaced_files);
            print2(Color::warning, "Files cannot be present in those directories.\n\n");
            return LintStatus::ERROR_DETECTED;
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
        const path package_dir = paths.package_dir(spec);

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

        const path debug_lib_dir = package_dir / "debug" / "lib";
        const path release_lib_dir = package_dir / "lib";
        const path debug_bin_dir = package_dir / "debug" / "bin";
        const path release_bin_dir = package_dir / "bin";

        std::vector<path> debug_libs = fs.get_files_recursive(debug_lib_dir);
        Util::erase_remove_if(debug_libs, not_extension_pred(fs, ".lib"));
        std::vector<path> release_libs = fs.get_files_recursive(release_lib_dir);
        Util::erase_remove_if(release_libs, not_extension_pred(fs, ".lib"));

        if (!pre_build_info.build_type && !build_info.policies.is_enabled(BuildPolicy::MISMATCHED_NUMBER_OF_BINARIES))
            error_count += check_matching_debug_and_release_binaries(debug_libs, release_libs);

        if (!build_info.policies.is_enabled(BuildPolicy::SKIP_ARCHITECTURE_CHECK))
        {
            std::vector<path> libs;
            libs.insert(libs.cend(), debug_libs.cbegin(), debug_libs.cend());
            libs.insert(libs.cend(), release_libs.cbegin(), release_libs.cend());
            error_count += check_lib_architecture(pre_build_info.target_architecture, libs, fs);
        }

        std::vector<path> debug_dlls = fs.get_files_recursive(debug_bin_dir);
        Util::erase_remove_if(debug_dlls, not_extension_pred(fs, ".dll"));
        std::vector<path> release_dlls = fs.get_files_recursive(release_bin_dir);
        Util::erase_remove_if(release_dlls, not_extension_pred(fs, ".dll"));

        switch (build_info.library_linkage)
        {
            case Build::LinkageType::DYNAMIC:
            {
                if (!pre_build_info.build_type &&
                    !build_info.policies.is_enabled(BuildPolicy::MISMATCHED_NUMBER_OF_BINARIES))
                    error_count += check_matching_debug_and_release_binaries(debug_dlls, release_dlls);

                error_count += check_lib_files_are_available_if_dlls_are_available(
                    build_info.policies, debug_libs.size(), debug_dlls.size(), debug_lib_dir);
                error_count += check_lib_files_are_available_if_dlls_are_available(
                    build_info.policies, release_libs.size(), release_dlls.size(), release_lib_dir);

                std::vector<path> dlls;
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
            case Build::LinkageType::STATIC:
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
                            BuildType::value_of(Build::ConfigurationType::DEBUG, build_info.crt_linkage),
                            debug_libs,
                            toolset.dumpbin);
                    }
                    error_count += check_crt_linkage_of_libs(
                        BuildType::value_of(Build::ConfigurationType::RELEASE, build_info.crt_linkage),
                        release_libs,
                        toolset.dumpbin);
                }
                break;
            }
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }

        error_count += check_no_empty_folders(fs, package_dir);
        error_count += check_no_files_in_dir(fs, package_dir);
        error_count += check_no_files_in_dir(fs, package_dir / "debug");

        return error_count;
    }

    size_t perform_all_checks(const PackageSpec& spec,
                              const VcpkgPaths& paths,
                              const PreBuildInfo& pre_build_info,
                              const BuildInfo& build_info,
                              const path& port_dir)
    {
        print2("-- Performing post-build validation\n");
        const size_t error_count = perform_all_checks_and_return_error_count(spec, paths, pre_build_info, build_info);

        if (error_count != 0)
        {
            const path portfile = port_dir / "portfile.cmake";
            print2(Color::error,
                   "Found ",
                   error_count,
                   " error(s). Please correct the portfile:\n    ",
                   vcpkg::u8string(portfile),
                   "\n");
        }

        print2("-- Performing post-build validation done\n");

        return error_count;
    }
}
