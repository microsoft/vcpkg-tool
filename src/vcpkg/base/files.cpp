#include <vcpkg/base/system_headers.h>

#include <vcpkg/base/files.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.h>
#include <vcpkg/base/system.print.h>
#include <vcpkg/base/system.process.h>
#include <vcpkg/base/util.h>

#include <assert.h>

#if !defined(_WIN32)
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>

#include <sys/file.h>
#include <sys/stat.h>
#endif // !_WIN32

#if defined(__linux__)
#include <sys/sendfile.h>
#elif defined(__APPLE__)
#include <copyfile.h>
#endif // ^^^ defined(__APPLE__)

#include <algorithm>
#include <list>
#include <string>
#include <thread>

#if defined(_WIN32)
#include <filesystem>
namespace stdfs = std::filesystem;
#endif // _WIN32

namespace
{
    using namespace vcpkg;

    std::atomic<uint64_t> g_us_filesystem_stats(0);

    struct IsSlash
    {
        bool operator()(const char c) const noexcept
        {
            return c == '/'
#if defined(_WIN32)
                   || c == '\\'
#endif // _WIN32
                ;
        }
    };

    constexpr IsSlash is_slash;

    bool is_dot(StringView sv) { return sv.size() == 1 && sv.byte_at_index(0) == '.'; }
    bool is_dot_dot(StringView sv)
    {
        return sv.size() == 2 && sv.byte_at_index(0) == '.' && sv.byte_at_index(1) == '.';
    }
#if !defined(_WIN32)
    bool is_dot_or_dot_dot(const char* ntbs)
    {
        return ntbs[0] == '.' && (ntbs[1] == '\0' || (ntbs[1] == '.' && ntbs[2] == '\0'));
    }
#endif

    [[noreturn]] void exit_filesystem_call_error(LineInfo li,
                                                 const std::error_code& ec,
                                                 StringView call_name,
                                                 std::initializer_list<StringView> args)
    {
        Checks::exit_with_message(
            li, Strings::concat(call_name, "(", Strings::join(", ", args.begin(), args.end()), "): ", ec.message()));
    }

#if defined(_WIN32)
    FileType convert_file_type(stdfs::file_type type) noexcept
    {
        switch (type)
        {
            case stdfs::file_type::none: return FileType::none;
            case stdfs::file_type::not_found: return FileType::not_found;
            case stdfs::file_type::regular: return FileType::regular;
            case stdfs::file_type::directory: return FileType::directory;
            case stdfs::file_type::symlink: return FileType::symlink;
            case stdfs::file_type::block: return FileType::block;
            case stdfs::file_type::character: return FileType::character;
            case stdfs::file_type::fifo: return FileType::fifo;
            case stdfs::file_type::socket: return FileType::socket;
            case stdfs::file_type::unknown: return FileType::unknown;
#if !defined(__MINGW32__)
            case stdfs::file_type::junction: return FileType::junction;
#endif
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }
    }

    stdfs::path to_stdfs_path(const Path& utfpath) { return stdfs::path(Strings::to_utf16(utfpath.native())); }

    Path from_stdfs_path(const stdfs::path& stdpath) { return Strings::to_utf8(stdpath.native()); }

    // The Win32 version of this implementation is effectively forked from
    // https://github.com/microsoft/STL/blob/bd7adb4a932725f60ba096580c415616486ab64c/stl/inc/filesystem#L436
    // converted to speak UTF-8 rather than UTF-16.
    template<class Ty>
    Ty unaligned_load(const void* pv) noexcept
    {
        static_assert(std::is_trivial<Ty>::value, "Unaligned loads require trivial types");
        Ty tmp;
        memcpy(&tmp, pv, sizeof(tmp));
        return tmp;
    }

    bool is_drive_prefix(const char* const first) noexcept
    {
        // test if first points to a prefix of the form X:
        // pre: first points to at least 2 char instances
        // pre: Little endian
        auto value = unaligned_load<unsigned short>(first);
        value &= 0xFFDFu; // transform lowercase drive letters into uppercase ones
        value -= (static_cast<unsigned short>(':') << CHAR_BIT) | 'A';
        return value < 26;
    }

    bool has_drive_letter_prefix(const char* const first, const char* const last) noexcept
    {
        // test if [first, last) has a prefix of the form X:
        return last - first >= 2 && is_drive_prefix(first);
    }
#endif // _WIN32

    const char* find_root_name_end(const char* const first, const char* const last) noexcept
    {
#if defined(_WIN32)
        // attempt to parse [first, last) as a path and return the end of root-name if it exists; otherwise, first

        // This is the place in the generic grammar where library implementations have the most freedom.
        // Below are example Windows paths, and what microsoft/STL decided to do with them:
        // * X:DriveRelative, X:\DosAbsolute
        //   We parse X: as root-name, if and only if \ is present we consider that root-directory
        // * \RootRelative
        //   We parse no root-name, and \ as root-directory
        // * \\server\share
        //   We parse \\server as root-name, \ as root-directory, and share as the first element in relative-path.
        //   Technically, Windows considers all of \\server\share the logical "root", but for purposes
        //   of decomposition we want those split, so that path(R"(\\server\share)").replace_filename("other_share")
        //   is \\server\other_share
        // * \\?\device
        // * \??\device
        // * \\.\device
        //   CreateFile appears to treat these as the same thing; we will set the first three characters as root-name
        //   and the first \ as root-directory. Support for these prefixes varies by particular Windows version, but
        //   for the purposes of path decomposition we don't need to worry about that.
        // * \\?\UNC\server\share
        //   MSDN explicitly documents the \\?\UNC syntax as a special case. What actually happens is that the device
        //   Mup, or "Multiple UNC provider", owns the path \\?\UNC in the NT namespace, and is responsible for the
        //   network file access. When the user says \\server\share, CreateFile translates that into
        //   \\?\UNC\server\share to get the remote server access behavior. Because NT treats this like any other
        //   device, we have chosen to treat this as the \\?\ case above.
        if (last - first < 2)
        {
            return first;
        }

        if (has_drive_letter_prefix(first, last))
        { // check for X: first because it's the most common root-name
            return first + 2;
        }

        if (!is_slash(first[0]))
        { // all the other root-names start with a slash; check that first because
          // we expect paths without a leading slash to be very common
            return first;
        }

        // $ means anything other than a slash, including potentially the end of the input
        if (last - first >= 4 && is_slash(first[3]) && (last - first == 4 || !is_slash(first[4])) // \xx\$
            && ((is_slash(first[1]) && (first[2] == '?' || first[2] == '.'))                      // \\?\$ or \\.\$
                || (first[1] == '?' && first[2] == '?')))
        { // \??\$
            return first + 3;
        }

        if (last - first >= 3 && is_slash(first[1]) && !is_slash(first[2]))
        { // \\server
            return std::find_if(first + 3, last, is_slash);
        }

        // no match
        return first;
#else  // ^^^ _WIN32 / !_WIN32 vvv
        (void)last;
        return first;
#endif // _WIN32
    }

    const char* find_relative_path(const char* const first, const char* const last) noexcept
    {
        // attempt to parse [first, last) as a path and return the start of relative-path
        return std::find_if_not(find_root_name_end(first, last), last, is_slash);
    }

    StringView parse_parent_path(const StringView str) noexcept
    {
        // attempt to parse str as a path and return the parent_path if it exists; otherwise, an empty view
        const auto first = str.data();
        auto last = first + str.size();
        const auto relative_path = find_relative_path(first, last);
        // case 1: relative-path ends in a directory-separator, remove the separator to remove "magic empty path"
        //  for example: R"(/cat/dog/\//\)"
        // case 2: relative-path doesn't end in a directory-separator, remove the filename and last directory-separator
        //  to prevent creation of a "magic empty path"
        //  for example: "/cat/dog"
        while (relative_path != last && !is_slash(last[-1]))
        {
            // handle case 2 by removing trailing filename, puts us into case 1
            --last;
        }

        while (relative_path != last && is_slash(last[-1]))
        { // handle case 1 by removing trailing slashes
            --last;
        }

        return StringView(first, static_cast<size_t>(last - first));
    }

    const char* find_filename(const char* const first, const char* last) noexcept
    {
        // attempt to parse [first, last) as a path and return the start of filename if it exists; otherwise, last
        const auto relative_path = find_relative_path(first, last);
        while (relative_path != last && !is_slash(last[-1]))
        {
            --last;
        }

        return last;
    }

    StringView parse_filename(const StringView str) noexcept
    {
        // attempt to parse str as a path and return the filename if it exists; otherwise, an empty view
        const auto first = str.data();
        const auto last = first + str.size();
        const auto filename = find_filename(first, last);
        return StringView(filename, static_cast<size_t>(last - filename));
    }

    constexpr const char* find_extension(const char* const filename, const char* const ads) noexcept
    {
        // find dividing point between stem and extension in a generic format filename consisting of [filename, ads)
        auto extension = ads;
        if (filename == extension)
        { // empty path
            return ads;
        }

        --extension;
        if (filename == extension)
        {
            // path is length 1 and either dot, or has no dots; either way, extension() is empty
            return ads;
        }

        if (*extension == '.')
        { // we might have found the end of stem
            if (filename == extension - 1 && extension[-1] == '.')
            { // dotdot special case
                return ads;
            }
            else
            { // x.
                return extension;
            }
        }

        while (filename != --extension)
        {
            if (*extension == '.')
            { // found a dot which is not in first position, so it starts extension()
                return extension;
            }
        }

        // if we got here, either there are no dots, in which case extension is empty, or the first element
        // is a dot, in which case we have the leading single dot special case, which also makes extension empty
        return ads;
    }

    StringView parse_stem(const StringView str) noexcept
    {
        // attempt to parse str as a path and return the stem if it exists; otherwise, an empty view
        const auto first = str.data();
        const auto last = first + str.size();
        const auto filename = find_filename(first, last);
#if defined(_WIN32)
        const auto ads = std::find(filename, last, ':'); // strip alternate data streams in intra-filename decomposition
        const auto extension = find_extension(filename, ads);
#else  // ^^^ _WIN32 / !_WIN32 vvv
        const auto extension = find_extension(filename, last);
#endif // _WIN32
        return StringView(filename, static_cast<size_t>(extension - filename));
    }

    StringView parse_extension(const StringView str) noexcept
    {
        // attempt to parse str as a path and return the extension if it exists; otherwise, an empty view
        const auto first = str.data();
        const auto last = first + str.size();
        const auto filename = find_filename(first, last);
#if defined(_WIN32)
        const auto ads = std::find(filename, last, ':'); // strip alternate data streams in intra-filename decomposition
        const auto extension = find_extension(filename, ads);
        return StringView(extension, static_cast<size_t>(ads - extension));
#else  // ^^^ _WIN32 / !_WIN32 vvv
        const auto extension = find_extension(filename, last);
        return StringView(extension, static_cast<size_t>(last - extension));
#endif // _WIN32
    }

    bool is_absolute_path(const StringView str) noexcept
    {
#if defined(_WIN32)
        // paths with a root-name that is a drive letter and no root-directory are drive relative, such as x:example
        // paths with no root-name or root-directory are relative, such as example
        // paths with no root-name but a root-directory are root relative, such as \example
        // all other paths are absolute
        const auto first = str.data();
        const auto last = first + str.size();
        if (has_drive_letter_prefix(first, last))
        { // test for X:\ but not X:cat
            return last - first >= 3 && is_slash(first[2]);
        }

        // if root-name is otherwise nonempty, then it must be one of the always-absolute prefixes like
        // \\?\ or \\server, so the path is absolute. Otherwise it is relative.
        return first != find_root_name_end(first, last);
#else  // ^^^ _WIN32 / !_WIN32 vvv
        return !str.empty() && str.byte_at_index(0) == '/';
#endif // ^^^ !_WIN32
    }

