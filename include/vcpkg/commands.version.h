#pragma once

#include <vcpkg/base/fwd/files.h>
#include <vcpkg/base/fwd/stringview.h>

#include <vcpkg/fwd/vcpkgcmdarguments.h>

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

namespace vcpkg
{
    extern const StringLiteral vcpkg_executable_version;
    extern const CommandMetadata CommandVersionMetadata;
    void command_version_and_exit(const VcpkgCmdArguments& args, const Filesystem& fs);
}
