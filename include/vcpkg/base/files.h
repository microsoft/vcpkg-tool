#pragma once

#include <vcpkg/base/fwd/format.h>
#include <vcpkg/base/fwd/span.h>

#include <vcpkg/base/checks.h>
#include <vcpkg/base/lineinfo.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/pragmas.h>
#include <vcpkg/base/setup_messages.h>
#include <vcpkg/base/stringview.h>

#include <stdio.h>
#include <string.h>

#include <initializer_list>
#include <memory>
#include <system_error>

#if defined(_WIN32)
#define VCPKG_PREFERRED_SEPARATOR "\\"
#else // ^^^ _WIN32 / !_WIN32 vvv
#define VCPKG_PREFERRED_SEPARATOR "/"
#endif // _WIN32

namespace vcpkg
{
    LocalizedString format_filesystem_call_error(const std::error_code& ec,
                                                 StringView call_name,
                                                 std::initializer_list<StringView> args);
    [[noreturn]] void exit_filesystem_call_error(LineInfo li,
                                                 const std::error_code& ec,
                                                 StringView call_name,
                                                 std::initializer_list<StringView> args);
    struct IgnoreErrors
    {
        operator std::error_code&() { return ec; }

    private:
        std::error_code ec;
    };

    enum class CopyOptions
    {
        none = 0,
        skip_existing = 0x1,
        overwrite_existing = 0x2,
    };

    struct Path
    {
        Path() : m_str() { }
        Path(const Path&) = default;
        Path(Path&&) = default;
        Path& operator=(const Path&) = default;
        Path& operator=(Path&&) = default;

        Path(const StringView sv) : m_str(sv.to_string()) { }
        Path(const std::string& s) : m_str(s) { }
        Path(std::string&& s) : m_str(std::move(s)) { }
        Path(const char* s) : m_str(s) { }
        template<class Iter>
        Path(Iter first, Iter last) : m_str(first, last)
        {
        }
        Path(const char* first, size_t size) : m_str(first, size) { }

        const std::string& native() const& noexcept { return m_str; }
        std::string&& native() && noexcept { return std::move(m_str); }
        operator StringView() const noexcept { return m_str; }

        const char* c_str() const noexcept { return m_str.c_str(); }

        std::string generic_u8string() const;

        bool empty() const noexcept { return m_str.empty(); }

        Path operator/(StringView sv) const&;
        Path operator/(StringView sv) &&;
        Path operator+(StringView sv) const&;
        Path operator+(StringView sv) &&;

        Path& operator/=(StringView sv);
        Path& operator+=(StringView sv);

        void replace_filename(StringView sv);
        void remove_filename();
        void make_preferred();
        void clear() { m_str.clear(); }
        Path lexically_normal() const;

        // Sets *this to parent_path, returns whether anything was removed
        bool make_parent_path();

        StringView parent_path() const;
        StringView filename() const;
        StringView extension() const;
        StringView stem() const;

        bool is_absolute() const;
        bool is_relative() const { return !is_absolute(); }

        friend const char* to_printf_arg(const Path& p) noexcept { return p.m_str.c_str(); }

    private:
        std::string m_str;
    };

    enum class FileType
    {
        none,
        not_found,
        regular,
        directory,
        symlink,

        block,
        character,

        fifo,
        socket,
        unknown,

        junction // implementation-defined value indicating an NT junction
    };

    inline bool is_symlink(FileType s) { return s == FileType::symlink || s == FileType::junction; }
    inline bool is_regular_file(FileType s) { return s == FileType::regular; }
    inline bool is_directory(FileType s) { return s == FileType::directory; }
    inline bool exists(FileType s) { return s != FileType::not_found && s != FileType::none; }

    struct FilePointer
    {
    protected:
        FILE* m_fs;

    public:
        FilePointer() noexcept : m_fs(nullptr) { }

        FilePointer(const FilePointer&) = delete;
        FilePointer(FilePointer&& other) noexcept : m_fs(other.m_fs) { other.m_fs = nullptr; }

        FilePointer& operator=(const FilePointer&) = delete;
        explicit operator bool() const noexcept { return m_fs != nullptr; }

