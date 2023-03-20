#define CATCH_CONFIG_RUNNER
#include <catch2/catch.hpp>

#include <vcpkg/base/basic-checks.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.h>

namespace vcpkg::Checks
{
    void on_final_cleanup_and_exit() { }
}

int main(int argc, char** argv)
{
    if (vcpkg::get_environment_variable("VCPKG_DEBUG").value_or("") == "1") vcpkg::Debug::g_debugging = true;
    // We set VCPKG_ROOT to an invalid value to ensure unit tests do not attempt to instantiate VcpkgRoot
    vcpkg::set_environment_variable("VCPKG_ROOT", "VCPKG_TESTS_SHOULD_NOT_USE_VCPKG_ROOT");

    return Catch::Session().run(argc, argv);
}