    void translate_not_found_to_success(std::error_code& ec)
    {
        if (ec && (ec == std::errc::no_such_file_or_directory || ec == std::errc::not_a_directory))
        {
            ec.clear();
        }
    }

#if defined(_WIN32)
    struct FileHandle
    {
        HANDLE h_file;
        FileHandle() = delete;
        FileHandle(const FileHandle&) = delete;
        FileHandle& operator=(const FileHandle&) = delete;

        FileHandle(const wchar_t* file_name,
                   DWORD desired_access,
                   DWORD share_mode,
                   DWORD creation_disposition,
                   DWORD flags_and_attributes,
                   std::error_code& ec) noexcept
            : h_file(::CreateFileW(
                  file_name, desired_access, share_mode, nullptr, creation_disposition, flags_and_attributes, 0))
        {
            if (h_file == INVALID_HANDLE_VALUE)
            {
                const auto last_error = static_cast<int>(GetLastError());
                ec.assign(last_error, std::system_category());
            }
            else
            {
                ec.clear();
            }
        }

        // https://developercommunity.visualstudio.com/t/Spurious-warning-C6001-Using-uninitial/1299941
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 6001)
#endif // ^^^ _MSC_VER
        ~FileHandle()
        {
            if (h_file != INVALID_HANDLE_VALUE)
            {
                Checks::check_exit(VCPKG_LINE_INFO, ::CloseHandle(h_file));
            }
        }
#if defined(_MSC_VER)
#pragma warning(pop)
#endif // ^^^ _MSC_VER
    };

    struct RemoveAllErrorInfo
    {
        std::error_code ec;
        Path failure_point;

        bool check_ec(const std::error_code& ec_arg, const stdfs::directory_entry& current_entry)
        {
            if (ec_arg)
            {
                ec = ec_arg;
                failure_point = from_stdfs_path(current_entry.path());
                return true;
            }

            return false;
        }
    };

    // does _not_ follow symlinks
    void remove_file_attribute_readonly(const stdfs::path& target, std::error_code& ec) noexcept
    {
        auto const file_name = target.c_str();
        WIN32_FILE_ATTRIBUTE_DATA attributes;
        if (!GetFileAttributesExW(file_name, GetFileExInfoStandard, &attributes))
        {
            ec.assign(GetLastError(), std::system_category());
            return;
        }

        auto dw_attributes = attributes.dwFileAttributes;
        dw_attributes &= ~FILE_ATTRIBUTE_READONLY;
        if (!SetFileAttributesW(file_name, dw_attributes))
        {
            ec.assign(GetLastError(), std::system_category());
            return;
        }

        ec.clear();
    }

    // Custom implementation of stdfs::remove_all intended to be resilient to transient issues
    void vcpkg_remove_all_impl(const stdfs::directory_entry& current_entry, RemoveAllErrorInfo& err)
    {
        std::error_code ec;
        const auto path_status = current_entry.symlink_status(ec);
        if (path_status.type() == stdfs::file_type::not_found)
        {
            return;
        }

        if (err.check_ec(ec, current_entry))
        {
            return;
        }

        if ((path_status.permissions() & stdfs::perms::owner_write) != stdfs::perms::owner_write)
        {
            remove_file_attribute_readonly(current_entry, ec);
            if (err.check_ec(ec, current_entry)) return;
        }

        if (stdfs::is_directory(path_status))
        {
            for (const auto& entry : stdfs::directory_iterator(current_entry.path()))
            {
                vcpkg_remove_all_impl(entry, err);
                if (err.ec) return;
            }
            if (!RemoveDirectoryW(current_entry.path().c_str()))
            {
                ec.assign(GetLastError(), std::system_category());
            }
        }
        else
        {
            stdfs::remove(current_entry.path(), ec);
        }

        err.check_ec(ec, current_entry);
    }

    void vcpkg_remove_all(const stdfs::directory_entry& current_entry, std::error_code& ec, Path& failure_point)
    {
        RemoveAllErrorInfo err;
        for (int backoff = 0; backoff < 5; ++backoff)
        {
            if (backoff)
            {
                using namespace std::chrono_literals;
                auto backoff_time = 100ms * backoff;
                std::this_thread::sleep_for(backoff_time);
            }

            vcpkg_remove_all_impl(current_entry, err);
            if (!err.ec)
            {
                break;
            }
        }

        ec = std::move(err.ec);
        failure_point = std::move(err.failure_point);
    }

    void vcpkg_remove_all(const Path& base, std::error_code& ec, Path& failure_point)
    {
        std::wstring wide_path;
        const auto& native_base = base.native();
        // Attempt to handle paths that are too long in recursive delete by prefixing absolute ones with
        // backslash backslash question backslash
        // See https://docs.microsoft.com/en-us/windows/win32/fileio/maximum-file-path-limitation
        // We are conservative and only accept paths that begin with a drive letter prefix because other forms
        // may have more Win32 path normalization that we do not replicate herein.
        // (There are still edge cases we don't handle, such as trailing whitespace or nulls, but
        // for purposes of remove_all, we never supported such trailing bits)
#if !defined(__MINGW32__)
        if (has_drive_letter_prefix(native_base.data(), native_base.data() + native_base.size()))
        {
            wide_path = L"\\\\?\\";
        }
#endif

        wide_path.append(Strings::to_utf16(native_base));

        stdfs::directory_entry entry(wide_path, ec);
        translate_not_found_to_success(ec);
        if (ec)
        {
            failure_point = base;
            return;
        }
        vcpkg_remove_all(entry, ec, failure_point);
    }

#else // ^^^ _WIN32 // !_WIN32 vvv
    bool stat_is_directory(const char* target) noexcept
    {
        struct stat s;
        if (::stat(target, &s) != 0)
        {
            return false;
        }

        return S_ISDIR(s.st_mode);
    }

    bool stat_is_regular_file(const char* target) noexcept
    {
        struct stat s;
        if (::stat(target, &s) != 0)
        {
            return false;
        }

        return S_ISREG(s.st_mode);
    }

    FileType posix_translate_stat_mode_to_file_type(mode_t mode) noexcept
    {
        if (S_ISBLK(mode))
        {
            return FileType::block;
        }

        if (S_ISCHR(mode))
        {
            return FileType::character;
        }

        if (S_ISDIR(mode))
        {
            return FileType::directory;
        }

        if (S_ISFIFO(mode))
        {
            return FileType::fifo;
        }

        if (S_ISREG(mode))
        {
            return FileType::regular;
        }

        if (S_ISLNK(mode))
        {
            return FileType::symlink;
        }

        if (S_ISSOCK(mode))
        {
            return FileType::socket;
        }

        return FileType::unknown;
    }

    struct PosixFd
    {
        PosixFd() = default;

        PosixFd(const char* path, int oflag, std::error_code& ec) noexcept : fd(::open(path, oflag))
        {
            check_error(ec);
        }

        PosixFd(const char* path, int oflag, mode_t mode, std::error_code& ec) noexcept : fd(::open(path, oflag, mode))
        {
            check_error(ec);
        }

        void swap(PosixFd& other) noexcept { std::swap(fd, other.fd); }

        PosixFd(const PosixFd&) = delete;
        PosixFd(PosixFd&& other) : fd(std::exchange(other.fd, -1)) { }
        PosixFd& operator=(const PosixFd&) = delete;
        PosixFd& operator=(PosixFd&& other)
        {
            PosixFd{std::move(other)}.swap(*this);
            return *this;
        }

        int flock(int operation) const noexcept { return ::flock(fd, operation); }

        ssize_t read(void* buf, size_t count) const noexcept { return ::read(fd, buf, count); }

        ssize_t write(void* buf, size_t count) const noexcept { return ::write(fd, buf, count); }

        int fstat(struct stat* statbuf) const noexcept { return ::fstat(fd, statbuf); }

        void fstat(struct stat* statbuf, std::error_code& ec) const noexcept
        {
            if (::fstat(fd, statbuf) == 0)
            {
                ec.clear();
            }
            else
            {
                ec.assign(errno, std::generic_category());
            }
        }

        void ftruncate(off_t length, std::error_code& ec) const noexcept
        {
            if (::ftruncate(fd, length) == 0)
            {
                ec.clear();
            }
            else
            {
                ec.assign(errno, std::generic_category());
            }
        }

        void fchmod(mode_t new_mode, std::error_code& ec) const noexcept
        {
            if (::fchmod(fd, new_mode) == 0)
            {
                ec.clear();
            }
            else
            {
                ec.assign(errno, std::generic_category());
            }
        }

        operator bool() const noexcept { return fd >= 0; }

        int get() const noexcept { return fd; }

        void close() noexcept
        {
            if (fd >= 0)
            {
                Checks::check_exit(VCPKG_LINE_INFO, ::close(fd) == 0);
                fd = -1;
            }
        }

        ~PosixFd() { close(); }

    private:
        int fd = -1;

        void check_error(std::error_code& ec)
        {
            if (fd >= 0)
            {
                ec.clear();
            }
            else
            {
                ec.assign(errno, std::generic_category());
            }
        }
    };

    struct ReadDirOp
    {
        DIR* dirp;

        ReadDirOp(const Path& base, std::error_code& ec) : dirp(opendir(base.c_str()))
        {
            if (dirp)
            {
                ec.clear();
            }
            else
            {
                ec.assign(errno, std::generic_category());
            }
        }

        ReadDirOp(const ReadDirOp&) = delete;
        ReadDirOp& operator=(const ReadDirOp&) = delete;

        const dirent* read() const
        {
            // https://www.gnu.org/software/libc/manual/html_node/Reading_002fClosing-Directory.html
            // > In the GNU C Library, it is safe to call readdir from multiple threads as long as
            // > each thread uses its own DIR object. POSIX.1-2008 does not require this to be safe,
            // > but we are not aware of any operating systems where it does not work.
            return readdir(dirp);
        }

        const dirent* read(std::error_code& ec) const
        {
            errno = 0;
            const dirent* result = readdir(dirp);
            if (result || errno == 0)
            {
                ec.clear();
            }
            else
            {
                ec.assign(errno, std::generic_category());
            }

            return result;
        }

        ~ReadDirOp()
        {
            if (dirp)
            {
                Checks::check_exit(VCPKG_LINE_INFO, closedir(dirp) == 0);
            }
        }
    };

#if defined(_DIRENT_HAVE_D_TYPE)
    enum class PosixDType : unsigned char
    {
        Unknown = DT_UNKNOWN,
        Regular = DT_REG,
        Directory = DT_DIR,
        Fifo = DT_FIFO,
        Socket = DT_SOCK,
        CharacterDevice = DT_CHR,
        BlockDevice = DT_BLK,
        Link = DT_LNK
    };

    PosixDType get_d_type(const struct dirent* d) noexcept { return static_cast<PosixDType>(d->d_type); }
#else  // ^^^ _DIRENT_HAVE_D_TYPE // !_DIRENT_HAVE_D_TYPE
    enum class PosixDType : unsigned char
    {
        Unknown = 0,
        Regular = 1,
        Directory = 2,
        Fifo = 3,
        Socket = 4,
        CharacterDevice = 5,
        BlockDevice = 6,
        Link = 7
    };

    PosixDType get_d_type(const struct dirent*) noexcept { return PosixDType::Unknown; }