        int seek(int offset, int origin) const noexcept { return ::fseek(m_fs, offset, origin); }
        int seek(unsigned int offset, int origin) const noexcept
        {
            return this->seek(static_cast<long long>(offset), origin);
        }
        int seek(long offset, int origin) const noexcept { return ::fseek(m_fs, offset, origin); }
        int seek(unsigned long offset, int origin) const noexcept
        {
#if defined(_WIN32)
            return ::_fseeki64(m_fs, static_cast<long long>(offset), origin);
#else  // ^^^ _WIN32 / !_WIN32 vvv
            Checks::check_exit(VCPKG_LINE_INFO, offset < LLONG_MAX);
            return ::fseek(m_fs, offset, origin);
#endif // ^^^ !_WIN32
        }
        int seek(long long offset, int origin) const noexcept
        {
#if defined(_WIN32)
            return ::_fseeki64(m_fs, offset, origin);
#else  // ^^^ _WIN32 / !_WIN32 vvv
            return ::fseek(m_fs, offset, origin);
#endif // ^^^ !_WIN32
        }
        int seek(unsigned long long offset, int origin) const noexcept
        {
            Checks::check_exit(VCPKG_LINE_INFO, offset < LLONG_MAX);
            return this->seek(static_cast<long long>(offset), origin);
        }
        int eof() const noexcept { return ::feof(m_fs); }
        std::error_code error() const noexcept { return std::error_code(::ferror(m_fs), std::generic_category()); }

        ~FilePointer()
        {
            if (m_fs)
            {
                Checks::check_exit(VCPKG_LINE_INFO, ::fclose(m_fs) == 0);
            }
        }
    };

    struct ReadFilePointer : FilePointer
    {
        ReadFilePointer() = default;
        explicit ReadFilePointer(const Path& file_path, std::error_code& ec) noexcept;

        size_t read(void* buffer, size_t element_size, size_t element_count) const noexcept
        {
            return ::fread(buffer, element_size, element_count, m_fs);
        }
    };

    struct WriteFilePointer : FilePointer
    {
        WriteFilePointer() = default;
        explicit WriteFilePointer(const Path& file_path, std::error_code& ec) noexcept;

        size_t write(const void* buffer, size_t element_size, size_t element_count) const noexcept
        {
            return ::fwrite(buffer, element_size, element_count, m_fs);
        }

        int put(int c) const noexcept { return ::fputc(c, m_fs); }
    };

    struct IExclusiveFileLock
    {
        virtual ~IExclusiveFileLock() = default;
    };

    uint64_t get_filesystem_stats();

    struct Filesystem
    {
        virtual std::string read_contents(const Path& file_path, std::error_code& ec) const = 0;
        std::string read_contents(const Path& file_path, LineInfo li) const;

        virtual std::vector<std::string> read_lines(const Path& file_path, std::error_code& ec) const = 0;
        std::vector<std::string> read_lines(const Path& file_path, LineInfo li) const;

        virtual Path find_file_recursively_up(const Path& starting_dir,
                                              const Path& filename,
                                              std::error_code& ec) const = 0;
        Path find_file_recursively_up(const Path& starting_dir, const Path& filename, LineInfo li) const;

        virtual std::vector<Path> get_files_recursive(const Path& dir, std::error_code& ec) const = 0;
        std::vector<Path> get_files_recursive(const Path& dir, LineInfo li) const;

        virtual std::vector<Path> get_files_non_recursive(const Path& dir, std::error_code& ec) const = 0;
        std::vector<Path> get_files_non_recursive(const Path& dir, LineInfo li) const;

        virtual std::vector<Path> get_directories_recursive(const Path& dir, std::error_code& ec) const = 0;
        std::vector<Path> get_directories_recursive(const Path& dir, LineInfo li) const;

        virtual std::vector<Path> get_directories_non_recursive(const Path& dir, std::error_code& ec) const = 0;
        std::vector<Path> get_directories_non_recursive(const Path& dir, LineInfo li) const;

        virtual std::vector<Path> get_regular_files_recursive(const Path& dir, std::error_code& ec) const = 0;
        std::vector<Path> get_regular_files_recursive(const Path& dir, LineInfo li) const;

