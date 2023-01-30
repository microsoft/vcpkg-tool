option(VCPKG_DEPENDENCY_CMAKERC "CMake-based C++ resource compiler" OFF)

# This option exists to allow the URI to be replaced with a Microsoft-internal URI in official
# builds which have restricted internet access; see azure-pipelines/signing.yml
# Note that the SHA512 is the same, so vcpkg-tool contributors need not be concerned that we built
# with different content.
set(VCPKG_CMAKERC_URL "https://github.com/vector-of-bool/cmrc/archive/refs/tags/2.0.1.tar.gz" CACHE STRING "URL to the cmrc release tarball to use.")

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

include(FetchContent)
FetchContent_Declare(
    CMakeRC
    URL "${VCPKG_CMAKERC_URL}"
    URL_HASH "SHA512=c50543ae990bbdcca6f6a8d9f78a26ce384c3dd3ab15212b520ce0f07b6fe8d4220eb7b448ae8b8b063653ee7a789f519f214deff9411c39e14d9c2b649d45e5"
)

if(NOT CMakeRC_FIND_REQUIRED)
    message(FATAL_ERROR "CMakeRC must be REQUIRED")
endif()

if(VCPKG_DEPENDENCY_CMAKERC)
    find_package(CMakeRC CONFIG REQUIRED)
else()
    FetchContent_MakeAvailable(CMakeRC)
endif()
