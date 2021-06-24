#pragma once

#include <vcpkg/base/expected.h>
#include <vcpkg/base/ignore_errors.h>
#include <vcpkg/base/pragmas.h>

#include <string.h>

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

namespace vcpkg::Files
{
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

    stdfs::path u8path(vcpkg::StringView s);
    inline stdfs::path u8path(const char* first, const char* last) { return u8path(vcpkg::StringView{first, last}); }
    inline stdfs::path u8path(std::initializer_list<char> il)
    {
        return u8path(vcpkg::StringView{il.begin(), il.end()});
    }
    inline stdfs::path u8path(const char* s) { return u8path(vcpkg::StringView{s, s + ::strlen(s)}); }

    inline stdfs::path u8path(std::string::const_iterator first, std::string::const_iterator last)
    {
        auto firstp = &*first;
        return u8path(vcpkg::StringView{firstp, firstp + (last - first)});
    }

    std::string u8string(const stdfs::path& p);
    std::string generic_u8string(const stdfs::path& p);

    // equivalent to p.lexically_normal()
    stdfs::path lexically_normal(const stdfs::path& p);

#if defined(_WIN32)
    enum class file_type
    {
        none = 0,
        not_found = -1,
        regular = 1,
        directory = 2,
        symlink = 3,
        block = 4,
        character = 5,
        fifo = 6,
        socket = 7,
        unknown = 8,
        // also stands for a junction
        directory_symlink = 42
    };

    struct file_status
    {
        explicit file_status(file_type type = file_type::none,
                             stdfs::perms permissions = stdfs::perms::unknown) noexcept
            : m_type(type), m_permissions(permissions)
        {
        }

        file_type type() const noexcept { return m_type; }
        void type(file_type type) noexcept { m_type = type; }

        stdfs::perms permissions() const noexcept { return m_permissions; }
        void permissions(stdfs::perms perm) noexcept { m_permissions = perm; }

    private:
        file_type m_type;
        stdfs::perms m_permissions;
    };

    struct SystemHandle
    {
        using type = intptr_t; // HANDLE
        type system_handle = -1;

        bool is_valid() const { return system_handle != -1; }
    };

#else

    using stdfs::file_type;
    // to set up ADL correctly on `file_status` objects, we are defining
    // this in our own namespace
    struct file_status : private stdfs::file_status
    {
        using stdfs::file_status::file_status;
        using stdfs::file_status::permissions;
        using stdfs::file_status::type;
    };

    struct SystemHandle
    {
        using type = int; // file descriptor
        type system_handle = -1;

        bool is_valid() const { return system_handle != -1; }
    };

#endif

    inline bool operator==(SystemHandle lhs, SystemHandle rhs) noexcept
    {
        return lhs.system_handle == rhs.system_handle;
    }
    inline bool operator!=(SystemHandle lhs, SystemHandle rhs) noexcept { return !(lhs == rhs); }

    inline bool is_symlink(file_status s) noexcept
    {
#if defined(_WIN32)
        if (s.type() == file_type::directory_symlink) return true;
#endif
        return s.type() == file_type::symlink;
    }
    inline bool is_regular_file(file_status s) { return s.type() == file_type::regular; }
    inline bool is_directory(file_status s) { return s.type() == file_type::directory; }
    inline bool exists(file_status s) { return s.type() != file_type::not_found && s.type() != file_type::none; }
}

/*
    if someone attempts to use unqualified `symlink_status` or `is_symlink`,
    they might get the ADL version, which is broken.
    Therefore, put `(symlink_)?status` as deleted in the global namespace, so
    that they get an error.

    We also want to poison the ADL on the other functions, because
    we don't want people calling these functions on paths
*/
void status(const stdfs::path& p) = delete;
void status(const stdfs::path& p, std::error_code& ec) = delete;
void symlink_status(const stdfs::path& p) = delete;
void symlink_status(const stdfs::path& p, std::error_code& ec) = delete;
void is_symlink(const stdfs::path& p) = delete;
void is_symlink(const stdfs::path& p, std::error_code& ec) = delete;
void is_regular_file(const stdfs::path& p) = delete;
void is_regular_file(const stdfs::path& p, std::error_code& ec) = delete;
void is_directory(const stdfs::path& p) = delete;
void is_directory(const stdfs::path& p, std::error_code& ec) = delete;

