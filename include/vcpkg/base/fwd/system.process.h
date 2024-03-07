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
}