#endif // ^^^ !_DIRENT_HAVE_D_TYPE
    void mark_recursive_error(const Path& base, std::error_code& ec, Path& failure_point)
    {
        failure_point = base;
        ec.assign(errno, std::generic_category());
    }

    void vcpkg_remove_all_directory(const Path& base,
                                    std::error_code& ec,
                                    Path& failure_point,
                                    struct stat& base_lstat);

    void vcpkg_remove_all(const Path& base,
                          std::error_code& ec,
                          Path& failure_point,
                          PosixDType base_dtype = PosixDType::Unknown)
    {
        struct stat s;
        if (base_dtype == PosixDType::Unknown)
        {
            // We have to check that `base` isn't a symbolic link
            if (::lstat(base.c_str(), &s) != 0)
            {
                if (errno != ENOENT && errno != ENOTDIR)
                {
                    mark_recursive_error(base, ec, failure_point);
                    return;
                }

                ec.clear();
                return;
            }

            if (S_ISDIR(s.st_mode))
            {
                vcpkg_remove_all_directory(base, ec, failure_point, s);
                return;
            }
        }
        else if (base_dtype == PosixDType::Directory)
        {
            // establish `base` being writable before calling vcpkg_remove_all_directory
            if (::lstat(base.c_str(), &s) != 0)
            {
                // no ENOENT check here since we were supposed to be visiting a directory
                mark_recursive_error(base, ec, failure_point);
                return;
            }

            if (!S_ISDIR(s.st_mode))
            {
                // if it isn't still a directory something is racy
                ec = std::make_error_code(std::errc::device_or_resource_busy);
                mark_recursive_error(base, ec, failure_point);
                return;
            }

            vcpkg_remove_all_directory(base, ec, failure_point, s);
            return;
        }

        if (::unlink(base.c_str()) != 0)
        {
            mark_recursive_error(base, ec, failure_point);
        }
    }

    void vcpkg_remove_all_directory(const Path& base, std::error_code& ec, Path& failure_point, struct stat& base_lstat)
    {
        // ensure that the directory is writable and executable
        // NOTE: the execute bit on directories is needed to allow opening files inside of that directory
        if ((base_lstat.st_mode & (S_IWUSR | S_IXUSR)) != (S_IWUSR | S_IXUSR))
        {
            if (::chmod(base.c_str(), base_lstat.st_mode | S_IWUSR | S_IXUSR) != 0)
            {
                ec.assign(errno, std::generic_category());
                mark_recursive_error(base, ec, failure_point);
                return;
            }
        }

        // delete everything inside
        ReadDirOp op{base, ec};
        if (ec)
        {
            mark_recursive_error(base, ec, failure_point);
            return;
        }

        for (;;)
        {
            errno = 0;
            const auto entry = op.read();
            if (!entry)
            {
                if (errno != 0)
                {
                    mark_recursive_error(base, ec, failure_point);
                    return;
                }

                // no more entries left, fall down to rmdir below
                break;
            }

            // delete base / entry.d_name, recursively
            if (is_dot_or_dot_dot(entry->d_name))
            {
                continue;
            }

            vcpkg_remove_all(base / entry->d_name, ec, failure_point, get_d_type(entry));
            if (ec)
            {
                // removing a contained entity failed; the recursive call will set failure_point
                return;
            }
        }

        if (::rmdir(base.c_str()) != 0)
        {
            mark_recursive_error(base, ec, failure_point);
        }
    }
#endif // ^^^ !_WIN32
}

#if defined(_WIN32)
namespace
{
    struct FindFirstOp
    {
        HANDLE h_find = INVALID_HANDLE_VALUE;
        WIN32_FIND_DATAW find_data;

        unsigned long find_first(const wchar_t* const path) noexcept
        {
            assert(h_find == INVALID_HANDLE_VALUE);
            h_find = FindFirstFileW(path, &find_data);
            if (h_find == INVALID_HANDLE_VALUE)
            {
                return GetLastError();
            }

            return ERROR_SUCCESS;
        }

        FindFirstOp() = default;
        FindFirstOp(const FindFirstOp&) = delete;
        FindFirstOp& operator=(const FindFirstOp&) = delete;

        ~FindFirstOp()
        {
            if (h_find != INVALID_HANDLE_VALUE)
            {
                (void)FindClose(h_find);
            }
        }
    };
} // unnamed namespace
#endif // _WIN32

namespace vcpkg
{
    std::string Path::generic_u8string() const
    {
#if defined(_WIN32)
        auto result = m_str;
        std::replace(result.begin(), result.end(), '\\', '/');
        return result;
#else
        return m_str;
#endif
    }

    Path Path::operator/(StringView sv) const&
    {
        Path result = *this;
        result /= sv;
        return result;
    }

    Path Path::operator/(StringView sv) &&
    {
        *this /= sv;
        return std::move(*this);
    }

    Path Path::operator+(StringView sv) const&
    {
        Path result = *this;
        result.m_str.append(sv.data(), sv.size());
        return result;
    }

    Path Path::operator+(StringView sv) &&
    {
        m_str.append(sv.data(), sv.size());
        return std::move(*this);
    }

    Path& Path::operator/=(StringView sv)
    {
        // set *this to the path lexically resolved by _Other relative to *this
        // examples:
        // path("cat") / "c:/dog"; // yields "c:/dog"
        // path("cat") / "c:"; // yields "c:"
        // path("c:") / ""; // yields "c:"
        // path("c:cat") / "/dog"; // yields "c:/dog"
        // path("c:cat") / "c:dog"; // yields "c:cat/dog"
        // path("c:cat") / "d:dog"; // yields "d:dog"
        // several places herein quote the standard, but the standard's variable p is replaced with _Other

        if (is_absolute_path(sv))
        { // if _Other.is_absolute(), then op=(_Other)
            m_str.assign(sv.data(), sv.size());
            return *this;
        }

        const char* my_first = m_str.data();
        const auto my_last = my_first + m_str.size();
        const auto other_first = sv.data();
        const auto other_last = other_first + sv.size();
        const auto my_root_name_end = find_root_name_end(my_first, my_last);
        const auto other_root_name_end = find_root_name_end(other_first, other_last);
        if (other_first != other_root_name_end &&
            !std::equal(my_first, my_root_name_end, other_first, other_root_name_end))
        {
            // if _Other.has_root_name() && _Other.root_name() != root_name(), then op=(_Other)
            m_str.assign(sv.data(), sv.size());
            return *this;
        }

        if (other_root_name_end != other_last && is_slash(*other_root_name_end))
        {
            // If _Other.has_root_directory() removes any root directory and relative-path from *this
            m_str.erase(static_cast<size_t>(my_root_name_end - my_first));
        }
        else
        {
            // Otherwise, if (!has_root_directory && is_absolute) || has_filename appends path::preferred_separator
#if defined(_WIN32)
            if (my_root_name_end == my_last)
            {
                // Here, !has_root_directory && !has_filename
                // Going through our root_name kinds:
                // X: can't be absolute here, since those paths are absolute only when has_root_directory
                // \\?\ can't exist without has_root_directory
                // \\server can be absolute here
                if (my_root_name_end - my_first >= 3)
                {
                    m_str.push_back(preferred_separator);
                }
            }
            else
            {
                // Here, has_root_directory || has_filename
                // If there is a trailing slash, the trailing slash might be part of root_directory.
                // If it is, has_root_directory && !has_filename, so the test fails.
                // If there is a trailing slash not part of root_directory, then !has_filename, so only
                // (!has_root_directory && is_absolute) remains
                // Going through our root_name kinds:
                // X:cat\ needs a root_directory to be absolute
                // \\server\cat must have a root_directory to exist with a relative_path
                // \\?\ must have a root_directory to exist
                // As a result, the test fails if there is a trailing slash.
                // If there is no trailing slash, then has_filename, so the test passes.
                // Therefore, the test passes if and only if there is no trailing slash.
                if (!is_slash(my_last[-1]))
                {
                    m_str.push_back(preferred_separator);
                }
            }
#else  // ^^^ _WIN32 / !_WIN32 vvv
       // (!has_root_directory && is_absolute) can't happen on POSIX, so this just becomes has_filename
            const auto my_relative_first = std::find_if_not(my_root_name_end, my_last, is_slash);
            if (my_relative_first != my_last && !is_slash(my_last[-1]))
            {
                m_str.push_back(preferred_separator);
            }
#endif // _WIN32
        }

        // Then appends the native format pathname of _Other, omitting any root-name from its generic format
        // pathname, to the native format pathname.
        m_str.append(other_root_name_end, static_cast<size_t>(other_last - other_root_name_end));
        return *this;
    }

    Path& Path::operator+=(StringView sv)
    {
        m_str.append(sv.data(), sv.size());
        return *this;
    }

    void Path::replace_filename(StringView sv)
    {
        // note: sv may refer to data in m_str, so do everything with one call to replace()
        const char* first = m_str.data();
        const char* last = first + m_str.size();
        const char* filename = find_filename(first, last);
        m_str.replace(m_str.begin() + (filename - first), m_str.end(), sv.data(), sv.size());
    }

    void Path::remove_filename()
    {
        const char* first = m_str.data();
        const char* last = first + m_str.size();
        const char* filename = find_filename(first, last);
        m_str.erase(m_str.begin() + (filename - first), m_str.end());
    }

    // It is not clear if this is intended to collapse multiple slashes, see
    // https://github.com/microsoft/STL/issues/2082
    // This implementation does collapse slashes because we primarily use it for shiny display purposes.
    void Path::make_preferred()
    {
        char* first = &m_str[0];
        char* last = first + m_str.size();
        char* after_root_name = const_cast<char*>(find_root_name_end(first, last));
        char* after_root_directory = std::find_if_not(after_root_name, last, is_slash);
#if defined(_WIN32)
        std::replace(first, after_root_name, '/', '\\');
#endif // _WIN32
        char* target = after_root_name;
        if (after_root_name != after_root_directory)
        {
            *target = preferred_separator;
            ++target;
        }

        first = after_root_directory;
        for (;;)
        {
            char* next_slash = std::find_if(first, last, is_slash);
            auto length = next_slash - first;
            if (first != target)
            {
                memmove(target, first, static_cast<size_t>(length));
            }

            target += length;
            if (next_slash == last)
            {
                m_str.erase(target - m_str.data());
                return;
            }

            *target = preferred_separator;
            ++target;
            first = std::find_if_not(next_slash + 1, last, is_slash);
        }
    }

