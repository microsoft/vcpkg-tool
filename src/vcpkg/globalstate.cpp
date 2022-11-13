#include <vcpkg/globalstate.h>

namespace vcpkg
{
    std::atomic<int> g_init_console_cp(0);
    std::atomic<int> g_init_console_output_cp(0);
    std::atomic<bool> g_init_console_initialized(false);
}
