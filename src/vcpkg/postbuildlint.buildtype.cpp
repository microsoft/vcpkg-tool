#include <vcpkg/base/checks.h>

#include <vcpkg/postbuildlint.buildtype.h>

namespace vcpkg::PostBuildLint
{
    bool BuildType::has_crt_linker_option(StringView sv) const
    {
        // "/DEFAULTLIB:LIBCMTD";
        // "/DEFAULTLIB:MSVCRTD";
        // "/DEFAULTLIB:LIBCMT[^D]";
        // "/DEFAULTLIB:LIBCMT[^D]";

        constexpr static const StringLiteral static_crt = "/DEFAULTLIB:LIBCMT";
        constexpr static const StringLiteral dynamic_crt = "/DEFAULTLIB:MSVCRT";

        StringView option = linkage == Build::LinkageType::STATIC ? static_crt : dynamic_crt;

        auto found = Strings::case_insensitive_ascii_search(sv, option);
        if (found == sv.end())
        {
            return false;
        }

        auto option_end = found + option.size();
        if (config == Build::ConfigurationType::DEBUG)
        {
            if (option_end == sv.end() || !Strings::icase_eq(*option_end, 'd'))
            {
                return false;
            }
            ++option_end;
        }

        return option_end == sv.end() || ParserBase::is_whitespace(*option_end);
    }

    StringLiteral BuildType::to_string() const
    {
        if (config == Build::ConfigurationType::DEBUG)
        {
            if (linkage == Build::LinkageType::STATIC)
            {
                return "Debug,Static";
            }
            else
            {
                return "Debug,Dynamic";
            }
        }
        else
        {
            if (linkage == Build::LinkageType::STATIC)
            {
                return "Release,Static";
            }
            else
            {
                return "Release,Dynamic";
            }
        }
    }
}
