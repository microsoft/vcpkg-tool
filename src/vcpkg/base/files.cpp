#include <vcpkg/base/files.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.h>
#include <vcpkg/base/system.print.h>
#include <vcpkg/base/system.process.h>
#include <vcpkg/base/util.h>

#if defined(_WIN32)
#include <vcpkg/base/system_headers.h>
#else // ^^^ _WIN32 // !_WIN32 vvv
#include <fcntl.h>

#include <sys/file.h>
#include <sys/stat.h>
#endif // _WIN32

#if defined(__linux__)
#include <sys/sendfile.h>
#elif defined(__APPLE__)
#include <copyfile.h>
#endif // ^^^ defined(__APPLE__)

#include <algorithm>
#include <list>
#include <string>

#if !defined(VCPKG_USE_STD_FILESYSTEM)
#error The build system must set VCPKG_USE_STD_FILESYSTEM.
#endif // !defined(VCPKG_USE_STD_FILESYSTEM)

#if VCPKG_USE_STD_FILESYSTEM
#include <filesystem>
#else
#include <experimental/filesystem>
#endif

#if VCPKG_USE_STD_FILESYSTEM
namespace stdfs = std::filesystem;
#else
namespace stdfs = std::experimental::filesystem;
#endif

namespace
{
    using namespace vcpkg;

#if defined(_WIN32)
    struct IsSlash
    {
        bool operator()(const wchar_t c) const noexcept { return c == L'/' || c == L'\\'; }
    };
#else
    struct IsSlash
    {
        bool operator()(const char c) const noexcept { return c == '/'; }
    };
#endif

    constexpr IsSlash is_slash;

    bool is_dot(StringView sv) { return sv.size() == 1 && sv.byte_at_index(0) == '.'; }
    bool is_dot_dot(StringView sv)
    {
        return sv.size() == 2 && sv.byte_at_index(0) == '.' && sv.byte_at_index(1) == '.';
    }

    [[noreturn]] void exit_filesystem_call_error(LineInfo li,
                                                 const std::error_code& ec,
                                                 StringView call_name,
                                                 std::initializer_list<StringView> args)
    {
        Checks::exit_with_message(
            li, Strings::concat(call_name, "(", Strings::join(", ", args.begin(), args.end()), "): ", ec.message()));
    }

    stdfs::copy_options convert_copy_options(CopyOptions options)
    {
        stdfs::copy_options result{};
        const auto unpacked = static_cast<int>(options);

        switch (unpacked & static_cast<int>(CopyOptions::existing_mask))
        {
            case static_cast<int>(CopyOptions::skip_existing): result |= stdfs::copy_options::skip_existing; break;
            case static_cast<int>(CopyOptions::overwrite_existing):
                result |= stdfs::copy_options::overwrite_existing;
                break;
            case static_cast<int>(CopyOptions::update_existing): result |= stdfs::copy_options::update_existing; break;
        }

        if (unpacked & static_cast<int>(CopyOptions::recursive))
        {
            result |= stdfs::copy_options::recursive;
        }

        switch (unpacked & static_cast<int>(CopyOptions::symlinks_mask))
        {
            case static_cast<int>(CopyOptions::copy_symlinks): result |= stdfs::copy_options::copy_symlinks; break;
            case static_cast<int>(CopyOptions::skip_symlinks): result |= stdfs::copy_options::skip_symlinks; break;
        }

        return result;
    }

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
#if defined(_WIN32)
            case stdfs::file_type::junction: return FileType::junction;
#endif // _WIN32
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }
    }

    stdfs::path to_stdfs_path(const Path& utfpath)
    {
#if defined(_WIN32)
        return stdfs::path(Strings::to_utf16(utfpath.native()));
#else // ^^^ _WIN32 / !_WIN32 vvv
        return stdfs::path(utfpath.native());
#endif // ^^^ !_WIN32
    }

    Path from_stdfs_path(const stdfs::path& stdpath)
    {
#if defined(_WIN32)
        return Strings::to_utf8(stdpath.native());
#else // ^^^ _WIN32 / !_WIN32 vvv
        return stdpath.native();
#endif // ^^^ !_WIN32
    }

#if defined(_WIN32)
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
#else // ^^^ _WIN32 / !_WIN32 vvv
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
#else // ^^^ _WIN32 / !_WIN32 vvv
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
#else // ^^^ _WIN32 / !_WIN32 vvv
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
#else // ^^^ _WIN32 / !_WIN32 vvv
        return !str.empty() && str.byte_at_index(0) == '/';
