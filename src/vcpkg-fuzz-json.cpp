#include <vcpkg/base/json.h>

using namespace vcpkg;

namespace vcpkg::Checks
{
    void on_final_cleanup_and_exit() { }
}

// avoid -Wmissing-prototypes
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    auto begin = reinterpret_cast<const char*>(data);
    StringView text(begin, size);
    if (!Unicode::utf8_is_valid_string(text.begin(), text.end()))
    {
        return -1;
    }

    static constexpr StringLiteral origin = "<stdin>";
    FullyBufferedDiagnosticContext fbdc;
    (void)Json::parse(fbdc, text, origin);
    return 0;
}
