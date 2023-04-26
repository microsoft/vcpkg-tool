#pragma once

#if defined(_WIN32)
#define VCPKG_PREFERRED_SEPARATOR "\\"
#else // ^^^ _WIN32 / !_WIN32 vvv
#define VCPKG_PREFERRED_SEPARATOR "/"
#endif // _WIN32

namespace vcpkg
{
    enum class CopyOptions
    {
        none = 0,
        skip_existing = 0x1,
        overwrite_existing = 0x2,
        update_existing = 0x3,
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

    enum class Append
    {
        NO = 0,
        YES,
    };

    struct IgnoreErrors;
    struct Path;
    struct FilePointer;
    struct ReadFilePointer;
    struct WriteFilePointer;
    struct IExclusiveFileLock;
    struct ILineReader;
    struct FileContents;
    struct RemoveFilesystem;
    struct Filesystem;
    struct NotExtensionCaseSensitive;
    struct NotExtensionCaseInsensitive;
    struct NotExtensionsCaseInsensitive;
}