    Path Path::lexically_normal() const
    {
        // copied from microsoft/STL, stl/inc/filesystem:lexically_normal()
        // relicensed under MIT for the vcpkg repository.

        // N4810 29.11.7.1 [fs.path.generic]/6:
        // "Normalization of a generic format pathname means:"

        // "1. If the path is empty, stop."
        if (m_str.empty())
        {
            return {};
        }

        // "2. Replace each slash character in the root-name with a preferred-separator."
        const auto first = m_str.data();
        const auto last = first + m_str.size();
        const auto root_name_end = find_root_name_end(first, last);

        std::string normalized(first, root_name_end);

#if defined(_WIN32)
        std::replace(normalized.begin(), normalized.end(), '/', '\\');
#endif // _WIN32

        // "3. Replace each directory-separator with a preferred-separator.
        // [ Note: The generic pathname grammar (29.11.7.1) defines directory-separator
        // as one or more slashes and preferred-separators. -end note ]"
        std::list<StringView> lst; // Empty string_view means directory-separator
                                   // that will be normalized to a preferred-separator.
                                   // Non-empty string_view means filename.
        for (auto next = root_name_end; next != last;)
        {
            if (is_slash(*next))
            {
                if (lst.empty() || !lst.back().empty())
                {
                    // collapse one or more slashes and preferred-separators to one empty StringView
                    lst.emplace_back();
                }

                ++next;
            }
            else
            {
                const auto filename_end = std::find_if(next + 1, last, is_slash);
                lst.emplace_back(next, filename_end);
                next = filename_end;
            }
        }

        // "4. Remove each dot filename and any immediately following directory-separator."
        for (auto next = lst.begin(); next != lst.end();)
        {
            if (is_dot(*next))
            {
                next = lst.erase(next); // erase dot filename

                if (next != lst.end())
                {
                    next = lst.erase(next); // erase immediately following directory-separator
                }
            }
            else
            {
                ++next;
            }
        }

        // "5. As long as any appear, remove a non-dot-dot filename immediately followed by a
        // directory-separator and a dot-dot filename, along with any immediately following directory-separator."
        for (auto next = lst.begin(); next != lst.end();)
        {
            auto prev = next;

            // If we aren't going to erase, keep advancing.
            // If we're going to erase, next now points past the dot-dot filename.
            ++next;

            if (is_dot_dot(*prev) && prev != lst.begin() && --prev != lst.begin() && !is_dot_dot(*(--prev)))
            {
                if (next != lst.end())
                { // dot-dot filename has an immediately following directory-separator
                    ++next;
                }

                lst.erase(prev, next); // next remains valid
            }
        }

        // "6. If there is a root-directory, remove all dot-dot filenames
        // and any directory-separators immediately following them.
        // [ Note: These dot-dot filenames attempt to refer to nonexistent parent directories. -end note ]"
        if (!lst.empty() && lst.front().empty())
        { // we have a root-directory
            for (auto next = lst.begin(); next != lst.end();)
            {
                if (is_dot_dot(*next))
                {
                    next = lst.erase(next); // erase dot-dot filename

                    if (next != lst.end())
                    {
                        next = lst.erase(next); // erase immediately following directory-separator
                    }
                }
                else
                {
                    ++next;
                }
            }
        }

        // "7. If the last filename is dot-dot, remove any trailing directory-separator."
        if (lst.size() >= 2 && lst.back().empty() && is_dot_dot(*std::prev(lst.end(), 2)))
        {
            lst.pop_back();
        }

        // Build up normalized by flattening lst.
        for (const auto& elem : lst)
        {
            if (elem.empty())
            {
                normalized.push_back(preferred_separator);
            }
            else
            {
                normalized.append(elem.data(), elem.size());
            }
        }

        // "8. If the path is empty, add a dot."
        if (normalized.empty())
        {
            normalized.push_back('.');
        }

        // "The result of normalization is a path in normal form, which is said to be normalized."
        return normalized;
    }

    bool Path::make_parent_path()
    {
        const auto parent = parent_path();
        if (parent.size() == m_str.size())
        {
            return false;
        }

        m_str.resize(parent.size());
        return true;
    }

    StringView Path::parent_path() const { return parse_parent_path(m_str); }
    StringView Path::filename() const { return parse_filename(m_str); }
    StringView Path::extension() const { return parse_extension(m_str); }
    StringView Path::stem() const { return parse_stem(m_str); }

    bool Path::is_absolute() const { return is_absolute_path(m_str); }

    ReadFilePointer::ReadFilePointer(const Path& file_path, std::error_code& ec) noexcept
    {
#if defined(_WIN32)
        ec.assign(::_wfopen_s(&m_fs, to_stdfs_path(file_path).c_str(), L"rb"), std::generic_category());
#else  // ^^^ _WIN32 / !_WIN32 vvv
        m_fs = ::fopen(file_path.c_str(), "rb");
        if (m_fs)
        {
            ec.clear();
        }
        else
        {
            ec.assign(errno, std::generic_category());
        }
#endif // ^^^ !_WIN32
    }

    WriteFilePointer::WriteFilePointer(const Path& file_path, std::error_code& ec) noexcept
    {
#if defined(_WIN32)
        ec.assign(::_wfopen_s(&m_fs, to_stdfs_path(file_path).c_str(), L"wb"), std::generic_category());
#else  // ^^^ _WIN32 / !_WIN32 vvv
        m_fs = ::fopen(file_path.c_str(), "wb");
        if (m_fs)
        {
            ec.clear();
        }
        else
        {
            ec.assign(errno, std::generic_category());
        }
#endif // ^^^ !_WIN32
    }

    std::vector<std::string> Filesystem::read_lines(const Path& file_path, LineInfo li) const
    {
        std::error_code ec;
        auto maybe_lines = this->read_lines(file_path, ec);
        if (ec)
        {
            exit_filesystem_call_error(li, ec, __func__, {file_path});
        }

        return maybe_lines;
    }

    std::string Filesystem::read_contents(const Path& file_path, LineInfo li) const
    {
        std::error_code ec;
        auto maybe_contents = this->read_contents(file_path, ec);
        if (ec)
        {
            exit_filesystem_call_error(li, ec, __func__, {file_path});
        }

        return maybe_contents;
    }

    Path Filesystem::find_file_recursively_up(const Path& starting_dir, const Path& filename, LineInfo li) const
    {
        std::error_code ec;
        auto result = this->find_file_recursively_up(starting_dir, filename, ec);
        if (ec)
        {
            exit_filesystem_call_error(li, ec, __func__, {starting_dir, filename});
        }

        return result;
    }

    std::vector<Path> Filesystem::get_files_recursive(const Path& dir, LineInfo li) const
    {
        std::error_code ec;
        auto maybe_files = this->get_files_recursive(dir, ec);
        if (ec)
        {
            exit_filesystem_call_error(li, ec, __func__, {dir});
        }

        return maybe_files;
    }

    std::vector<Path> Filesystem::get_files_non_recursive(const Path& dir, LineInfo li) const
    {
        std::error_code ec;
        auto maybe_files = this->get_files_non_recursive(dir, ec);
        if (ec)
        {
            exit_filesystem_call_error(li, ec, __func__, {dir});
        }

        return maybe_files;
    }

    std::vector<Path> Filesystem::get_directories_recursive(const Path& dir, LineInfo li) const
    {
        std::error_code ec;
        auto maybe_directories = this->get_directories_recursive(dir, ec);
        if (ec)
        {
            exit_filesystem_call_error(li, ec, __func__, {dir});
        }

        return maybe_directories;
    }

    std::vector<Path> Filesystem::get_directories_non_recursive(const Path& dir, LineInfo li) const
    {
        std::error_code ec;
        auto maybe_directories = this->get_directories_non_recursive(dir, ec);
        if (ec)
        {
            exit_filesystem_call_error(li, ec, __func__, {dir});
        }

        return maybe_directories;
    }

    std::vector<Path> Filesystem::get_regular_files_recursive(const Path& dir, LineInfo li) const
    {
        std::error_code ec;
        auto maybe_directories = this->get_regular_files_recursive(dir, ec);
        if (ec)
        {
            exit_filesystem_call_error(li, ec, __func__, {dir});
        }

        return maybe_directories;
    }

    std::vector<Path> Filesystem::get_regular_files_non_recursive(const Path& dir, LineInfo li) const
    {
        std::error_code ec;
        auto maybe_directories = this->get_regular_files_non_recursive(dir, ec);
        if (ec)
        {
            exit_filesystem_call_error(li, ec, __func__, {dir});
        }

        return maybe_directories;
    }

    void Filesystem::write_contents(const Path& file_path, const std::string& data, LineInfo li)
    {
        std::error_code ec;
        this->write_contents(file_path, data, ec);
        if (ec)
        {
            exit_filesystem_call_error(li, ec, __func__, {file_path});
        }
    }
    void Filesystem::write_rename_contents(const Path& file_path,
                                           const Path& temp_name,
                                           const std::string& data,
                                           LineInfo li)
    {
        auto temp_path = file_path;
        temp_path.replace_filename(temp_name);
        this->write_contents(temp_path, data, li);
        this->rename(temp_path, file_path, li);
    }
    void Filesystem::write_contents_and_dirs(const Path& file_path, const std::string& data, LineInfo li)
    {
        std::error_code ec;
        this->write_contents_and_dirs(file_path, data, ec);
        if (ec)
        {
            exit_filesystem_call_error(li, ec, __func__, {file_path});
        }
    }
    void Filesystem::rename(const Path& old_path, const Path& new_path, LineInfo li)
    {
        std::error_code ec;
        this->rename(old_path, new_path, ec);
        if (ec)
        {
            exit_filesystem_call_error(li, ec, __func__, {old_path, new_path});
        }
    }
    void Filesystem::rename_with_retry(const Path& old_path, const Path& new_path, LineInfo li)
    {
        std::error_code ec;
        this->rename_with_retry(old_path, new_path, ec);
        if (ec)
        {
            exit_filesystem_call_error(li, ec, __func__, {old_path, new_path});
        }
    }
    void Filesystem::rename_with_retry(const Path& old_path, const Path& new_path, std::error_code& ec)
    {
        this->rename(old_path, new_path, ec);
        using namespace std::chrono_literals;
        for (const auto& delay : {10ms, 100ms, 1000ms})
        {
            if (!ec)
            {
                return;
            }

            std::this_thread::sleep_for(delay);
            this->rename(old_path, new_path, ec);
        }
    }

    bool Filesystem::remove(const Path& target, LineInfo li)
    {
        std::error_code ec;
        auto r = this->remove(target, ec);
        if (ec)
        {
            exit_filesystem_call_error(li, ec, __func__, {target});
        }

        return r;
    }

    bool Filesystem::exists(const Path& target, std::error_code& ec) const
    {
        return vcpkg::exists(this->symlink_status(target, ec));
    }

    bool Filesystem::exists(const Path& target, LineInfo li) const
    {
        std::error_code ec;
        auto result = this->exists(target, ec);
        if (ec)
        {
            exit_filesystem_call_error(li, ec, __func__, {target});
        }

        return result;
    }

    bool Filesystem::is_empty(const Path& target, LineInfo li) const
    {
        std::error_code ec;
        auto result = this->is_empty(target, ec);
        if (ec)
        {
            exit_filesystem_call_error(li, ec, __func__, {target});
        }

        return result;
    }

    bool Filesystem::create_directory(const Path& new_directory, LineInfo li)
    {
        std::error_code ec;
        bool result = this->create_directory(new_directory, ec);
        if (ec)
        {
            exit_filesystem_call_error(li, ec, __func__, {new_directory});
        }

        return result;
    }

    bool Filesystem::create_directories(const Path& new_directory, LineInfo li)
    {
        std::error_code ec;
        bool result = this->create_directories(new_directory, ec);
        if (ec)
        {
            exit_filesystem_call_error(li, ec, __func__, {new_directory});
        }

        return result;
    }

    void Filesystem::create_symlink(const Path& to, const Path& from, LineInfo li)
    {
        std::error_code ec;
        this->create_symlink(to, from, ec);
        if (ec)
        {
            exit_filesystem_call_error(li, ec, __func__, {to, from});
        }
    }

    void Filesystem::create_directory_symlink(const Path& to, const Path& from, LineInfo li)
    {
        std::error_code ec;
        this->create_directory_symlink(to, from, ec);
        if (ec)
        {
            exit_filesystem_call_error(li, ec, __func__, {to, from});
        }
    }

    void Filesystem::create_hard_link(const Path& to, const Path& from, LineInfo li)
    {
        std::error_code ec;
        this->create_hard_link(to, from, ec);
        if (ec)
        {
            exit_filesystem_call_error(li, ec, __func__, {to, from});
        }
    }

    void Filesystem::create_best_link(const Path& to, const Path& from, std::error_code& ec)
    {
        this->create_hard_link(to, from, ec);
        if (!ec) return;
        this->create_symlink(to, from, ec);
        if (!ec) return;
        this->copy_file(from, to, CopyOptions::none, ec);
    }

    void Filesystem::create_best_link(const Path& to, const Path& from, LineInfo li)
    {
        std::error_code ec;
        this->create_best_link(to, from, ec);
        if (ec)
        {
            exit_filesystem_call_error(li, ec, __func__, {to, from});
        }
    }

