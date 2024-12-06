#pragma once

namespace vcpkg
{
    struct ParseError;
    struct SourceLoc;

    enum class MessageKind
    {
        Warning,
        Error,
    };

    struct ParseMessage;
    struct ParseMessages;
    struct ParserBase;
}
