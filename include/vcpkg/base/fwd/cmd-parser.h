#pragma once
namespace vcpkg
{
#if defined(_WIN32)
    using CommandLineCharType = wchar_t;
#else
    using CommandLineCharType = char;
#endif // ^^^ !_WIN32

    struct HelpTableFormatter;
}
