#pragma once
namespace vcpkg
{
#if defined(_WIN32)
    using CommandLineCharType = wchar_t;
#else
    using CommandLineCharType = char;
#endif // ^^^ !_WIN32

    enum class StabilityTag
    {
        Standard,            // no prefix or x-
        Experimental,        // x-
        ImplementationDetail // z-
    };

    struct HelpTableFormatter;
    struct CmdParser;
}