    void Filesystem::copy_regular_recursive(const Path& source, const Path& destination, LineInfo li)
    {
        std::error_code ec;
        this->copy_regular_recursive(source, destination, ec);
        if (ec)
        {
            exit_filesystem_call_error(li, ec, __func__, {source, destination});
        }
    }

    bool Filesystem::copy_file(const Path& source, const Path& destination, CopyOptions options, LineInfo li)
    {
        std::error_code ec;
        const bool result = this->copy_file(source, destination, options, ec);
        if (ec)
        {
            exit_filesystem_call_error(li, ec, __func__, {source, destination});
        }

        return result;
    }

    void Filesystem::copy_symlink(const Path& source, const Path& destination, LineInfo li)
    {
        std::error_code ec;
        this->copy_symlink(source, destination, ec);
        if (ec)
        {
            exit_filesystem_call_error(li, ec, __func__, {source, destination});
        }
    }

    FileType Filesystem::status(const Path& target, vcpkg::LineInfo li) const noexcept
    {
        std::error_code ec;
        auto result = this->status(target, ec);
        if (ec)
        {
            exit_filesystem_call_error(li, ec, __func__, {target});
        }

        return result;
    }

    FileType Filesystem::symlink_status(const Path& target, vcpkg::LineInfo li) const noexcept
    {
        std::error_code ec;
        auto result = this->symlink_status(target, ec);
        if (ec)
        {
            exit_filesystem_call_error(li, ec, __func__, {target});
        }

        return result;
    }

    void Filesystem::write_lines(const Path& file_path, const std::vector<std::string>& lines, LineInfo li)
    {
        std::error_code ec;
        this->write_lines(file_path, lines, ec);
        if (ec)
        {
            exit_filesystem_call_error(li, ec, __func__, {file_path});
        }
    }

    void Filesystem::remove_all(const Path& base, LineInfo li)
    {
        std::error_code ec;
        Path failure_point;

        this->remove_all(base, ec, failure_point);

        if (ec)
        {
            Checks::exit_with_message(
                li, "Failure to remove_all(\"%s\") due to file \"%s\": %s", base, failure_point, ec.message());
        }
    }

    void Filesystem::remove_all(const Path& base, std::error_code& ec)
    {
        Path failure_point;
        this->remove_all(base, ec, failure_point);
    }

    void Filesystem::remove_all_inside(const Path& base, std::error_code& ec, Path& failure_point)
    {
        for (auto&& subdir : this->get_directories_non_recursive(base, ec))
        {
            if (ec)
            {
                return;
            }

            this->remove_all(subdir, ec, failure_point);
        }

        if (ec)
        {
            return;
        }

        for (auto&& file : this->get_files_non_recursive(base, ec))
        {
            this->remove(file, ec);
            if (ec)
            {
                failure_point = file;
                return;
            }
        }
    }

    void Filesystem::remove_all_inside(const Path& base, LineInfo li)
    {
        std::error_code ec;
        Path failure_point;

        this->remove_all_inside(base, ec, failure_point);

        if (ec)
        {
            Checks::exit_with_message(
                li, "Failure to remove_all_inside(\"%s\") due to file \"%s\": %s", base, failure_point, ec.message());
        }
    }

    void Filesystem::remove_all_inside(const Path& base, std::error_code& ec)
    {
        Path failure_point;
        this->remove_all_inside(base, ec, failure_point);
    }

    Path Filesystem::absolute(const Path& target, LineInfo li) const
    {
        std::error_code ec;
        const auto result = this->absolute(target, ec);
        if (ec)
        {
            exit_filesystem_call_error(li, ec, __func__, {target});
        }

        return result;
    }

    Path Filesystem::almost_canonical(const Path& target, LineInfo li) const
    {
        std::error_code ec;
        const auto result = this->almost_canonical(target, ec);
        if (ec)
        {
            exit_filesystem_call_error(li, ec, __func__, {target});
        }

        return result;
    }

    Path Filesystem::current_path(LineInfo li) const
    {
        std::error_code ec;
        const auto result = this->current_path(ec);
        if (ec)
        {
            exit_filesystem_call_error(li, ec, __func__, {});
        }

        return result;
    }
    void Filesystem::current_path(const Path& new_current_path, LineInfo li)
    {
        std::error_code ec;
        this->current_path(new_current_path, ec);
        if (ec)
        {
            exit_filesystem_call_error(li, ec, __func__, {new_current_path});
        }
    }

    std::unique_ptr<IExclusiveFileLock> Filesystem::take_exclusive_file_lock(const Path& lockfile, LineInfo li)
    {
        std::error_code ec;
        auto sh = this->take_exclusive_file_lock(lockfile, ec);
        if (ec)
        {
            exit_filesystem_call_error(li, ec, __func__, {lockfile});
        }

        return sh;
    }

    std::unique_ptr<IExclusiveFileLock> Filesystem::try_take_exclusive_file_lock(const Path& lockfile, LineInfo li)
    {
        std::error_code ec;
        auto sh = this->try_take_exclusive_file_lock(lockfile, ec);
        if (ec)
        {
            exit_filesystem_call_error(li, ec, __func__, {lockfile});
        }

        return sh;
    }

    ReadFilePointer Filesystem::open_for_read(const Path& file_path, LineInfo li) const
    {
        std::error_code ec;
        auto ret = this->open_for_read(file_path, ec);
        if (ec)
        {
            exit_filesystem_call_error(li, ec, __func__, {file_path});
        }

        return ret;
    }

    WriteFilePointer Filesystem::open_for_write(const Path& file_path, LineInfo li)
    {
        std::error_code ec;
        auto ret = this->open_for_write(file_path, ec);
        if (ec)
        {
            exit_filesystem_call_error(li, ec, __func__, {file_path});
        }

        return ret;
    }

    struct RealFilesystem final : Filesystem
    {
        virtual std::string read_contents(const Path& file_path, std::error_code& ec) const override
        {
            StatsTimer t(g_us_filesystem_stats);
            ReadFilePointer file{file_path, ec};
            if (ec)
            {
                Debug::print("Failed to open: ", file_path, '\n');
                return std::string();
            }

            std::string output;
            constexpr std::size_t buffer_size = 1024 * 32;
            char buffer[buffer_size];
            do
            {
                const auto this_read = file.read(buffer, 1, buffer_size);
                if (this_read != 0)
                {
                    output.append(buffer, this_read);
                }
                else if ((ec = file.error()))
                {
                    return std::string();
                }
            } while (!file.eof());

            return output;
        }
        virtual std::vector<std::string> read_lines(const Path& file_path, std::error_code& ec) const override
        {
            StatsTimer t(g_us_filesystem_stats);
            ReadFilePointer file{file_path, ec};
            if (ec)
            {
                Debug::print("Failed to open: ", file_path, '\n');
                return std::vector<std::string>();
            }

            Strings::LinesCollector output;
            constexpr std::size_t buffer_size = 1024 * 32;
            char buffer[buffer_size];
            do
            {
                const auto this_read = file.read(buffer, 1, buffer_size);
                if (this_read != 0)
                {
                    output.on_data({buffer, this_read});
                }
                else if ((ec = file.error()))
                {
                    return std::vector<std::string>();
                }
            } while (!file.eof());

            return output.extract();
        }

        virtual Path find_file_recursively_up(const Path& starting_dir,
                                              const Path& filename,
                                              std::error_code& ec) const override
        {
            Path current_dir = starting_dir;
            if (exists(current_dir / filename, ec))
            {
                return current_dir;
            }

            if (ec)
            {
                current_dir.clear();
                return current_dir;
            }

            int counter = 10000;
            for (;;)
            {
                if (!current_dir.make_parent_path())
                {
                    current_dir.clear();
                    return current_dir;
                }

                const auto candidate = current_dir / filename;
                if (exists(candidate, ec))
                {
                    return current_dir;
                }

                if (ec)
                {
                    current_dir.clear();
                    return current_dir;
                }

                --counter;
                Checks::check_exit(VCPKG_LINE_INFO,
                                   counter > 0,
                                   "infinite loop encountered while trying to find_file_recursively_up()");
            }
        }

#if defined(_WIN32)
        template<class Iter>
        static std::vector<Path> get_files_impl(const Path& dir, std::error_code& ec)
        {
            std::vector<Path> ret;
            Iter b(to_stdfs_path(dir), ec), e{};
            if (ec)
            {
                translate_not_found_to_success(ec);
            }
            else
            {
                while (b != e)
                {
                    ret.push_back(from_stdfs_path(b->path()));
                    b.increment(ec);
                    if (ec)
                    {
                        ret.clear();
                        break;
                    }
                }
            }

            return ret;
        }

        virtual std::vector<Path> get_files_recursive(const Path& dir, std::error_code& ec) const override
        {
            return get_files_impl<stdfs::recursive_directory_iterator>(dir, ec);
        }

        virtual std::vector<Path> get_files_non_recursive(const Path& dir, std::error_code& ec) const override
        {
            return get_files_impl<stdfs::directory_iterator>(dir, ec);
        }

        template<class Iter>
        static std::vector<Path> get_directories_impl(const Path& dir, std::error_code& ec)
        {
            std::vector<Path> ret;
            Iter b(to_stdfs_path(dir), ec), e{};
            if (ec)
            {
                translate_not_found_to_success(ec);
            }
            else
            {
                while (b != e)
                {
                    if (b->is_directory(ec))
                    {
                        ret.push_back(from_stdfs_path(b->path()));
                    }

                    if (ec)
                    {
                        ret.clear();
                        break;
                    }

                    b.increment(ec);
                    if (ec)
                    {
                        ret.clear();
                        break;
                    }
                }
            }

            return ret;
        }

        virtual std::vector<Path> get_directories_recursive(const Path& dir, std::error_code& ec) const override
        {
            return get_directories_impl<stdfs::recursive_directory_iterator>(dir, ec);
        }

        virtual std::vector<Path> get_directories_non_recursive(const Path& dir, std::error_code& ec) const override
        {
            return get_directories_impl<stdfs::directory_iterator>(dir, ec);
        }

        template<class Iter>
        static std::vector<Path> get_regular_files_impl(const Path& dir, std::error_code& ec)
        {
            std::vector<Path> ret;
            Iter b(to_stdfs_path(dir), ec), e{};
            if (ec)
            {
                translate_not_found_to_success(ec);
            }
            else
            {
                while (b != e)
                {
                    if (b->is_regular_file(ec))
                    {
                        ret.push_back(from_stdfs_path(b->path()));
                    }

                    if (ec)
                    {
                        ret.clear();
                        break;
                    }

                    b.increment(ec);
                    if (ec)
                    {
                        ret.clear();
                        break;
                    }
                }
            }

            return ret;
        }

        virtual std::vector<Path> get_regular_files_recursive(const Path& dir, std::error_code& ec) const override
        {
            return get_regular_files_impl<stdfs::recursive_directory_iterator>(dir, ec);
        }

        virtual std::vector<Path> get_regular_files_non_recursive(const Path& dir, std::error_code& ec) const override
        {
            return get_regular_files_impl<stdfs::directory_iterator>(dir, ec);
        }
#else  // ^^^ _WIN32 // !_WIN32 vvv
        static void insert_if_stat_matches(std::vector<Path>& result,
                                           const Path& full,
                                           struct stat* s,
                                           bool want_directories,
                                           bool want_regular_files,
                                           bool want_other)
        {
            if (S_ISDIR(s->st_mode))
            {
                if (!want_directories)
                {
                    return;
                }
            }
            else if (S_ISREG(s->st_mode))
            {
                if (!want_regular_files)
                {
                    return;
                }
            }
            else
            {
                if (!want_other)
                {
                    return;
                }
            }

            result.push_back(full);
        }

