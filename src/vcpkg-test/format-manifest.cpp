#include <catch2/catch.hpp>

#include <vcpkg/base/files.h>
#include <vcpkg/base/stringview.h>

#include <vcpkg/commands.format-manifest.h>

#include <algorithm>
#include <system_error>

using namespace vcpkg;
using namespace vcpkg::Commands::FormatManifest;

namespace
{
    struct ListOfExistingFiles : Files::IFilesystemStatusProvider
    {
        std::vector<fs::path> allowed_paths;

        ListOfExistingFiles(std::vector<fs::path>&& allowed_paths_) : allowed_paths(std::move(allowed_paths_)) { }

        virtual fs::file_status status(const fs::path& path, std::error_code&) const noexcept override
        {
            const bool fileExists = std::any_of(allowed_paths.begin(),
                                                allowed_paths.end(),
                                                [&](const fs::path& candidate) { return path == candidate; });

            return fs::file_status{fileExists ? fs::file_type::regular : fs::file_type::none, fs::perms::unknown};
        }

        virtual fs::file_status symlink_status(const fs::path& path, std::error_code& ec) const noexcept override
        {
            return this->status(path, ec);
        }
    };

#if defined(_WIN32)
    StringView existing_absolute{"C:\\hello"};
    StringView missing_absolute{"C:\\hello\\world"};
#define SEPARATOR "\\"
#else // ^^^ defined(_WIN32) // !defined(_WIN32) vvv
    StringView existing_absolute{"/hello"};
    StringView missing_absolute{"/hello/world"};
#define SEPARATOR "/"
#endif // ^^^ !defined(_WIN32)

    const auto existing_absolute_path = fs::u8path(existing_absolute);

    const auto original_cwd = existing_absolute_path / fs::u8path("cwd");
    const auto ports = existing_absolute_path / fs::u8path("ports");

    ListOfExistingFiles filesystem{std::vector<fs::path>{
        existing_absolute_path,
        // relative test cases
        original_cwd,
        original_cwd / fs::u8path("example"),
        original_cwd / fs::u8path("example" SEPARATOR "CONTROL"),
        original_cwd / fs::u8path("example" SEPARATOR "vcpkg.json"),
        original_cwd / fs::u8path("example" SEPARATOR "anything.json"),
        // port name test cases
        ports,
        ports / fs::u8path("control-port"),
        ports / fs::u8path("control-port" SEPARATOR "CONTROL"),
        ports / fs::u8path("manifest-port"),
        ports / fs::u8path("manifest-port" SEPARATOR "vcpkg.json"),
        ports / fs::u8path("ambiguous-port"),
        ports / fs::u8path("ambiguous-port" SEPARATOR "CONTROL"),
        ports / fs::u8path("ambiguous-port" SEPARATOR "vcpkg.json"),
        // conflict between port name and filesystem name test cases
        original_cwd / fs::u8path("overlap-port"),
        ports / fs::u8path("overlap-port"),
        ports / fs::u8path("overlap-port" SEPARATOR "CONTROL"),
    }};
}

TEST_CASE ("resolves_existing_absolute_path", "[commands][format-manifest]")
{
    const auto result = resolve_format_manifest_input(existing_absolute, original_cwd, ports, filesystem);
    REQUIRE(result.has_value());
    REQUIRE(result.value_or_exit(VCPKG_LINE_INFO) == existing_absolute_path);
}

TEST_CASE ("does_not_resolve_missing_absolute_path", "[commands][format-manifest]")
{
    const auto result = resolve_format_manifest_input(missing_absolute, original_cwd, ports, filesystem);
    REQUIRE(!result.has_value());
    REQUIRE(result.error().find(" not found.") != std::string::npos);
}

TEST_CASE ("resolves_relative_paths", "[commands][format-manifest]")
{
    vcpkg::StringView relative_paths[] = {
        vcpkg::StringView{"example"},
        vcpkg::StringView{"example" SEPARATOR "CONTROL"},
        vcpkg::StringView{"example" SEPARATOR "vcpkg.json"},
        vcpkg::StringView{"example" SEPARATOR "anything.json"},
    };

    for (vcpkg::StringView& relative : relative_paths)
    {
        const auto result = resolve_format_manifest_input(relative, original_cwd, ports, filesystem);
        REQUIRE(result.has_value());
        REQUIRE(result.value_or_exit(VCPKG_LINE_INFO) == original_cwd / fs::u8path(relative));
    }
}

TEST_CASE ("resolves_control_port", "[commands][format-manifest]")
{
    const auto result = resolve_format_manifest_input("control-port", original_cwd, ports, filesystem);
    REQUIRE(result.has_value());
    REQUIRE(result.value_or_exit(VCPKG_LINE_INFO) == ports / fs::u8path("control-port" SEPARATOR "CONTROL"));
}

TEST_CASE ("resolves_manifest_port", "[commands][format-manifest]")
{
    const auto result = resolve_format_manifest_input("manifest-port", original_cwd, ports, filesystem);
    REQUIRE(result.has_value());
    REQUIRE(result.value_or_exit(VCPKG_LINE_INFO) == ports / fs::u8path("manifest-port" SEPARATOR "vcpkg.json"));
}

TEST_CASE ("does_not_resolve_ambiguous_port", "[commands][format-manifest]")
{
    const auto result = resolve_format_manifest_input("ambiguous-port", original_cwd, ports, filesystem);
    REQUIRE(!result.has_value());
    REQUIRE(result.error().find("Both a manifest file and a CONTROL file exist") != std::string::npos);
}

TEST_CASE ("chooses_filesystem_path_over_port_name", "[commands][format-manifest]")
{
    const auto result = resolve_format_manifest_input("overlap-port", original_cwd, ports, filesystem);
    REQUIRE(result.has_value());
    REQUIRE(result.value_or_exit(VCPKG_LINE_INFO) == original_cwd / fs::u8path("overlap-port"));
}
