option(VCPKG_DEPENDENCY_CMAKERC "CMake-based C++ resource compiler" OFF)

if(VCPKG_DEPENDENCY_CMAKERC)
    find_package(CMakeRC CONFIG REQUIRED)
    return()
endif()

# This option exists to allow the URI to be replaced with a Microsoft-internal URI in official
# builds which have restricted internet access; see azure-pipelines/signing.yml
# Note that the SHA512 is the same, so vcpkg-tool contributors need not be concerned that we built
# with different content.
set(VCPKG_CMAKERC_URL "https://github.com/vector-of-bool/cmrc/archive/refs/tags/2.0.1.tar.gz" CACHE STRING "URL to the cmrc release tarball to use.")

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

include(FetchContent)
find_package(Git REQUIRED)
FetchContent_Declare(
    CMakeRC
    URL "${VCPKG_CMAKERC_URL}"
    URL_HASH "SHA512=cb69ff4545065a1a89e3a966e931a58c3f07d468d88ecec8f00da9e6ce3768a41735a46fc71af56e0753926371d3ca5e7a3f2221211b4b1cf634df860c2c997f"
    PATCH_COMMAND "${GIT_EXECUTABLE}" apply "${CMAKE_CURRENT_LIST_DIR}/CMakeRC_cmake_4.patch"
)
FetchContent_MakeAvailable(CMakeRC)

if(NOT CMakeRC_FIND_REQUIRED)
    message(FATAL_ERROR "CMakeRC must be REQUIRED")
endif()