        static void get_files_recursive_impl(std::vector<Path>& result,
                                             const Path& base,
                                             std::error_code& ec,
                                             bool want_directories,
                                             bool want_regular_files,
                                             bool want_other)
        {
            ReadDirOp op{base, ec};
            if (ec)
            {
                translate_not_found_to_success(ec);
            }
            else
            {
                for (;;)
                {
                    errno = 0;
                    auto entry = op.read();
                    if (!entry)
                    {
                        if (errno != 0)
                        {
                            ec.assign(errno, std::generic_category());
                            return;
                        }

                        // no more entries left
                        break;
                    }

                    if (is_dot_or_dot_dot(entry->d_name))
                    {
                        continue;
                    }

                    const auto full = base / entry->d_name;
                    const auto entry_dtype = get_d_type(entry);
                    struct stat s;
                    struct stat ls;
                    switch (entry_dtype)
                    {
                        case PosixDType::Directory:
                            if (want_directories)
                            {
                                // push results before recursion to get outer entries first
                                result.push_back(full);
                            }

                            get_files_recursive_impl(
                                result, full, ec, want_directories, want_regular_files, want_other);
                            if (ec)
                            {
                                return;
                            }

                            break;
                        case PosixDType::Regular:
                            if (want_regular_files)
                            {
                                result.push_back(full);
                            }

                            break;

                        case PosixDType::Fifo:
                        case PosixDType::Socket:
                        case PosixDType::CharacterDevice:
                        case PosixDType::BlockDevice:
                            if (want_other)
                            {
                                result.push_back(full);
                            }

                            break;

                        case PosixDType::Unknown:
                        default:
                            if (::lstat(full.c_str(), &ls) != 0)
                            {
                                if (errno == ENOENT || errno == ENOTDIR)
                                {
                                    ec.clear();
                                }
                                else
                                {
                                    result.clear();
                                    ec.assign(errno, std::generic_category());
                                }

                                return;
                            }

                            if (S_ISLNK(ls.st_mode))
                            {
                                if (want_directories && want_regular_files && want_other)
                                {
                                    // skip extra stat syscall since we want everything
                                    result.push_back(full);
                                }
                                else
                                {
                                    if (::stat(full.c_str(), &s) != 0)
                                    {
                                        if (errno == ENOENT || errno == ENOTDIR)
                                        {
                                            ec.clear();
                                        }
                                        else
                                        {
                                            result.clear();
                                            ec.assign(errno, std::generic_category());
                                        }

                                        return;
                                    }

                                    insert_if_stat_matches(
                                        result, full, &s, want_directories, want_regular_files, want_other);
                                }
                            }
                            else
                            {
                                // push results before recursion to get outer entries first
                                insert_if_stat_matches(
                                    result, full, &ls, want_directories, want_regular_files, want_other);
                            }

                            // recursion check doesn't follow symlinks:
                            if (S_ISDIR(ls.st_mode))
                            {
                                get_files_recursive_impl(
                                    result, full, ec, want_directories, want_regular_files, want_other);
                            }
                            break;
                    }
                }
            }
        }

        // Selector is a function taking (PosixDType dtype, const Path&) and returning bool
        // (This is similar to the recursive version, but the non-recursive version doesn't need to do stat calls
        // so selector needs to do them if it wants.)
        static void get_files_non_recursive_impl(std::vector<Path>& result,
                                                 const Path& base,
                                                 std::error_code& ec,
                                                 bool (*selector)(PosixDType, const Path&))
        {
            ReadDirOp op{base, ec};
            if (ec)
            {
                translate_not_found_to_success(ec);
            }
            else
            {
                for (;;)
                {
                    errno = 0;
                    auto entry = op.read();
                    if (!entry)
                    {
                        if (errno != 0)
                        {
                            ec.assign(errno, std::generic_category());
                            return;
                        }

                        // no more entries left
                        break;
                    }

                    if (is_dot_or_dot_dot(entry->d_name))
                    {
                        continue;
                    }

                    auto full = base / entry->d_name;
                    if (selector(get_d_type(entry), full))
                    {
                        result.push_back(std::move(full));
                    }
                }
            }
        }

        virtual std::vector<Path> get_files_recursive(const Path& dir, std::error_code& ec) const override
        {
            std::vector<Path> result;
            get_files_recursive_impl(result, dir, ec, true, true, true);
            return result;
        }

        virtual std::vector<Path> get_files_non_recursive(const Path& dir, std::error_code& ec) const override
        {
            std::vector<Path> result;
            get_files_non_recursive_impl(result, dir, ec, [](PosixDType, const Path&) { return true; });
            return result;
        }

        virtual std::vector<Path> get_directories_recursive(const Path& dir, std::error_code& ec) const override
        {
            std::vector<Path> result;
            get_files_recursive_impl(result, dir, ec, true, false, false);

            return result;
        }

        virtual std::vector<Path> get_directories_non_recursive(const Path& dir, std::error_code& ec) const override
        {
            std::vector<Path> result;
            get_files_non_recursive_impl(result, dir, ec, [](PosixDType dtype, const Path& p) {
                switch (dtype)
                {
                    case PosixDType::Directory: return true;
                    case PosixDType::BlockDevice:
                    case PosixDType::CharacterDevice:
                    case PosixDType::Fifo:
                    case PosixDType::Regular:
                    case PosixDType::Socket: return false;
                    case PosixDType::Link:
                    case PosixDType::Unknown:
                    default: return stat_is_directory(p.c_str());
                }
            });

            return result;
        }

        virtual std::vector<Path> get_regular_files_recursive(const Path& dir, std::error_code& ec) const override
        {
            std::vector<Path> result;
            get_files_recursive_impl(result, dir, ec, false, true, false);
            return result;
        }

        virtual std::vector<Path> get_regular_files_non_recursive(const Path& dir, std::error_code& ec) const override
        {
            std::vector<Path> result;
            get_files_non_recursive_impl(result, dir, ec, [](PosixDType dtype, const Path& p) {
                switch (dtype)
                {
                    case PosixDType::Regular: return true;
                    case PosixDType::BlockDevice:
                    case PosixDType::CharacterDevice:
                    case PosixDType::Fifo:
                    case PosixDType::Directory:
                    case PosixDType::Socket: return false;
                    case PosixDType::Link:
                    case PosixDType::Unknown:
                    default: return stat_is_regular_file(p.c_str());
                }
            });

            return result;
        }
#endif // ^^^ !_WIN32

        virtual void write_lines(const Path& file_path,
                                 const std::vector<std::string>& lines,
                                 std::error_code& ec) override
        {
            vcpkg::WriteFilePointer output{file_path, ec};
            if (!ec)
            {
                for (const auto& line : lines)
                {
                    if (output.write(line.c_str(), 1, line.size()) != line.size() || output.put('\n') != '\n')
                    {
                        ec.assign(errno, std::generic_category());
                        return;
                    }
                }
            }
        }
        virtual void rename(const Path& old_path, const Path& new_path, std::error_code& ec) override
        {
#if defined(_WIN32)
            stdfs::rename(to_stdfs_path(old_path), to_stdfs_path(new_path), ec);
#else  // ^^^ _WIN32 // !_WIN32 vvv
            if (::rename(old_path.c_str(), new_path.c_str()) == 0)
            {
                ec.clear();
            }
            else
            {
                ec.assign(errno, std::generic_category());
            }
#endif // ^^^ !_WIN32
        }
        virtual void rename_or_copy(const Path& old_path,
                                    const Path& new_path,
                                    StringLiteral temp_suffix,
                                    std::error_code& ec) override
        {
            this->rename(old_path, new_path, ec);
            (void)temp_suffix;
#if !defined(_WIN32)
            if (ec)
            {
                auto dst = new_path + temp_suffix;
                this->copy_file(old_path, dst, CopyOptions::overwrite_existing, ec);
                if (ec)
                {
                    return;
                }

                this->rename(dst, new_path, ec);
                if (ec)
                {
                    return;
                }

                this->remove(old_path, ec);
            }
#endif // ^^^ !defined(_WIN32)
        }

        virtual bool remove(const Path& target, std::error_code& ec) override
        {
#if defined(_WIN32)
            auto as_stdfs = to_stdfs_path(target);
            bool result = stdfs::remove(as_stdfs, ec);
            if (ec && ec == std::error_code(ERROR_ACCESS_DENIED, std::system_category()))
            {
                remove_file_attribute_readonly(as_stdfs, ec);
                if (ec)
                {
                    return false;
                }

                return stdfs::remove(as_stdfs, ec);
            }

            return result;
#else  // ^^^ _WIN32 // !_WIN32 vvv
            if (::remove(target.c_str()) == 0)
            {
                ec.clear();
                return true;
            }

            const auto remove_errno = errno;
            if (remove_errno == ENOENT || remove_errno == ENOTDIR)
            {
                ec.clear();
            }
            else
            {
                ec.assign(remove_errno, std::generic_category());
            }

            return false;
#endif // _WIN32
        }
        virtual void remove_all(const Path& base, std::error_code& ec, Path& failure_point) override
        {
            vcpkg_remove_all(base, ec, failure_point);
        }

        virtual bool is_directory(const Path& target) const override
        {
#if defined(_WIN32)
            return stdfs::is_directory(to_stdfs_path(target));
#else  // ^^^ _WIN32 // !_WIN32 vvv
            struct stat s;
            if (::stat(target.c_str(), &s) != 0)
            {
                return false;
            }

            return S_ISDIR(s.st_mode);
#endif // ^^^ !_WIN32
        }
        virtual bool is_regular_file(const Path& target) const override
        {
#if defined(_WIN32)
            return stdfs::is_regular_file(to_stdfs_path(target));
#else  // ^^^ _WIN32 // !_WIN32 vvv
            struct stat s;
            if (::stat(target.c_str(), &s) != 0)
            {
                return false;
            }

            return S_ISREG(s.st_mode);
#endif // ^^^ !_WIN32
        }
        virtual bool is_empty(const Path& target, std::error_code& ec) const override
        {
#if defined(_WIN32)
            return stdfs::is_empty(to_stdfs_path(target), ec);
#else  // ^^^ _WIN32 // !_WIN32 vvv
            struct stat st;
            if (::stat(target.c_str(), &st) != 0)
            {
                ec.assign(errno, std::generic_category());
                return false;
            }

            if (S_ISDIR(st.st_mode))
            {
                ReadDirOp rdo{target, ec};
                if (ec) return false;
                const dirent* entry;
                do
                {
                    entry = rdo.read(ec);
                    if (ec)
                    {
                        return false;
                    }
                    if (entry == nullptr)
                    {
                        return true;
                    }
                } while (is_dot_or_dot_dot(entry->d_name));
                return false;
            }

            return st.st_size == 0;
#endif // ^^^ !_WIN32
        }

#if !defined(_WIN32)
        static int posix_create_directory(const char* new_directory)
        {
            if (::mkdir(new_directory, 0777) == 0)
            {
                return -1;
            }

            auto mkdir_error = errno;
            if (mkdir_error == EEXIST)
            {
                struct stat s;
                if (::stat(new_directory, &s) == 0)
                {
                    if (S_ISDIR(s.st_mode))
                    {
                        return 0;
                    }
                }
                else
                {
                    return errno;
                }
            }

            return mkdir_error;
        }
#endif // ^^^ !_WIN32

