#include <catch2/catch.hpp>

#include <vcpkg/portlint.h>

using namespace vcpkg;
using namespace vcpkg::Lint;

TEST_CASE ("Lint::get_recommended_license_expression", "[portlint]")
{
    REQUIRE(get_recommended_license_expression("GPL-1.0") == "GPL-1.0-only");
    REQUIRE(get_recommended_license_expression("GPL-1.0 OR test") == "GPL-1.0-only OR test");
    REQUIRE(get_recommended_license_expression("GPL-1.0 OR GPL-1.0") == "GPL-1.0-only OR GPL-1.0-only");
    REQUIRE(get_recommended_license_expression("GPL-1.0+ OR GPL-1.0") == "GPL-1.0-or-later OR GPL-1.0-only");
}

TEST_CASE ("Lint::get_recommended_version_scheme", "[portlint]")
{
    REQUIRE(get_recommended_version_scheme("1.0.0", VersionScheme::String) == VersionScheme::Relaxed);
    REQUIRE(get_recommended_version_scheme("2020-01-01", VersionScheme::String) == VersionScheme::Date);
    REQUIRE(get_recommended_version_scheme("latest", VersionScheme::String) == VersionScheme::String);
}

namespace
{
    struct CountingMessageSink : MessageSink
    {
        int counter = 0;
        void print(Color, StringView) override { ++counter; }
    };

    void check_replacement(StringView old_content, const std::string& new_content, StringView new_host_dependency)
    {
        CountingMessageSink msg_sink;
        auto result = check_portfile_deprecated_functions(old_content.to_string(), "test", Fix::YES, msg_sink);
        REQUIRE(result.status == Status::Fixed);
        REQUIRE(msg_sink.counter == 0);
        if (result.new_portfile_content != new_content)
        {
            REQUIRE(Strings::replace_all(result.new_portfile_content, "\r", "\\r") ==
                    Strings::replace_all(new_content, "\r", "\\r"));
        }
        if (new_host_dependency.empty())
        {
            REQUIRE(result.added_host_deps.empty());
        }
        else
        {
            REQUIRE(result.added_host_deps.size() == 1);
            REQUIRE(*result.added_host_deps.begin() == new_host_dependency);
        }

        result = check_portfile_deprecated_functions(old_content.to_string(), "test", Fix::NO, msg_sink);
        REQUIRE(result.status == Status::Problem);
        REQUIRE(msg_sink.counter == 1);
        REQUIRE(result.new_portfile_content.empty());
        REQUIRE(result.added_host_deps.empty());
    }
}

TEST_CASE ("Lint::check_portfile_deprecated_functions", "[portlint]")
{
    SECTION ("vcpkg_build_msbuild")
    {
        auto content = R"-(
vcpkg_build_msbuild(
    PROJECT_PATH "${SOURCE_PATH}/msvc/unicorn.sln"
    PLATFORM "${UNICORN_PLATFORM}"
)
)-";
        CountingMessageSink msg_sink;
        auto result = check_portfile_deprecated_functions(content, "test", Fix::YES, msg_sink);
        REQUIRE(result.status == Status::Problem);
        REQUIRE(msg_sink.counter == 1);
    }

    SECTION ("vcpkg_configure_cmake -> vcpkg_cmake_configure")
    {
        auto content = R"-(
vcpkg_configure_cmake(
    SOURCE_PATH ${SOURCE_PATH}
    PREFER_NINJA
    OPTIONS_DEBUG -DDISABLE_INSTALL_HEADERS=ON -DDISABLE_INSTALL_TOOLS=ON
)
vcpkg_configure_cmake(
    SOURCE_PATH ${SOURCE_PATH}
    OPTIONS_DEBUG -DDISABLE_INSTALL_HEADERS=ON -DDISABLE_INSTALL_TOOLS=ON
)
)-";
        auto new_content = R"-(
vcpkg_cmake_configure(
    SOURCE_PATH ${SOURCE_PATH}
    OPTIONS_DEBUG -DDISABLE_INSTALL_HEADERS=ON -DDISABLE_INSTALL_TOOLS=ON
)
vcpkg_cmake_configure(
    SOURCE_PATH ${SOURCE_PATH}
    OPTIONS_DEBUG -DDISABLE_INSTALL_HEADERS=ON -DDISABLE_INSTALL_TOOLS=ON
)
)-";
        check_replacement(content, new_content, "vcpkg-cmake");
    }

    SECTION ("vcpkg_build_cmake -> vcpkg_cmake_build")
    {
        auto content = R"-(
vcpkg_build_cmake(TARGET test)
)-";
        auto new_content = R"-(
vcpkg_cmake_build(TARGET test)
)-";
        check_replacement(content, new_content, "vcpkg-cmake");
    }

    SECTION ("vcpkg_install_cmake -> vcpkg_cmake_install")
    {
        auto content = R"-(
vcpkg_install_cmake()
)-";
        auto new_content = R"-(
vcpkg_cmake_install()
)-";
        check_replacement(content, new_content, "vcpkg-cmake");
    }

    SECTION ("vcpkg_fixup_cmake_targets -> vcpkg_cmake_config_fixup")
    {
        auto content = R"-(
vcpkg_fixup_cmake_targets(CONFIG_PATH lib/cmake/${PORT})
vcpkg_fixup_cmake_targets(TARGET_PATH share/${PORT})
vcpkg_fixup_cmake_targets(CONFIG_PATH share/unofficial-cfitsio TARGET_PATH share/unofficial-cfitsio)
vcpkg_fixup_cmake_targets(CONFIG_PATH cmake TARGET_PATH share/async++)
)-";
        auto new_content = R"-(
vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/${PORT})
vcpkg_cmake_config_fixup()
vcpkg_cmake_config_fixup(PACKAGE_NAME unofficial-cfitsio)
vcpkg_cmake_config_fixup(CONFIG_PATH cmake PACKAGE_NAME async++)
)-";
        check_replacement(content, new_content, "vcpkg-cmake-config");
    }

    SECTION ("vcpkg_extract_source_archive_ex -> vcpkg_extract_source_archive")
    {
        std::string content = R"-(
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
)
vcpkg_extract_source_archive_ex(
    OUT_SOURCE_PATH SOURCE_PATH
    ARCHIVE ${ARCHIVE}
    REF lib1.0.0
    PATCHES
        remove_stdint_headers.patch
        no-pragma-warning.patch
)
vcpkg_extract_source_archive_ex(
    ARCHIVE ${ARCHIVE}
    OUT_SOURCE_PATH SOURCE_PATH
REF
lib1.0.0
    PATCHES
        remove_stdint_headers.patch
        no-pragma-warning.patch
)
vcpkg_extract_source_archive_ex(OUT_SOURCE_PATH SOURCE_PATH ARCHIVE ${ARCHIVE})
)-";
        std::string new_content = R"-(
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
)
vcpkg_extract_source_archive(
    SOURCE_PATH
    ARCHIVE ${ARCHIVE}
    SOURCE_BASE lib1.0.0
    PATCHES
        remove_stdint_headers.patch
        no-pragma-warning.patch
)
vcpkg_extract_source_archive(
    SOURCE_PATH
    ARCHIVE ${ARCHIVE}
SOURCE_BASE
lib1.0.0
    PATCHES
        remove_stdint_headers.patch
        no-pragma-warning.patch
)
vcpkg_extract_source_archive(SOURCE_PATH ARCHIVE ${ARCHIVE})
)-";
        check_replacement(content, new_content, "");
        check_replacement(
            Strings::replace_all(content, "\n", "\r\n"), Strings::replace_all(new_content, "\n", "\r\n"), "");
    }
}
