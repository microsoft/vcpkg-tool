#include <vcpkg/base/checks.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/system.print.h>

#include <vcpkg/commands.ciclean.h>
#include <vcpkg/installedpaths.h>
#include <vcpkg/vcpkgcmdarguments.h>
#include <vcpkg/vcpkgpaths.h>

using namespace vcpkg;

namespace
{
    void clear_directory(Filesystem& fs, const Path& target)
    {
        if (fs.is_directory(target))
        {
            msg::println(msgClearingContents, msg::path = target);
            fs.remove_all_inside(target, VCPKG_LINE_INFO);
        }
        else
        {
            msg::println(msgSkipClearingInvalidDir, msg::path = target);
        }
    }
}

namespace vcpkg::Commands::CIClean
{
    void perform_and_exit(const VcpkgCmdArguments&, const VcpkgPaths& paths)
    {
        using vcpkg::print2;
        auto& fs = paths.get_filesystem();
        clear_directory(fs, paths.buildtrees());
        clear_directory(fs, paths.installed().root());
        clear_directory(fs, paths.packages());
        Checks::exit_success(VCPKG_LINE_INFO);
    }

    void CICleanCommand::perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths) const
    {
        CIClean::perform_and_exit(args, paths);
    }
}
