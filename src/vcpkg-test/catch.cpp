#define CATCH_CONFIG_RUNNER
#include <vcpkg-test/util.h>

#include <vcpkg/base/curl.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.h>

namespace vcpkg::Checks
{
    void on_final_cleanup_and_exit() { }
}

int main(int argc, char** argv)
{
    vcpkg_curl_global_init(CURL_GLOBAL_DEFAULT);
    if (vcpkg::get_environment_variable("VCPKG_DEBUG").value_or("") == "1") vcpkg::Debug::g_debugging = true;
    // We set VCPKG_ROOT to an invalid value to ensure unit tests do not attempt to instantiate VcpkgRoot
    vcpkg::set_environment_variable("VCPKG_ROOT", "VCPKG_TESTS_SHOULD_NOT_USE_VCPKG_ROOT");

    return Catch::Session().run(argc, argv);
}
