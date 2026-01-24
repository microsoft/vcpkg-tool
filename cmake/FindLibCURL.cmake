if (WIN32 OR APPLE)
    set(VCPKG_LIBCURL "SYSTEM" CACHE STRING "Select libcurl provider (SYSTEM|DLSYM)")
else()
    set(VCPKG_LIBCURL "DLSYM" CACHE STRING "Select libcurl provider (SYSTEM|DLSYM)")
endif()

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

if (VCPKG_LIBCURL STREQUAL "SYSTEM")
    find_package(CURL REQUIRED)
    return()
elseif (NOT VCPKG_LIBCURL STREQUAL "DLSYM")
    message(FATAL_ERROR "Unsupported VCPKG_LIBCURL value '${VCPKG_LIBCURL}'. Expected SYSTEM or DLSYM.")
endif()

# This option exists to allow the URI to be replaced with a Microsoft-internal URI in official
# builds which have restricted internet access; see azure-pipelines/signing.yml
# Note that the SHA512 is the same, so vcpkg-tool contributors need not be concerned that we built
# with different content.
#
# We're using curl-7_23_0 headers here because that's what comes with RHEL 7 and curl has not broken
# ABI in a very long time, so that should still be acceptable on modern *nix
if(NOT VCPKG_LIBCURL_URL)
    set(VCPKG_LIBCURL_URL "https://github.com/curl/curl/archive/refs/tags/curl-7_23_0.tar.gz")
endif()

include(FetchContent)
FetchContent_Declare(
    LibCURLHeaders
    URL "${VCPKG_LIBCURL_URL}"
    URL_HASH "SHA512=d927d76c43b9803d260b2a400e3b5ac6bbd9517d00a2083dc55da066a7f7393ded22ccda3778ff4faa812461b779f92e4d0775d8985ceaddc28e349598fe8362"
)
FetchContent_GetProperties(LibCURLHeaders)
# This dance is done rather than `FetchContent_MakeAvailable` because we only want to download
# curl's headers for use with dlopen/dlsym rather than building curl.
if(NOT LibCURLHeaders_POPULATED)
    FetchContent_Populate(LibCURLHeaders)
endif()

if(NOT TARGET CURL::libcurl)
    add_library(CURL::libcurl INTERFACE IMPORTED)
endif()

set_target_properties(CURL::libcurl PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${LibCURLHeaders_SOURCE_DIR}/include"
    INTERFACE_COMPILE_DEFINITIONS "VCPKG_LIBCURL_DLSYM=1"
)