        virtual std::vector<Path> get_regular_files_recursive_lexically_proximate(const Path& dir,
                                                                                  std::error_code& ec) const = 0;
        std::vector<Path> get_regular_files_recursive_lexically_proximate(const Path& dir, LineInfo li) const;

        virtual std::vector<Path> get_regular_files_non_recursive(const Path& dir, std::error_code& ec) const = 0;
        std::vector<Path> get_regular_files_non_recursive(const Path& dir, LineInfo li) const;

        virtual void write_lines(const Path& file_path, const std::vector<std::string>& lines, std::error_code& ec) = 0;
        void write_lines(const Path& file_path, const std::vector<std::string>& lines, LineInfo li);

        virtual void write_contents(const Path& file_path, StringView data, std::error_code& ec) = 0;
        void write_contents(const Path& file_path, StringView data, LineInfo li);

        void write_rename_contents(const Path& file_path, const Path& temp_name, StringView data, LineInfo li);
        void write_contents_and_dirs(const Path& file_path, StringView data, LineInfo li);
        virtual void write_contents_and_dirs(const Path& file_path, StringView data, std::error_code& ec) = 0;

        virtual void rename(const Path& old_path, const Path& new_path, std::error_code& ec) = 0;
        void rename(const Path& old_path, const Path& new_path, LineInfo li);

        void rename_with_retry(const Path& old_path, const Path& new_path, std::error_code& ec);
        void rename_with_retry(const Path& old_path, const Path& new_path, LineInfo li);

        virtual void rename_or_copy(const Path& old_path,
                                    const Path& new_path,
                                    StringLiteral temp_suffix,
                                    std::error_code& ec) = 0;

        virtual bool remove(const Path& target, std::error_code& ec) = 0;
        bool remove(const Path& target, LineInfo li);

        virtual void remove_all(const Path& base, std::error_code& ec, Path& failure_point) = 0;
        void remove_all(const Path& base, std::error_code& ec);
        void remove_all(const Path& base, LineInfo li);

        void remove_all_inside(const Path& base, std::error_code& ec, Path& failure_point);
        void remove_all_inside(const Path& base, std::error_code& ec);
        void remove_all_inside(const Path& base, LineInfo li);

        bool exists(const Path& target, std::error_code& ec) const;
        bool exists(const Path& target, LineInfo li) const;

        virtual bool is_directory(const Path& target) const = 0;
        virtual bool is_regular_file(const Path& target) const = 0;

        virtual bool is_empty(const Path& target, std::error_code& ec) const = 0;
        bool is_empty(const Path& target, LineInfo li) const;

        virtual bool create_directory(const Path& new_directory, std::error_code& ec) = 0;
        bool create_directory(const Path& new_directory, LineInfo li);

        virtual bool create_directories(const Path& new_directory, std::error_code& ec) = 0;
        bool create_directories(const Path& new_directory, LineInfo);

        virtual void create_symlink(const Path& to, const Path& from, std::error_code& ec) = 0;
        void create_symlink(const Path& to, const Path& from, LineInfo);

        virtual void create_directory_symlink(const Path& to, const Path& from, std::error_code& ec) = 0;
        void create_directory_symlink(const Path& to, const Path& from, LineInfo);

        virtual void create_hard_link(const Path& to, const Path& from, std::error_code& ec) = 0;
        void create_hard_link(const Path& to, const Path& from, LineInfo);

        void create_best_link(const Path& to, const Path& from, std::error_code& ec);
        void create_best_link(const Path& to, const Path& from, LineInfo);

        // copies regular files and directories, recursively.
        // symlinks are followed and copied as if they were regular files or directories
        //   (like std::filesystem::copy(..., std::filesystem::copy_options::recursive))
        virtual void copy_regular_recursive(const Path& source, const Path& destination, std::error_code& ec) = 0;
        void copy_regular_recursive(const Path& source, const Path& destination, LineInfo);

        virtual bool copy_file(const Path& source,
                               const Path& destination,
                               CopyOptions options,
                               std::error_code& ec) = 0;
        bool copy_file(const Path& source, const Path& destination, CopyOptions options, LineInfo li);

        virtual void copy_symlink(const Path& source, const Path& destination, std::error_code& ec) = 0;
        void copy_symlink(const Path& source, const Path& destination, LineInfo li);