        virtual bool create_directory(const Path& new_directory, std::error_code& ec) override
        {
#if defined(_WIN32)
            return stdfs::create_directory(to_stdfs_path(new_directory), ec);
#else  // ^^^ _WIN32 // !_WIN32 vvv
            auto attempt = posix_create_directory(new_directory.c_str());
            if (attempt <= 0)
            {
                ec.clear();
                return attempt != 0;
            }

            ec.assign(attempt, std::generic_category());
            return false;
#endif // ^^^ !_WIN32
        }
        virtual bool create_directories(const Path& new_directory, std::error_code& ec) override
        {
            StatsTimer t(g_us_filesystem_stats);
#if defined(_WIN32)
            return stdfs::create_directories(to_stdfs_path(new_directory), ec);
#else // ^^^ _WIN32 // !_WIN32 vvv
            ec.clear();
            if (new_directory.empty())
            {
                return false;
            }

            const auto& new_str = new_directory.native();
            auto first = new_str.begin();
            const auto last = new_str.end();
            std::string this_create;
            // establish the !is_slash(*first) loop invariant
            if (is_slash(*first))
            {
                this_create.push_back('/');
                first = std::find_if_not(first, last, is_slash); // collapse multiple slashes
            }

            bool last_mkdir_created = false;
            for (;;)
            {
                if (first == last)
                {
                    return last_mkdir_created;
                }

                assert(!is_slash(*first));
                const auto next_slash = std::find_if(first, last, is_slash);
                this_create.append(first, next_slash);
                const auto attempt = posix_create_directory(this_create.c_str());
                if (attempt > 0)
                {
                    ec.assign(attempt, std::generic_category());
                    return false;
                }

                last_mkdir_created = attempt != 0;
                if (next_slash == last)
                {
                    return last_mkdir_created;
                }

                this_create.push_back('/');
                first = std::find_if_not(next_slash, last, is_slash); // collapse multiple slashes
            }

#endif // _WIN32
        }

#if !defined(_WIN32)
        static void posix_create_symlink(const Path& to, const Path& from, std::error_code& ec)
        {
            if (::symlink(to.c_str(), from.c_str()) == 0)
            {
                ec.clear();
            }
            else
            {
                ec.assign(errno, std::generic_category());
            }
        }
#endif // !_WIN32

        virtual void create_symlink(const Path& to, const Path& from, std::error_code& ec) override
        {
#if defined(_WIN32)
            stdfs::create_symlink(to_stdfs_path(to), to_stdfs_path(from), ec);
#else  // ^^^ _WIN32 // !_WIN32 vvv
            posix_create_symlink(to, from, ec);
#endif // _WIN32
        }
        virtual void create_directory_symlink(const Path& to, const Path& from, std::error_code& ec) override
        {
#if defined(_WIN32)
            stdfs::create_directory_symlink(to_stdfs_path(to), to_stdfs_path(from), ec);
#else  // ^^^ _WIN32 // !_WIN32 vvv
            posix_create_symlink(to, from, ec);
#endif // _WIN32
        }
        virtual void create_hard_link(const Path& to, const Path& from, std::error_code& ec) override
        {
#if defined(_WIN32)
            stdfs::create_hard_link(to_stdfs_path(to), to_stdfs_path(from), ec);
#else  // ^^^ _WIN32 // !_WIN32 vvv
            if (::link(from.c_str(), to.c_str()) == 0)
            {
                ec.clear();
            }
            else
            {
                ec.assign(errno, std::generic_category());
            }
#endif // _WIN32
        }

        virtual void copy_regular_recursive(const Path& source, const Path& destination, std::error_code& ec) override
        {
            StatsTimer t(g_us_filesystem_stats);
            copy_regular_recursive_impl(source, destination, ec);
        }
        void copy_regular_recursive_impl(const Path& source, const Path& destination, std::error_code& ec)
        {
#if defined(_WIN32)
            stdfs::copy(to_stdfs_path(source), to_stdfs_path(destination), stdfs::copy_options::recursive, ec);
#else  // ^^^ _WIN32 // !_WIN32 vvv
            ReadDirOp rd{source.c_str(), ec};
            if (ec)
            {
                if (ec == std::errc::not_a_directory)
                {
                    this->copy_file(source, destination, CopyOptions::none, ec);
                }

                return;
            }

            this->create_directory(destination, ec);
            Path source_entry_name;
            Path destination_entry_name;
            const dirent* entry;
            // the !ec check is either for the create_directory above on the first iteration, or for the most
            // recent copy_file or copy_regular_recursive on subsequent iterations
            while (!ec && (entry = rd.read(ec)))
            {
                source_entry_name = source / entry->d_name;
                destination_entry_name = destination / entry->d_name;
                if (get_d_type(entry) == PosixDType::Regular)
                {
                    this->copy_file(source_entry_name, destination_entry_name, CopyOptions::none, ec);
                }
                else
                {
                    this->copy_regular_recursive_impl(source_entry_name, destination_entry_name, ec);
                }
            }
#endif // ^^^ !_WIN32
        }

        virtual bool copy_file(const Path& source,
                               const Path& destination,
                               CopyOptions options,
                               std::error_code& ec) override
        {
#if defined(_WIN32)
            DWORD last_error;
            auto wide_source = Strings::to_utf16(source.native());
            auto wide_destination = Strings::to_utf16(destination.native());
            if (options != CopyOptions::overwrite_existing)
            {
                if (::CopyFileW(wide_source.c_str(), wide_destination.c_str(), TRUE))
                {
                    ec.clear();
                    return true;
                }

                last_error = GetLastError();
                if (last_error != ERROR_FILE_EXISTS || options == CopyOptions::none)
                {
                    ec.assign(static_cast<int>(last_error), std::system_category());
                    return false;
                }

                // open handles to both files in exclusive mode to implement the equivalent() check
                FileHandle source_handle(wide_source.c_str(), FILE_READ_DATA, 0, OPEN_EXISTING, 0, ec);
                if (ec)
                {
                    return false;
                }
                FileHandle destination_handle(wide_destination.c_str(), FILE_WRITE_DATA, 0, OPEN_EXISTING, 0, ec);
                return false;
            }

            if (::CopyFileW(wide_source.c_str(), wide_destination.c_str(), FALSE))
            {
                ec.clear();
                return true;
            }

            last_error = GetLastError();
            ec.assign(static_cast<int>(last_error), std::system_category());
            return false;
#else // ^^^ _WIN32 // !_WIN32 vvv
            PosixFd source_fd{source.c_str(), O_RDONLY, ec};
            if (ec)
            {
                return false;
            }

            struct stat source_stat;
            source_fd.fstat(&source_stat, ec);
            if (ec)
            {
                return false;
            }

            if (!S_ISREG(source_stat.st_mode))
            {
                // N4861 [fs.op.copy.file]/4: "report an error if"
                // is_regular_file(from) is false
                ec = std::make_error_code(std::errc::invalid_argument);
                return false;
            }

            int open_options = O_WRONLY | O_CREAT;
            if (options != CopyOptions::overwrite_existing)
            {
                // the standard wording suggests that we should create a file through a broken symlink which would
                // forbid use of O_EXCL. However, implementations like boost::copy_file don't do this and doing it
                // would introduce more potential for TOCTOU bugs
                open_options |= O_EXCL;
            }

            const mode_t open_mode = source_stat.st_mode | 0222; // ensure the file is created writable
            PosixFd destination_fd{destination.c_str(), open_options, open_mode, ec};
            if (ec)
            {
                if (options == CopyOptions::skip_existing && ec == std::errc::file_exists)
                {
                    ec.clear();
                }

                return false;
            }

            struct stat destination_stat;
            destination_fd.fstat(&destination_stat, ec);
            if (ec)
            {
                return false;
            }
            else if (!S_ISREG(destination_stat.st_mode))
            {
                ec = std::make_error_code(std::errc::invalid_argument);
                return false;
            }

            if (source_stat.st_dev == destination_stat.st_dev && source_stat.st_ino == destination_stat.st_ino)
            {
                // /4 exists(to) is true and equivalent(from, to) is true
                ec = std::make_error_code(std::errc::device_or_resource_busy);
                return false;
            }

            if (options == CopyOptions::overwrite_existing)
            {
                destination_fd.ftruncate(0, ec);
                if (ec) return false;
            }

#if defined(__linux__)
            off_t bytes = 0;
            if (sendfile(destination_fd.get(), source_fd.get(), &bytes, source_stat.st_size) == -1)
            {
                ec.assign(errno, std::generic_category());
                return false;
            }

            destination_fd.fchmod(source_stat.st_mode, ec);
            return !ec;
#elif defined(__APPLE__)
            if (fcopyfile(source_fd.get(), destination_fd.get(), 0, COPYFILE_ALL) == -1)
            {
                ec.assign(errno, std::generic_category());
                return false;
            }

            // fcopyfile copies the mode so no need to fchmod here
            return true;
#else  // ^^^ defined(__APPLE__) // !(defined(__APPLE__) || defined(__linux__)) vvv
            constexpr std::size_t buffer_length = 4096;
            unsigned char buffer[buffer_length];
            while (auto read_bytes = source_fd.read(buffer, buffer_length))
            {
                if (read_bytes == -1)
                {
                    ec.assign(errno, std::generic_category());
                    return false;
                }

                auto remaining = read_bytes;
                while (remaining > 0)
                {
                    auto write_result = destination_fd.write(buffer, remaining);
                    if (write_result == -1)
                    {
                        ec.assign(errno, std::generic_category());
                        return false;
                    }

                    remaining -= write_result;
                }
            }

            destination_fd.fchmod(source_stat.st_mode, ec);
            return !ec;
#endif // ^^^ !(defined(__APPLE__) || defined(__linux__))
#endif // ^^^ !_WIN32
        }

        virtual void copy_symlink(const Path& source, const Path& destination, std::error_code& ec) override
        {
#if defined(_WIN32)
            stdfs::copy_symlink(to_stdfs_path(source), to_stdfs_path(destination), ec);
#else  // ^^^ _WIN32 // !_WIN32 vvv
            std::string buffer;
            buffer.resize(PATH_MAX);
            for (;;)
            {
                ssize_t result = ::readlink(source.c_str(), &buffer[0], buffer.size());
                if (result < 0)
                {
                    ec.assign(errno, std::generic_category());
                    return;
                }

                if (static_cast<size_t>(result) == buffer.size())
                {
                    // we might not have used a big enough buffer, grow and retry
                    buffer.append(buffer.size(), '\0');
                    continue;
                }

                buffer.resize(static_cast<size_t>(result));
                // "Conforming applications should not assume that the returned contents of the
                // symbolic link are null-terminated." -- but std::string already adds the extra
                // null we need
                break;
            }

            if (::symlink(buffer.c_str(), destination.c_str()) == 0)
            {
                ec.clear();
            }
            else
            {
                ec.assign(errno, std::generic_category());
            }
#endif // ^^^ !_WIN32
        }

        virtual FileType status(const Path& target, std::error_code& ec) const override
        {
#if defined(_WIN32)
            auto result = stdfs::status(to_stdfs_path(target), ec);
            translate_not_found_to_success(ec);
            return convert_file_type(result.type());
#else  // ^^^ _WIN32 // !_WIN32 vvv
            struct stat s;
            if (::stat(target.c_str(), &s) == 0)
            {
                ec.clear();
                return posix_translate_stat_mode_to_file_type(s.st_mode);
            }

            if (errno == ENOENT || errno == ENOTDIR)
            {
                ec.clear();
                return FileType::not_found;
            }

            ec.assign(errno, std::generic_category());
            return FileType::unknown;
#endif // ^^^ !_WIN32
        }
        virtual FileType symlink_status(const Path& target, std::error_code& ec) const override
        {
#if defined(_WIN32)
            auto result = stdfs::symlink_status(to_stdfs_path(target), ec);
            translate_not_found_to_success(ec);
            return convert_file_type(result.type());
#else  // ^^^ _WIN32 // !_WIN32 vvv
            struct stat s;
            if (::lstat(target.c_str(), &s) == 0)
            {
                ec.clear();
                return posix_translate_stat_mode_to_file_type(s.st_mode);
            }

            if (errno == ENOENT || errno == ENOTDIR)
            {
                ec.clear();
                return FileType::not_found;
            }

            ec.assign(errno, std::generic_category());
            return FileType::unknown;
#endif // ^^^ !_WIN32
        }
        virtual void write_contents(const Path& file_path, const std::string& data, std::error_code& ec) override
        {
            StatsTimer t(g_us_filesystem_stats);
            auto f = open_for_write(file_path, ec);
            if (!ec)
            {
                auto count = f.write(data.data(), 1, data.size());
                if (count != data.size())
                {
                    ec.assign(errno, std::generic_category());
                }
            }
        }

