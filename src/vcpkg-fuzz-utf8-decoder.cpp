#include <vcpkg/base/unicode.h>

namespace vcpkg::Checks
{
    void on_final_cleanup_and_exit() { }
}

// avoid -Wmissing-prototypes
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    auto begin = reinterpret_cast<const char*>(data);
    auto end = begin + size;
    vcpkg::Unicode::utf8_errc decode_error;
    vcpkg::Unicode::Utf8Decoder dec(begin, end, decode_error);
    while (!dec.is_eof() && decode_error == vcpkg::Unicode::utf8_errc::NoError)
    {
        decode_error = dec.next();
    }

    return 0;
}