#endif // ^^^ !_WIN32
    }

    VCPKG_CLANG_DIAGNOSTIC(push)
    VCPKG_CLANG_DIAGNOSTIC(ignored "-Wunused-const-variable")
    constexpr char preferred_separator = VCPKG_PREFERED_SEPARATOR[0];
    VCPKG_CLANG_DIAGNOSTIC(pop)

    void translate_not_found_to_success(std::error_code& ec)
    {
        if (ec && (ec == std::errc::no_such_file_or_directory || ec == std::errc::not_a_directory))
        {
            ec.clear();
        }
    }

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
    void set_writeable(const stdfs::path& target, std::error_code& ec) noexcept
    {
#if defined(_WIN32)
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
        }
#else // ^^^ defined(_WIN32) // !defined(_WIN32) vvv
        struct stat s;
        if (lstat(target.c_str(), &s))
        {
            ec.assign(errno, std::system_category());
            return;
        }

        auto mode = s.st_mode;
        // if the file is a symlink, perms don't matter
        if (!(mode & S_IFLNK))
        {
            mode |= S_IWUSR;
            if (chmod(target.c_str(), mode))
            {
                ec.assign(errno, std::system_category());
            }
        }
#endif // ^^^ !defined(_WIN32)
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
            set_writeable(current_entry, ec);
            if (err.check_ec(ec, current_entry)) return;
        }

        if (stdfs::is_directory(path_status))
        {
            for (const auto& entry : stdfs::directory_iterator(current_entry.path()))
            {
                vcpkg_remove_all_impl(entry, err);
                if (err.ec) return;
            }
#if defined(_WIN32)
            if (!RemoveDirectoryW(current_entry.path().c_str()))
            {
                ec.assign(GetLastError(), std::system_category());
            }
#else // ^^^ defined(_WIN32) // !defined(_WIN32) vvv
            if (rmdir(current_entry.path().c_str()))
            {
                ec.assign(errno, std::system_category());
            }
#endif // ^^^ !defined(_WIN32)
        }
#if VCPKG_USE_STD_FILESYSTEM
        else
        {
            stdfs::remove(current_entry.path(), ec);
            if (err.check_ec(ec, current_entry)) return;
        }
#else // ^^^  VCPKG_USE_STD_FILESYSTEM // !VCPKG_USE_STD_FILESYSTEM vvv
        else
        {
            if (unlink(current_entry.path().c_str()))
            {
                ec.assign(errno, std::system_category());
            }
        }
