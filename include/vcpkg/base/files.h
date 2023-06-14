#pragma once

#include <vcpkg/base/fwd/files.h>
#include <vcpkg/base/fwd/format.h>
#include <vcpkg/base/fwd/message_sinks.h>
#include <vcpkg/base/fwd/span.h>

#include <vcpkg/base/checks.h>
#include <vcpkg/base/expected.h>
#include <vcpkg/base/file-contents.h>
#include <vcpkg/base/lineinfo.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/path.h>
#include <vcpkg/base/stringview.h>

#include <stdio.h>
#include <string.h>

#include <initializer_list>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

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
        operator std::error_code&();

    private:
        std::error_code ec;
    };

    bool is_symlink(FileType s);
    bool is_regular_file(FileType s);
    bool is_directory(FileType s);
    bool exists(FileType s);

    struct FilePointer
    {
    protected:
        FILE* m_fs;
        Path m_path;

        FilePointer(const Path& path);

    public:
        FilePointer() noexcept;

        FilePointer(const FilePointer&) = delete;
        FilePointer(FilePointer&& other) noexcept;
        FilePointer& operator=(const FilePointer&) = delete;
        explicit operator bool() const noexcept;

        long long tell() const noexcept;
        int eof() const noexcept;
        std::error_code error() const noexcept;

        const Path& path() const;
        ExpectedL<Unit> try_seek_to(long long offset);
        ExpectedL<Unit> try_seek_to(long long offset, int origin);

        ~FilePointer();
    };

    struct ReadFilePointer : FilePointer
    {
        ReadFilePointer() noexcept;
        ReadFilePointer(ReadFilePointer&&) noexcept;
        explicit ReadFilePointer(const Path& file_path, std::error_code& ec);
        ReadFilePointer& operator=(ReadFilePointer&& other) noexcept;
        size_t read(void* buffer, size_t element_size, size_t element_count) const noexcept;
        ExpectedL<Unit> try_read_all(void* buffer, std::uint32_t size);
        ExpectedL<char> try_getc();
        ExpectedL<Unit> try_read_all_from(long long offset, void* buffer, std::uint32_t size);
        std::string read_to_end(std::error_code& ec);
    };

    struct WriteFilePointer : FilePointer
    {
        WriteFilePointer() noexcept;
        WriteFilePointer(WriteFilePointer&&) noexcept;
        explicit WriteFilePointer(const Path& file_path, Append append, std::error_code& ec);
        WriteFilePointer& operator=(WriteFilePointer&& other) noexcept;
        size_t write(const void* buffer, size_t element_size, size_t element_count) const noexcept;
        int put(int c) const noexcept;
    };

    struct IExclusiveFileLock
    {
        virtual ~IExclusiveFileLock();
    };

    uint64_t get_filesystem_stats();

    struct ILineReader
    {
        virtual ExpectedL<std::vector<std::string>> read_lines(const Path& file_path) const = 0;

        // Omitted to allow constexpr:
        // protected:
        //    ~ILineReader();
    };

    struct ReadOnlyFilesystem : ILineReader
    {
        virtual std::string read_contents(const Path& file_path, std::error_code& ec) const = 0;
        std::string read_contents(const Path& file_path, LineInfo li) const;

        ExpectedL<FileContents> try_read_contents(const Path& file_path) const;

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

        bool exists(const Path& target, std::error_code& ec) const;
        bool exists(const Path& target, LineInfo li) const;

        virtual bool is_directory(const Path& target) const = 0;
        virtual bool is_regular_file(const Path& target) const = 0;

        virtual bool is_empty(const Path& target, std::error_code& ec) const = 0;
        bool is_empty(const Path& target, LineInfo li) const;

        virtual FileType status(const Path& target, std::error_code& ec) const = 0;
        FileType status(const Path& target, LineInfo li) const noexcept;

        virtual FileType symlink_status(const Path& target, std::error_code& ec) const = 0;
        FileType symlink_status(const Path& target, LineInfo li) const noexcept;

        // absolute/system_complete + lexically_normal + fixup_win32_path_case
        // we don't use real canonical due to issues like:
        // https://github.com/microsoft/vcpkg/issues/16614 (canonical breaking on some older Windows Server containers)
        // https://github.com/microsoft/vcpkg/issues/18208 (canonical removing subst despite our recommendation to use
        // subst)
        virtual Path almost_canonical(const Path& target, std::error_code& ec) const = 0;
        Path almost_canonical(const Path& target, LineInfo li) const;

        virtual Path current_path(std::error_code&) const = 0;
        Path current_path(LineInfo li) const;

        virtual Path absolute(const Path& target, std::error_code& ec) const = 0;
        Path absolute(const Path& target, LineInfo li) const;

        virtual std::vector<Path> find_from_PATH(View<StringView> stems) const = 0;
        std::vector<Path> find_from_PATH(StringView stem) const;

        virtual ReadFilePointer open_for_read(const Path& file_path, std::error_code& ec) const = 0;
        ReadFilePointer open_for_read(const Path& file_path, LineInfo li) const;
        ExpectedL<ReadFilePointer> try_open_for_read(const Path& file_path) const;

        ExpectedL<bool> check_update_required(const Path& version_path, StringView expected_version) const;

        // Omitted to allow constexpr:
        // protected:
        //    ~ReadOnlyFilesystem();
    };

    struct Filesystem : ReadOnlyFilesystem
    {
        virtual void write_lines(const Path& file_path,
                                 const std::vector<std::string>& lines,
                                 std::error_code& ec) const = 0;
        void write_lines(const Path& file_path, const std::vector<std::string>& lines, LineInfo li) const;

        virtual void write_contents(const Path& file_path, StringView data, std::error_code& ec) const = 0;
        void write_contents(const Path& file_path, StringView data, LineInfo li) const;

        void write_rename_contents(const Path& file_path, const Path& temp_name, StringView data, LineInfo li) const;
        void write_contents_and_dirs(const Path& file_path, StringView data, LineInfo li) const;
        virtual void write_contents_and_dirs(const Path& file_path, StringView data, std::error_code& ec) const = 0;

        virtual void rename(const Path& old_path, const Path& new_path, std::error_code& ec) const = 0;
        void rename(const Path& old_path, const Path& new_path, LineInfo li) const;

        void rename_with_retry(const Path& old_path, const Path& new_path, std::error_code& ec) const;
        void rename_with_retry(const Path& old_path, const Path& new_path, LineInfo li) const;

        virtual void rename_or_copy(const Path& old_path,
                                    const Path& new_path,
                                    StringLiteral temp_suffix,
                                    std::error_code& ec) const = 0;

        virtual bool remove(const Path& target, std::error_code& ec) const = 0;
        bool remove(const Path& target, LineInfo li) const;

        virtual void remove_all(const Path& base, std::error_code& ec, Path& failure_point) const = 0;
        void remove_all(const Path& base, std::error_code& ec) const;
        void remove_all(const Path& base, LineInfo li) const;

        void remove_all_inside(const Path& base, std::error_code& ec, Path& failure_point) const;
        void remove_all_inside(const Path& base, std::error_code& ec) const;
        void remove_all_inside(const Path& base, LineInfo li) const;

        virtual bool create_directory(const Path& new_directory, std::error_code& ec) const = 0;
        bool create_directory(const Path& new_directory, LineInfo li) const;

        virtual bool create_directories(const Path& new_directory, std::error_code& ec) const = 0;
        bool create_directories(const Path& new_directory, LineInfo) const;

        virtual Path create_or_get_temp_directory(std::error_code& ec) const = 0;
        Path create_or_get_temp_directory(LineInfo) const;

        virtual void create_symlink(const Path& to, const Path& from, std::error_code& ec) const = 0;
        void create_symlink(const Path& to, const Path& from, LineInfo) const;

        virtual void create_directory_symlink(const Path& to, const Path& from, std::error_code& ec) const = 0;
        void create_directory_symlink(const Path& to, const Path& from, LineInfo) const;

        virtual void create_hard_link(const Path& to, const Path& from, std::error_code& ec) const = 0;
        void create_hard_link(const Path& to, const Path& from, LineInfo) const;

        void create_best_link(const Path& to, const Path& from, std::error_code& ec) const;
        void create_best_link(const Path& to, const Path& from, LineInfo) const;

        // copies regular files and directories, recursively.
        // symlinks are followed and copied as if they were regular files or directories
        //   (like std::filesystem::copy(..., std::filesystem::copy_options::recursive))
        virtual void copy_regular_recursive(const Path& source, const Path& destination, std::error_code& ec) const = 0;
        void copy_regular_recursive(const Path& source, const Path& destination, LineInfo) const;

        virtual bool copy_file(const Path& source,
                               const Path& destination,
                               CopyOptions options,
                               std::error_code& ec) const = 0;
        bool copy_file(const Path& source, const Path& destination, CopyOptions options, LineInfo li) const;

        virtual void copy_symlink(const Path& source, const Path& destination, std::error_code& ec) const = 0;
        void copy_symlink(const Path& source, const Path& destination, LineInfo li) const;

        using ReadOnlyFilesystem::current_path;
        virtual void current_path(const Path& new_current_path, std::error_code&) const = 0;
        void current_path(const Path& new_current_path, LineInfo li) const;

        // if the path does not exist, then (try_|)take_exclusive_file_lock attempts to create the file
        // (but not any path members above the file itself)
        // in other words, if `/a/b` is a directory, and you're attempting to lock `/a/b/c`,
        // then these lock functions create `/a/b/c` if it doesn't exist;
        // however, if `/a/b` doesn't exist, then the functions will fail.

        // waits forever for the file lock
        virtual std::unique_ptr<IExclusiveFileLock> take_exclusive_file_lock(const Path& lockfile,
                                                                             std::error_code&) const = 0;
        std::unique_ptr<IExclusiveFileLock> take_exclusive_file_lock(const Path& lockfile, LineInfo li) const;

        // waits, at most, 1.5 seconds, for the file lock
        virtual std::unique_ptr<IExclusiveFileLock> try_take_exclusive_file_lock(const Path& lockfile,
                                                                                 std::error_code&) const = 0;
        std::unique_ptr<IExclusiveFileLock> try_take_exclusive_file_lock(const Path& lockfile, LineInfo li) const;

        virtual WriteFilePointer open_for_write(const Path& file_path, Append append, std::error_code& ec) const = 0;
        WriteFilePointer open_for_write(const Path& file_path, Append append, LineInfo li) const;
        WriteFilePointer open_for_write(const Path& file_path, std::error_code& ec) const;
        WriteFilePointer open_for_write(const Path& file_path, LineInfo li) const;
    };

    extern const Filesystem& real_filesystem;

    extern const StringLiteral FILESYSTEM_INVALID_CHARACTERS;

    bool has_invalid_chars_for_filesystem(const std::string& s);

    void print_paths(MessageSink& msg_sink, const std::vector<Path>& paths);

#if defined(_WIN32)
    Path win32_fix_path_case(const Path& source);
#endif // _WIN32

    struct NotExtensionCaseSensitive
    {
        StringView ext;
        bool operator()(const Path& target) const;
    };

    struct NotExtensionCaseInsensitive
    {
        StringView ext;
        bool operator()(const Path& target) const;
    };

    struct NotExtensionsCaseInsensitive
    {
        std::vector<std::string> exts;
        bool operator()(const Path& target) const;
    };
}

VCPKG_FORMAT_AS(vcpkg::Path, vcpkg::StringView);