namespace vcpkg
{
    struct Filesystem
    {
        std::string read_contents(const stdfs::path& file_path, LineInfo linfo) const;
        virtual Expected<std::string> read_contents(const stdfs::path& file_path) const = 0;
        /// <summary>Read text lines from a file</summary>
        /// <remarks>Lines will have up to one trailing carriage-return character stripped (CRLF)</remarks>
        virtual Expected<std::vector<std::string>> read_lines(const stdfs::path& file_path) const = 0;
        std::vector<std::string> read_lines(const stdfs::path& file_path, LineInfo linfo) const;
        virtual stdfs::path find_file_recursively_up(const stdfs::path& starting_dir,
                                                     const stdfs::path& filename) const = 0;
        virtual std::vector<stdfs::path> get_files_recursive(const stdfs::path& dir) const = 0;
        virtual std::vector<stdfs::path> get_files_non_recursive(const stdfs::path& dir) const = 0;
        void write_lines(const stdfs::path& file_path, const std::vector<std::string>& lines, LineInfo linfo);
        virtual void write_lines(const stdfs::path& file_path,
                                 const std::vector<std::string>& lines,
                                 std::error_code& ec) = 0;
        void write_contents(const stdfs::path& path, const std::string& data, LineInfo linfo);
        virtual void write_contents(const stdfs::path& file_path, const std::string& data, std::error_code& ec) = 0;
        void write_rename_contents(const stdfs::path& path,
                                   const stdfs::path& tmpext,
                                   const std::string& data,
                                   LineInfo linfo);
        void write_contents_and_dirs(const stdfs::path& path, const std::string& data, LineInfo linfo);
        virtual void write_contents_and_dirs(const stdfs::path& file_path,
                                             const std::string& data,
                                             std::error_code& ec) = 0;
        void rename(const stdfs::path& oldpath, const stdfs::path& newpath, LineInfo linfo);
        void rename_with_retry(const stdfs::path& oldpath, const stdfs::path& newpath, std::error_code& ec);
        virtual void rename(const stdfs::path& oldpath, const stdfs::path& newpath, std::error_code& ec) = 0;
        virtual void rename_or_copy(const stdfs::path& oldpath,
                                    const stdfs::path& newpath,
                                    StringLiteral temp_suffix,
                                    std::error_code& ec) = 0;
        bool remove(const stdfs::path& path, LineInfo linfo);
        bool remove(const stdfs::path& path, ignore_errors_t);
        virtual bool remove(const stdfs::path& path, std::error_code& ec) = 0;

        virtual void remove_all(const stdfs::path& path, std::error_code& ec, stdfs::path& failure_point) = 0;
        void remove_all(const stdfs::path& path, LineInfo li);
        void remove_all(const stdfs::path& path, ignore_errors_t);
        virtual void remove_all_inside(const stdfs::path& path, std::error_code& ec, stdfs::path& failure_point) = 0;
        void remove_all_inside(const stdfs::path& path, LineInfo li);
        void remove_all_inside(const stdfs::path& path, ignore_errors_t);
        bool exists(const stdfs::path& path, std::error_code& ec) const;
        bool exists(LineInfo li, const stdfs::path& path) const;
        bool exists(const stdfs::path& path, ignore_errors_t = ignore_errors) const;
        virtual bool is_directory(const stdfs::path& path) const = 0;
        virtual bool is_regular_file(const stdfs::path& path) const = 0;
        virtual bool is_empty(const stdfs::path& path) const = 0;
        virtual bool create_directory(const stdfs::path& path, std::error_code& ec) = 0;
        bool create_directory(const stdfs::path& path, ignore_errors_t);
        bool create_directory(const stdfs::path& path, LineInfo li);
        virtual bool create_directories(const stdfs::path& path, std::error_code& ec) = 0;
        bool create_directories(const stdfs::path& path, ignore_errors_t);
        bool create_directories(const stdfs::path& path, LineInfo);
        virtual void create_symlink(const stdfs::path& to, const stdfs::path& from, std::error_code& ec) = 0;
        virtual void create_hard_link(const stdfs::path& to, const stdfs::path& from, std::error_code& ec) = 0;
        void create_best_link(const stdfs::path& to, const stdfs::path& from, std::error_code& ec);
        void create_best_link(const stdfs::path& to, const stdfs::path& from, LineInfo);
        virtual void copy(const stdfs::path& oldpath, const stdfs::path& newpath, stdfs::copy_options opts) = 0;
        virtual bool copy_file(const stdfs::path& oldpath,
                               const stdfs::path& newpath,
                               stdfs::copy_options opts,
                               std::error_code& ec) = 0;
        void copy_file(const stdfs::path& oldpath, const stdfs::path& newpath, stdfs::copy_options opts, LineInfo li);
        virtual void copy_symlink(const stdfs::path& oldpath, const stdfs::path& newpath, std::error_code& ec) = 0;
        virtual vcpkg::Files::file_status status(const stdfs::path& path, std::error_code& ec) const = 0;
        virtual vcpkg::Files::file_status symlink_status(const stdfs::path& path, std::error_code& ec) const = 0;
        vcpkg::Files::file_status status(LineInfo li, const stdfs::path& p) const noexcept;
        vcpkg::Files::file_status status(const stdfs::path& p, ignore_errors_t) const noexcept;
        vcpkg::Files::file_status symlink_status(LineInfo li, const stdfs::path& p) const noexcept;
        vcpkg::Files::file_status symlink_status(const stdfs::path& p, ignore_errors_t) const noexcept;
        virtual stdfs::path absolute(const stdfs::path& path, std::error_code& ec) const = 0;
        stdfs::path absolute(LineInfo li, const stdfs::path& path) const;
        // absolute/system_complete + lexically_normal + fixup_win32_path_case
        // we don't use real canonical due to issues like:
        // https://github.com/microsoft/vcpkg/issues/16614 (canonical breaking on some older Windows Server containers)
        // https://github.com/microsoft/vcpkg/issues/18208 (canonical removing subst despite our recommendation to use
        // subst)
        virtual stdfs::path almost_canonical(const stdfs::path& path, std::error_code& ec) const = 0;
        stdfs::path almost_canonical(LineInfo li, const stdfs::path& path) const;
        stdfs::path almost_canonical(const stdfs::path& path, ignore_errors_t) const;
        virtual stdfs::path current_path(std::error_code&) const = 0;
        stdfs::path current_path(LineInfo li) const;
        virtual void current_path(const stdfs::path& path, std::error_code&) = 0;
        void current_path(const stdfs::path& path, LineInfo li);

