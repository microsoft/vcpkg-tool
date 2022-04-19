#pragma once

namespace vcpkg
{
    struct CMakeVariable;
    struct Command;
    struct CommandLess;
    struct ExitCodeAndOutput;

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
}
