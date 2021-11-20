#include <vcpkg/fwd/vcpkgpaths.h>

#include <vcpkg/base/view.h>

#include <string>

namespace vcpkg
{
    int run_configure_environment_command(const VcpkgPaths& paths, View<std::string> args);
    int run_configure_environment_command(const VcpkgPaths& paths, StringView arg0, View<std::string> args);
}