#endif // ^^^ !VCPKG_USE_STD_FILESYSTEM

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

    void vcpkg_remove_all(const stdfs::path& base, std::error_code& ec, Path& failure_point)
    {
#if VCPKG_USE_STD_FILESYSTEM
        stdfs::directory_entry entry(base, ec);
        translate_not_found_to_success(ec);
        if (ec)
        {
            failure_point = from_stdfs_path(base);
            return;
        }
#else
        stdfs::directory_entry entry(base);
#endif // VCPKG_USE_STD_FILESYSTEM

        vcpkg_remove_all(entry, ec, failure_point);
    }
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
#else // ^^^ _WIN32 / !_WIN32 vvv
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
#else // ^^^ _WIN32 / !_WIN32 vvv
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
#else // ^^^ _WIN32 / !_WIN32 vvv
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

    void Filesystem::copy(const Path& source, const Path& destination, CopyOptions options, LineInfo li)
    {
        std::error_code ec;
        this->copy(source, destination, options, ec);
        if (ec)
        {
            exit_filesystem_call_error(li, ec, __func__, {source, destination});
        }
    }

    void Filesystem::copy_file(const Path& source, const Path& destination, CopyOptions options, LineInfo li)
    {
        std::error_code ec;
        this->copy_file(source, destination, options, ec);
        if (ec)
        {
            exit_filesystem_call_error(li, ec, __func__, {source, destination});
        }
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
                else if (file.error())
                {
                    ec = std::io_errc::stream;
                    return std::string();
                }
            } while (!file.eof());

            return output;
        }
        virtual std::vector<std::string> read_lines(const Path& file_path, std::error_code& ec) const override
        {
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
                else if (file.error())
                {
                    ec = std::io_errc::stream;
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

        template<class Iter>
        static std::vector<Path> get_files_impl(const Path& dir, std::error_code& ec)
        {
            std::vector<Path> ret;
            Iter b(to_stdfs_path(dir), ec), e{};
            if (!ec)
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
            if (!ec)
            {
                while (b != e)
                {
#if VCPKG_USE_STD_FILESYSTEM
                    if (b->is_directory(ec))
#else // ^^^ VCPKG_USE_STD_FILESYSTEM // !VCPKG_USE_STD_FILESYSTEM vvv
                    if (stdfs::is_directory(b->path(), ec))
#endif // VCPKG_USE_STD_FILESYSTEM
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
            if (!ec)
            {
                while (b != e)
                {
#if VCPKG_USE_STD_FILESYSTEM
                    if (b->is_regular_file(ec))
#else // ^^^ VCPKG_USE_STD_FILESYSTEM // !VCPKG_USE_STD_FILESYSTEM vvv
                    if (stdfs::is_regular_file(b->path(), ec))
#endif // VCPKG_USE_STD_FILESYSTEM
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
            stdfs::rename(to_stdfs_path(old_path), to_stdfs_path(new_path), ec);
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
                auto dst = new_path;
                dst += temp_suffix;

                int i_fd = open(old_path.c_str(), O_RDONLY);
                if (i_fd == -1) return;

                int o_fd = creat(dst.c_str(), 0664);
                if (o_fd == -1)
                {
                    close(i_fd);
                    return;
                }

#if defined(__linux__)
                off_t bytes = 0;
                struct stat info = {0};
                fstat(i_fd, &info);
                auto written_bytes = sendfile(o_fd, i_fd, &bytes, info.st_size);
#elif defined(__APPLE__)
                auto written_bytes = fcopyfile(i_fd, o_fd, 0, COPYFILE_ALL);
#else // ^^^ defined(__APPLE__) // !(defined(__APPLE__) || defined(__linux__)) vvv
                ssize_t written_bytes = 0;
                {
                    constexpr std::size_t buffer_length = 4096;
                    auto buffer = std::make_unique<unsigned char[]>(buffer_length);
                    while (auto read_bytes = read(i_fd, buffer.get(), buffer_length))
                    {
                        if (read_bytes == -1)
                        {
                            written_bytes = -1;
                            break;
                        }
                        auto remaining = read_bytes;
                        while (remaining > 0)
                        {
                            auto read_result = write(o_fd, buffer.get(), remaining);
                            if (read_result == -1)
                            {
                                written_bytes = -1;
                                // break two loops
                                goto copy_failure;
                            }
                            remaining -= read_result;
                        }
                    }

                copy_failure:;
                }
#endif // ^^^ !(defined(__APPLE__) || defined(__linux__))
                if (written_bytes == -1)
                {
                    ec.assign(errno, std::generic_category());
                    close(i_fd);
                    close(o_fd);

                    return;
                }

                close(i_fd);
                close(o_fd);

                this->rename(dst, new_path, ec);
                if (ec) return;
                this->remove(old_path, ec);
            }
#endif // ^^^ !defined(_WIN32)
        }
        virtual bool remove(const Path& target, std::error_code& ec) override
        {
            auto as_stdfs = to_stdfs_path(target);
            bool result = stdfs::remove(as_stdfs, ec);
#if defined(_WIN32)
            if (ec && ec == std::error_code(ERROR_ACCESS_DENIED, std::system_category()))
            {
                set_writeable(as_stdfs, ec);
                if (ec)
                {
                    return false;
                }

                return stdfs::remove(as_stdfs, ec);
            }
#endif // _WIN32

            return result;
        }
        virtual void remove_all(const Path& base, std::error_code& ec, Path& failure_point) override
        {
            vcpkg_remove_all(to_stdfs_path(base), ec, failure_point);
        }

        virtual void remove_all_inside(const Path& base, std::error_code& ec, Path& failure_point) override
        {
            stdfs::directory_iterator last{};
            stdfs::directory_iterator first(to_stdfs_path(base), ec);
            if (ec)
            {
                failure_point = base;
                return;
            }

            for (;;)
            {
                if (first == last)
                {
                    return;
                }

                auto stats = first->status(ec);
                if (ec)
                {
                    break;
                }

                auto& thisPath = first->path();
                if (stats.type() == stdfs::file_type::directory)
                {
                    vcpkg_remove_all(*first, ec, failure_point);
                    if (ec)
                    {
                        return; // keep inner failure_point
                    }
                }
                else
                {
                    stdfs::remove(thisPath, ec);
                    if (ec)
                    {
                        break;
                    }
                }

                first.increment(ec);
                if (ec)
                {
                    break;
                }
            }

            failure_point = from_stdfs_path(first->path());
        }

        virtual bool is_directory(const Path& target) const override
        {
            return stdfs::is_directory(to_stdfs_path(target));
        }
        virtual bool is_regular_file(const Path& target) const override
        {
            return stdfs::is_regular_file(to_stdfs_path(target));
        }
        virtual bool is_empty(const Path& target, std::error_code& ec) const override
        {
            return stdfs::is_empty(to_stdfs_path(target), ec);
        }
        virtual bool create_directory(const Path& new_directory, std::error_code& ec) override
        {
            return stdfs::create_directory(to_stdfs_path(new_directory), ec);
        }
        virtual bool create_directories(const Path& new_directory, std::error_code& ec) override
        {
            return stdfs::create_directories(to_stdfs_path(new_directory), ec);
        }
        virtual void create_symlink(const Path& to, const Path& from, std::error_code& ec) override
        {
            stdfs::create_symlink(to_stdfs_path(to), to_stdfs_path(from), ec);
        }
        virtual void create_directory_symlink(const Path& to, const Path& from, std::error_code& ec) override
        {
            stdfs::create_directory_symlink(to_stdfs_path(to), to_stdfs_path(from), ec);
        }
        virtual void create_hard_link(const Path& to, const Path& from, std::error_code& ec) override
        {
            stdfs::create_hard_link(to_stdfs_path(to), to_stdfs_path(from), ec);
        }
        virtual void copy(const Path& source,
                          const Path& destination,
                          CopyOptions options,
                          std::error_code& ec) override
        {
            stdfs::copy(to_stdfs_path(source), to_stdfs_path(destination), convert_copy_options(options), ec);
        }
        virtual bool copy_file(const Path& source,
                               const Path& destination,
                               CopyOptions options,
                               std::error_code& ec) override
        {
            return stdfs::copy_file(
                to_stdfs_path(source), to_stdfs_path(destination), convert_copy_options(options), ec);
        }
        virtual void copy_symlink(const Path& source, const Path& destination, std::error_code& ec) override
        {
            return stdfs::copy_symlink(to_stdfs_path(source), to_stdfs_path(destination), ec);
        }

        virtual FileType status(const Path& target, std::error_code& ec) const override
        {
            auto result = stdfs::status(to_stdfs_path(target), ec);
            translate_not_found_to_success(ec);
            return convert_file_type(result.type());
        }
        virtual FileType symlink_status(const Path& target, std::error_code& ec) const override
        {
            auto result = stdfs::symlink_status(to_stdfs_path(target), ec);
            translate_not_found_to_success(ec);
            return convert_file_type(result.type());
        }
        virtual void write_contents(const Path& file_path, const std::string& data, std::error_code& ec) override
        {
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
#if VCPKG_USE_STD_FILESYSTEM
            return from_stdfs_path(stdfs::absolute(to_stdfs_path(target), ec));
#else // ^^^ VCPKG_USE_STD_FILESYSTEM  /  !VCPKG_USE_STD_FILESYSTEM  vvv
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
#endif // ^^^ !VCPKG_USE_STD_FILESYSTEM
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
            return from_stdfs_path(stdfs::current_path(ec));
        }
        virtual void current_path(const Path& new_current_path, std::error_code& ec) override
        {
            stdfs::current_path(to_stdfs_path(new_current_path), ec);
        }

        struct ExclusiveFileLock : IExclusiveFileLock
        {
#if defined(_WIN32)
            HANDLE handle = INVALID_HANDLE_VALUE;
            stdfs::path native;
            ExclusiveFileLock(stdfs::path&& native, std::error_code& ec) : native(std::move(native)) { ec.clear(); }

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
            int fd;
            bool locked = false;
            ExclusiveFileLock(stdfs::path&& native, std::error_code& ec)
                : fd(::open(native.c_str(), O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH))
            {
                if (fd < 0)
                {
                    ec.assign(errno, std::generic_category());
                }
                else
                {
                    ec.clear();
                }
            }

            bool lock_attempt(std::error_code& ec)
            {
                if (::flock(fd, LOCK_EX | LOCK_NB) == 0)
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
                if (fd > 0)
                {
                    if (locked)
                    {
                        Checks::check_exit(VCPKG_LINE_INFO, flock(fd, LOCK_UN) == 0);
                    }

                    Checks::check_exit(VCPKG_LINE_INFO, close(fd) == 0);
                }
            }
#endif
        };

        virtual std::unique_ptr<IExclusiveFileLock> take_exclusive_file_lock(const Path& lockfile,
                                                                             std::error_code& ec) override
        {
            auto result = std::make_unique<ExclusiveFileLock>(to_stdfs_path(lockfile), ec);
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
            auto result = std::make_unique<ExclusiveFileLock>(to_stdfs_path(lockfile), ec);
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
#else // ^^^ defined(_WIN32) // !defined(_WIN32) vvv
            static constexpr StringLiteral EXTS[] = {""};
#endif // ^^^!defined(_WIN32)
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
