#pragma once

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
    struct Filesystem;
    struct NotExtensionCaseSensitive;
    struct NotExtensionCaseInsensitive;
    struct NotExtensionsCaseInsensitive;
}
