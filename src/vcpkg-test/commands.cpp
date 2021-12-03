#include <catch2/catch.hpp>

#include <vcpkg/base/util.h>

#include <vcpkg/commands.contact.h>
#include <vcpkg/commands.h>
#include <vcpkg/commands.upload-metrics.h>
#include <vcpkg/commands.version.h>

#include <stddef.h>

#include <set>

using namespace vcpkg;

TEST_CASE ("list of commands is correct", "[commands]")
{
    using vcpkg::Util::Sets::contains;
    std::set<std::string> commands;
    for (auto command : Commands::get_available_basic_commands())
    {
        CHECK_FALSE(contains(commands, command.name));
        commands.insert(command.name);
    }
    for (auto command : Commands::get_available_paths_commands())
    {
        CHECK_FALSE(contains(commands, command.name));
        commands.insert(command.name);
    }
    for (auto command : Commands::get_available_triplet_commands())
    {
        CHECK_FALSE(contains(commands, command.name));
        commands.insert(command.name);
    }

    // clang-format off
    std::set<std::string> expected_commands{
        "contact",
        "search",
        "version",
        "hash",
        "x-download",
        "x-init-registry",
        "x-generate-default-message-map",
        "/?",
        "help",
        "list",
        "integrate",
        "new",
        "list",
        "owns",
        "update",
        "edit",
        "create",
        "cache",
        "portsdiff",
        "autocomplete",
        "fetch",
        "format-manifest",
        "x-ci-clean",
        "x-history",
        "x-package-info",
        "x-vsinstances",
        "x-ci-verify-versions",
        "x-add-version",
        "install",
        "x-set-installed",
        "ci",
        "remove",
        "upgrade",
        "build",
        "env",
        "build-external",
        "export",
        "depend-info",
        "activate",
        "find",
        "use",
        "add",
        "z-ce",
        "x-check-support",
        "z-print-config",
        "z-bootstrap-standalone",
#if defined(_WIN32)
        "x-upload-metrics",
#endif // defined(_WIN32)
        };
    // clang-format on

    CHECK(commands == expected_commands);
}