        // if the path does not exist, then (try_|)take_exclusive_file_lock attempts to create the file
        // (but not any path members above the file itself)
        // in other words, if `/a/b` is a directory, and you're attempting to lock `/a/b/c`,
        // then these lock functions create `/a/b/c` if it doesn't exist;
        // however, if `/a/b` doesn't exist, then the functions will fail.

        // waits forever for the file lock
        virtual vcpkg::Files::SystemHandle take_exclusive_file_lock(const stdfs::path& path, std::error_code&) = 0;
        // waits, at most, 1.5 seconds, for the file lock
        virtual vcpkg::Files::SystemHandle try_take_exclusive_file_lock(const stdfs::path& path, std::error_code&) = 0;
        virtual void unlock_file_lock(vcpkg::Files::SystemHandle handle, std::error_code&) = 0;

        virtual std::vector<stdfs::path> find_from_PATH(const std::string& name) const = 0;
    };

    Filesystem& get_real_filesystem();

    static constexpr const char* FILESYSTEM_INVALID_CHARACTERS = R"(\/:*?"<>|)";

    bool has_invalid_chars_for_filesystem(const std::string& s);

    void print_paths(const std::vector<stdfs::path>& paths);

    // Performs "lhs / rhs" according to the C++17 Filesystem Library Specification.
    // This function exists as a workaround for TS implementations.
    stdfs::path combine(const stdfs::path& lhs, const stdfs::path& rhs);

#if defined(_WIN32)
    constexpr char preferred_separator = '\\';
#else
    constexpr char preferred_separator = '/';
#endif // _WIN32

#if defined(_WIN32)
    stdfs::path win32_fix_path_case(const stdfs::path& source);
#endif // _WIN32

    struct ExclusiveFileLock
    {
        enum class Wait
        {
            Yes,
            No,
        };

        ExclusiveFileLock() = default;
        ExclusiveFileLock(ExclusiveFileLock&&) = delete;
        ExclusiveFileLock& operator=(ExclusiveFileLock&&) = delete;

        ExclusiveFileLock(Wait wait, Filesystem& fs, const stdfs::path& path_, std::error_code& ec) : m_fs(&fs)
        {
            switch (wait)
            {
                case Wait::Yes: m_handle = m_fs->take_exclusive_file_lock(path_, ec); break;
                case Wait::No: m_handle = m_fs->try_take_exclusive_file_lock(path_, ec); break;
            }
        }
        ~ExclusiveFileLock() { clear(); }

        explicit operator bool() const { return m_handle.is_valid(); }
        bool has_lock() const { return m_handle.is_valid(); }

        void clear()
        {
            if (m_fs && m_handle.is_valid())
            {
                std::error_code ignore;
                m_fs->unlock_file_lock(std::exchange(m_handle, vcpkg::Files::SystemHandle{}), ignore);
            }
        }

    private:
        Filesystem* m_fs;
        vcpkg::Files::SystemHandle m_handle;
    };

}