        virtual void write_contents_and_dirs(const Path& file_path,
                                             const std::string& data,
                                             std::error_code& ec) override
        {
            write_contents(file_path, data, ec);
            if (ec)
            {
                create_directories(file_path.parent_path(), ec);
                if (ec)
                {
                    return;
                }
                write_contents(file_path, data, ec);
            }
        }

        virtual Path absolute(const Path& target, std::error_code& ec) const override
        {
#if defined(_WIN32)
            return from_stdfs_path(stdfs::absolute(to_stdfs_path(target), ec));
#else  // ^^^ _WIN32 / !_WIN32 vvv
            if (target.is_absolute())
            {
                return target;
            }
            else
            {
                auto current_path = this->current_path(ec);
                if (ec) return Path();
                return std::move(current_path) / target;
            }
#endif // ^^^ !_WIN32
        }

        virtual Path almost_canonical(const Path& target, std::error_code& ec) const override
        {
            auto result = this->absolute(target, ec);
            if (ec)
            {
                return result;
            }

            result = result.lexically_normal();
#if defined(_WIN32)
            result = vcpkg::win32_fix_path_case(result);
#endif // _WIN32
            return result;
        }

        virtual Path current_path(std::error_code& ec) const override
        {
#if defined(_WIN32)
            return from_stdfs_path(stdfs::current_path(ec));
#else  // ^^^ _WIN32 // !_WIN32
            std::string buf;
            buf.resize(PATH_MAX);
            for (;;)
            {
                if (getcwd(&buf[0], buf.size() + 1) != nullptr)
                {
                    buf.resize(strlen(buf.c_str()));
                    ec.clear();
                    break;
                }

                if (errno != ERANGE)
                {
                    ec.assign(errno, std::generic_category());
                    buf.clear();
                    break;
                }

                // the current working directory is too big for the size of the string; resize and try again.
                buf.append(PATH_MAX, '\0');
            }

            return Path{std::move(buf)};
#endif // ^^^ !_WIN32
        }
        virtual void current_path(const Path& new_current_path, std::error_code& ec) override
        {
#if defined(_WIN32)
            stdfs::current_path(to_stdfs_path(new_current_path), ec);
#else  // ^^^ _WIN32 // !_WIN32 vvv
            if (chdir(new_current_path.c_str()) == 0)
            {
                ec.clear();
                return;
            }

            ec.assign(errno, std::generic_category());
#endif // ^^^ !_WIN32
        }

        struct ExclusiveFileLock : IExclusiveFileLock
        {
#if defined(_WIN32)
            HANDLE handle = INVALID_HANDLE_VALUE;
            stdfs::path native;
            ExclusiveFileLock(const Path& path, std::error_code& ec) : native(to_stdfs_path(path)) { ec.clear(); }

            bool lock_attempt(std::error_code& ec)
            {
                Checks::check_exit(VCPKG_LINE_INFO, handle == INVALID_HANDLE_VALUE);
                handle = CreateFileW(native.c_str(),
                                     GENERIC_READ,
                                     0 /* no sharing */,
                                     nullptr /* no security attributes */,
                                     OPEN_ALWAYS,
                                     FILE_ATTRIBUTE_NORMAL,
                                     nullptr /* no template file */);
                if (handle != INVALID_HANDLE_VALUE)
                {
                    ec.clear();
                    return true;
                }

                const auto err = GetLastError();
                if (err == ERROR_SHARING_VIOLATION)
                {
                    ec.clear();
                    return false;
                }

                ec.assign(err, std::system_category());
                return false;
            }

            ~ExclusiveFileLock() override
            {
                if (handle != INVALID_HANDLE_VALUE)
                {
                    const auto chresult = CloseHandle(handle);
                    Checks::check_exit(VCPKG_LINE_INFO, chresult != 0);
                }
            }
#else // ^^^ _WIN32 / !_WIN32 vvv
            PosixFd fd;
            bool locked = false;
            ExclusiveFileLock(const Path& path, std::error_code& ec)
                : fd(path.c_str(), O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH, ec)
            {
            }

            bool lock_attempt(std::error_code& ec)
            {
                if (fd.flock(LOCK_EX | LOCK_NB) == 0)
                {
                    ec.clear();
                    locked = true;
                    return true;
                }

                if (errno == EWOULDBLOCK)
                {
                    ec.clear();
                    return false;
                }

                ec.assign(errno, std::generic_category());
                return false;
            };

            ~ExclusiveFileLock() override
            {
                if (locked)
                {
                    Checks::check_exit(VCPKG_LINE_INFO, fd && fd.flock(LOCK_UN) == 0);
                }
            }
#endif
        };

        virtual std::unique_ptr<IExclusiveFileLock> take_exclusive_file_lock(const Path& lockfile,
                                                                             std::error_code& ec) override
        {
            auto result = std::make_unique<ExclusiveFileLock>(lockfile, ec);
            if (!ec && !result->lock_attempt(ec) && !ec)
            {
                vcpkg::printf("Waiting to take filesystem lock on %s...\n", lockfile);
                do
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                } while (!result->lock_attempt(ec) && !ec);
            }

            return std::move(result);
        }

        virtual std::unique_ptr<IExclusiveFileLock> try_take_exclusive_file_lock(const Path& lockfile,
                                                                                 std::error_code& ec) override
        {
            auto result = std::make_unique<ExclusiveFileLock>(lockfile, ec);
            if (!ec && !result->lock_attempt(ec) && !ec)
            {
                Debug::print("Waiting to take filesystem lock on ", lockfile, "...\n");
                // waits, at most, a second and a half.
                for (auto wait = std::chrono::milliseconds(100);;)
                {
                    std::this_thread::sleep_for(wait);
                    if (result->lock_attempt(ec) || ec)
                    {
                        break;
                    }

                    wait *= 2;
                    if (wait >= std::chrono::milliseconds(1000))
                    {
                        ec.assign(EBUSY, std::generic_category());
                        break;
                    }
                }
            }

            return std::move(result);
        }

        virtual std::vector<Path> find_from_PATH(const std::string& name) const override
        {
#if defined(_WIN32)
            static constexpr StringLiteral EXTS[] = {".cmd", ".exe", ".bat"};
#else  // ^^^ _WIN32 // !_WIN32 vvv
            static constexpr StringLiteral EXTS[] = {""};
#endif // ^^^!_WIN32
            const Path pname = name;
            auto path_bases = Strings::split_paths(get_environment_variable("PATH").value_or_exit(VCPKG_LINE_INFO));

            std::vector<Path> ret;
            for (auto&& path_base : path_bases)
            {
                auto path_base_name = Path(path_base) / pname;
                for (auto&& ext : EXTS)
                {
                    auto with_extension = path_base_name + ext;
                    if (Util::find(ret, with_extension) == ret.end() && this->exists(with_extension, IgnoreErrors{}))
                    {
                        Debug::print("Found path: ", with_extension, '\n');
                        ret.push_back(std::move(with_extension));
                    }
                }
            }

            return ret;
        }

        virtual ReadFilePointer open_for_read(const Path& file_path, std::error_code& ec) const override
        {
            return ReadFilePointer{file_path, ec};
        }

        virtual WriteFilePointer open_for_write(const Path& file_path, std::error_code& ec) override
        {
            return WriteFilePointer{file_path, ec};
        }
    };

    Filesystem& get_real_filesystem()
    {
        static RealFilesystem real_fs;
        return real_fs;
    }

    bool has_invalid_chars_for_filesystem(const std::string& s)
    {
        return s.find_first_of(R"([/\:*"<>|])") != std::string::npos;
    }

    void print_paths(const std::vector<Path>& paths)
    {
        std::string message = "\n";
        for (const Path& p : paths)
        {
            Strings::append(message, "    ", p.generic_u8string(), '\n');
        }
        message.push_back('\n');
        print2(message);
    }

    uint64_t get_filesystem_stats() { return g_us_filesystem_stats.load(); }

#ifdef _WIN32
    Path win32_fix_path_case(const Path& source)
    {
        const StringView native = source;
        if (native.empty())
        {
            return Path{};
        }

        if (Strings::starts_with(native, "\\\\?\\") || Strings::starts_with(native, "\\??\\") ||
            Strings::starts_with(native, "\\\\.\\"))
        {
            // no support to attempt to fix paths in the NT, \\GLOBAL??, or device namespaces at this time
            return source;
        }

        auto first = native.data();
        const auto last = first + native.size();
        auto is_wildcard = [](char c) { return c == '?' || c == '*'; };
        if (std::any_of(first, last, is_wildcard))
        {
            Checks::exit_with_message(
                VCPKG_LINE_INFO, "Attempt to fix case of a path containing wildcards: %s", source);
        }

        std::string in_progress;
        in_progress.reserve(native.size());
        if (last - first >= 3 && is_slash(first[0]) && is_slash(first[1]) && !is_slash(first[2]))
        {
            // path with UNC prefix \\server\share; this will be rejected by FindFirstFile so we skip over that
            in_progress.push_back('\\');
            in_progress.push_back('\\');
            first += 2;
            auto next_slash = std::find_if(first, last, is_slash);
            in_progress.append(first, next_slash);
            in_progress.push_back('\\');
            first = std::find_if_not(next_slash, last, is_slash);
            next_slash = std::find_if(first, last, is_slash);
            in_progress.append(first, next_slash);
            first = std::find_if_not(next_slash, last, is_slash);
            if (first != next_slash)
            {
                in_progress.push_back('\\');
            }
        }
        else if (last - first >= 1 && is_slash(first[0]))
        {
            // root relative path
            in_progress.push_back('\\');
            first = std::find_if_not(first, last, is_slash);
        }
        else if (has_drive_letter_prefix(first, last))
        {
            // path with drive letter root
            auto letter = first[0];
            if (letter >= 'a' && letter <= 'z')
            {
                letter = letter - 'a' + 'A';
            }

            in_progress.push_back(letter);
            in_progress.push_back(':');
            first += 2;
            if (first != last && is_slash(*first))
            {
                // absolute path
                in_progress.push_back('\\');
                first = std::find_if_not(first, last, is_slash);
            }
        }

        assert(find_root_name_end(first, last) == first);

        while (first != last)
        {
            auto next_slash = std::find_if(first, last, is_slash);
            auto original_size = in_progress.size();
            in_progress.append(first, next_slash);
            FindFirstOp this_find;
            unsigned long last_error = this_find.find_first(Strings::to_utf16(in_progress).c_str());
            if (last_error == ERROR_SUCCESS)
            {
                in_progress.resize(original_size);
                in_progress.append(Strings::to_utf8(this_find.find_data.cFileName));
            }
            else
            {
                // we might not have access to this intermediate part of the path;
                // just guess that the case of that element is correct and move on
            }

            first = std::find_if_not(next_slash, last, is_slash);
            if (first != next_slash)
            {
                in_progress.push_back('\\');
            }
        }

        return in_progress;
    }
#endif // _WIN32

}
