#include <vcpkg/base/lockguarded.h>

#include <vcpkg/globalstate.h>

namespace vcpkg
{
    ElapsedTimer GlobalState::timer;
    LockGuarded<std::string> GlobalState::g_surveydate;

    std::atomic<int> GlobalState::g_init_console_cp(0);
    std::atomic<int> GlobalState::g_init_console_output_cp(0);
    std::atomic<bool> GlobalState::g_init_console_initialized(false);
}
