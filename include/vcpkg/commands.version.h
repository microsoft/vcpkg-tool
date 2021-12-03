#pragma once

#include <vcpkg/commands.interface.h>

#define STRINGIFY(...) #__VA_ARGS__
#define MACRO_TO_STRING(X) STRINGIFY(X)

#if !defined(VCPKG_VERSION)
#error VCPKG_VERSION must be defined
#endif

#define VCPKG_VERSION_AS_STRING MACRO_TO_STRING(VCPKG_VERSION)

#if !defined(VCPKG_BASE_VERSION)
#error VCPKG_BASE_VERSION must be defined
#endif

#define VCPKG_BASE_VERSION_AS_STRING MACRO_TO_STRING(VCPKG_BASE_VERSION)

namespace vcpkg::Commands::Version
{
    const char* version() noexcept;
    void perform_and_exit(const VcpkgCmdArguments& args, Filesystem& fs);

    struct VersionCommand : BasicCommand
    {
        virtual void perform_and_exit(const VcpkgCmdArguments& args, Filesystem& fs) const override;
    };
}
