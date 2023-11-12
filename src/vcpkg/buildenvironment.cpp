#include <vcpkg/base/fwd/message_sinks.h>

#include <vcpkg/buildenvironment.h>
#include <vcpkg/installedpaths.h>
#include <vcpkg/tools.h>
#include <vcpkg/vcpkgpaths.h>

namespace vcpkg
{
    Command make_cmake_cmd(const VcpkgPaths& paths,
                           const Path& cmake_script,
                           std::vector<CMakeVariable>&& pass_variables)
    {
        auto local_variables = std::move(pass_variables);
        local_variables.emplace_back("VCPKG_ROOT_DIR", paths.root);
        local_variables.emplace_back("PACKAGES_DIR", paths.packages());
        local_variables.emplace_back("BUILDTREES_DIR", paths.buildtrees());
        local_variables.emplace_back("_VCPKG_INSTALLED_DIR", paths.installed().root());
        local_variables.emplace_back("DOWNLOADS", paths.downloads);
        local_variables.emplace_back("VCPKG_MANIFEST_INSTALL", "OFF");
        return make_basic_cmake_cmd(paths.get_tool_exe(Tools::CMAKE, stdout_sink), cmake_script, local_variables);
    }
}
