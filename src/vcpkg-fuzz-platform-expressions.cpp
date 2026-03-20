#include <vcpkg/base/unicode.h>

#include <vcpkg/platform-expression.h>

using namespace vcpkg;

namespace vcpkg::Checks
{
    void on_final_cleanup_and_exit() { __debugbreak(); }
}

extern "C" int _cdecl LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    auto begin = reinterpret_cast<const char*>(data);
    StringView text(begin, size);
    if (!Unicode::utf8_is_valid_string(text.begin(), text.end()))
    {
        return -1;
    }

    (void)PlatformExpression::parse_platform_expression(text, PlatformExpression::MultipleBinaryOperators::Deny);
    (void)PlatformExpression::parse_platform_expression(text, PlatformExpression::MultipleBinaryOperators::Allow);
    return 0;
}
