#include <vcpkg/base/system.write.h>

#include <stdio.h>
#include <vcpkg/base/strings.h>

namespace vcpkg
{
#if defined(_WIN32)
    static bool is_console(HANDLE h)
    {
        DWORD mode = 0;
        // GetConsoleMode succeeds iff `h` is a console
        // we do not actually care about the mode of the console
        return GetConsoleMode(h, &mode);
    }

    void write_text_to_std_handle(StringView sv, StdHandle handle_id)
    {
        auto handle = GetStdHandle(static_cast<DWORD>(handle_id));
        if (is_console(handle))
        {
            auto as_wstr = Strings::to_utf16(sv);

            const wchar_t* pointer = as_wstr.data();
            ::size_t size = as_wstr.size();

            while (size != 0)
            {
                DWORD to_write = size > static_cast<DWORD>(-1)
                    ? static_cast<DWORD>(-1)
                    : static_cast<DWORD>(size);
                DWORD written = 0;

                BOOL success = WriteConsoleW(handle, pointer, to_write, &written, nullptr);
                if (!success)
                {
                    fwprintf(stderr,
                        L"[DEBUG] Failed to print to handle %lu: %lu\n",
                        static_cast<DWORD>(handle_id),
                        GetLastError()
                    );
                    std::abort();
                }
                size -= written;
            }
        }
        else
        {
            const char* pointer = sv.data();
            ::size_t size = sv.size();

            while (size != 0)
            {
                DWORD to_write = size > static_cast<DWORD>(-1)
                    ? static_cast<DWORD>(-1)
                    : static_cast<DWORD>(size);
                DWORD written = 0;

                BOOL success = WriteFile(handle, pointer, to_write, &written, nullptr);
                if (!success)
                {
                    fwprintf(stderr,
                        L"[DEBUG] Failed to print to handle %lu: %lu\n",
                        static_cast<DWORD>(handle_id),
                        GetLastError()
                    );
                    std::abort();
                }
                size -= written;
            }
        }
    }
#else
#endif
}
