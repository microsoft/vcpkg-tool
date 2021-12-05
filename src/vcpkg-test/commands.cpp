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
        "/?",
        "activate",
        "add",
        "autocomplete",
        "build",
        "build-external",
        "cache",
        "ci",
        "contact",
        "create",
        "depend-info",
        "edit",
        "env",
        "export",
        "fetch",
        "find",
        "format-manifest",
        "hash",
        "help",
        "install",
        "integrate",
        "list",
        "list",
        "new",
        "owns",
        "portsdiff",
        "remove",
        "search",
        "update",
        "upgrade",
        "use",
        "version",
        "x-add-version",
        "x-check-support",
        "x-ci-clean",
        "x-ci-verify-versions",
        "x-download",
        "x-generate-default-message-map",
        "x-history",
        "x-init-registry",
        "x-package-info",
        "x-regenerate",
        "x-set-installed",
        "x-vsinstances",
        "z-bootstrap-standalone",
        "z-ce",
        "z-print-config",
#if defined(_WIN32)
        "x-upload-metrics",
#endif // defined(_WIN32)
        };
    // clang-format on

    CHECK(commands == expected_commands);
}