        virtual FileType status(const Path& target, std::error_code& ec) const = 0;
        FileType status(const Path& target, LineInfo li) const noexcept;

        virtual FileType symlink_status(const Path& target, std::error_code& ec) const = 0;
        FileType symlink_status(const Path& target, LineInfo li) const noexcept;

        virtual int64_t last_write_time(const Path& target, std::error_code& ec) const = 0;
        int64_t last_write_time(const Path& target, LineInfo li) const noexcept;

        virtual Path absolute(const Path& target, std::error_code& ec) const = 0;
        Path absolute(const Path& target, LineInfo li) const;

        // absolute/system_complete + lexically_normal + fixup_win32_path_case
        // we don't use real canonical due to issues like:
        // https://github.com/microsoft/vcpkg/issues/16614 (canonical breaking on some older Windows Server containers)
        // https://github.com/microsoft/vcpkg/issues/18208 (canonical removing subst despite our recommendation to use
        // subst)
        virtual Path almost_canonical(const Path& target, std::error_code& ec) const = 0;
        Path almost_canonical(const Path& target, LineInfo li) const;

        virtual Path current_path(std::error_code&) const = 0;
        Path current_path(LineInfo li) const;

        virtual void current_path(const Path& new_current_path, std::error_code&) = 0;
        void current_path(const Path& new_current_path, LineInfo li);

        // if the path does not exist, then (try_|)take_exclusive_file_lock attempts to create the file
        // (but not any path members above the file itself)
        // in other words, if `/a/b` is a directory, and you're attempting to lock `/a/b/c`,
        // then these lock functions create `/a/b/c` if it doesn't exist;
        // however, if `/a/b` doesn't exist, then the functions will fail.

        // waits forever for the file lock
        virtual std::unique_ptr<IExclusiveFileLock> take_exclusive_file_lock(const Path& lockfile,
                                                                             std::error_code&) = 0;
        std::unique_ptr<IExclusiveFileLock> take_exclusive_file_lock(const Path& lockfile, LineInfo li);

        // waits, at most, 1.5 seconds, for the file lock
        virtual std::unique_ptr<IExclusiveFileLock> try_take_exclusive_file_lock(const Path& lockfile,
                                                                                 std::error_code&) = 0;
        std::unique_ptr<IExclusiveFileLock> try_take_exclusive_file_lock(const Path& lockfile, LineInfo li);

        virtual std::vector<Path> find_from_PATH(View<StringView> stems) const = 0;
        std::vector<Path> find_from_PATH(StringView stem) const;

        virtual ReadFilePointer open_for_read(const Path& file_path, std::error_code& ec) const = 0;
        ReadFilePointer open_for_read(const Path& file_path, LineInfo li) const;

        virtual WriteFilePointer open_for_write(const Path& file_path, std::error_code& ec) = 0;
        WriteFilePointer open_for_write(const Path& file_path, LineInfo li);
    };

    Filesystem& get_real_filesystem();

    static constexpr const char* FILESYSTEM_INVALID_CHARACTERS = R"(\/:*?"<>|)";

    bool has_invalid_chars_for_filesystem(const std::string& s);

    void print_paths(const std::vector<Path>& paths);

    constexpr char preferred_separator = VCPKG_PREFERRED_SEPARATOR[0];

#if defined(_WIN32)
    Path win32_fix_path_case(const Path& source);
#endif // _WIN32

    struct NotExtensionCaseSensitive
    {
        StringView ext;
        bool operator()(const Path& target) const { return target.extension() != ext; }
    };

    struct NotExtensionCaseInsensitive
    {
        StringView ext;
        bool operator()(const Path& target) const
        {
            return !Strings::case_insensitive_ascii_equals(target.extension(), ext);
        }
    };

    struct NotExtensionsCaseInsensitive
    {
        std::vector<std::string> exts;
        bool operator()(const Path& target) const
        {
            return !std::any_of(exts.begin(), exts.end(), [extension = target.extension()](const auto& ext) {
                return Strings::case_insensitive_ascii_equals(extension, ext);
            });
        }
    };
}

VCPKG_FORMAT_AS(vcpkg::Path, vcpkg::StringView);
