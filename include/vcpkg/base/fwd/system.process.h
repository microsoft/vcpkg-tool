#pragma once

namespace vcpkg
{
    enum class EchoInDebug
    {
        Show,
        Hide
    };

    enum class Encoding
    {
        Utf8,
        Utf16
    };

    enum class CreateNewConsole
    {
        No,
        Yes
    };

    struct CMakeVariable;
    struct Command;
    struct CommandLess;
    struct ExitCodeAndOutput;
    struct Environment;

    // The integral type the operating system uses to represent exit codes.
#if defined(_WIN32)
    using ExitCodeIntegral = unsigned long; // DWORD
#else
    using ExitCodeIntegral = int;
#endif // ^^^ !_WIN32
}
